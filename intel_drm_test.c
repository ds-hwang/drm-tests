/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <gbm.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/i915_drm.h>
#include <libdrm/intel_bufmgr.h>

#define BUFFERS 2

struct context {
	int drm_card_fd;
	drm_intel_bufmgr* buffer_manager;
	drmModeRes *resources;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
	drmModeModeInfo *mode;
	unsigned long stride;
	drm_intel_bo *intel_buffer[BUFFERS];
	uint32_t drm_fb_id[BUFFERS];

};

void disable_psr() {
	const char psr_path[] = "/sys/module/i915/parameters/enable_psr";
	int psr_fd = open(psr_path, O_WRONLY);

	if (psr_fd < 0)
		return;

	if (write(psr_fd, "0", 1) == -1) {
		fprintf(stderr, "failed to disable psr\n");
	} else {
		fprintf(stderr, "disabled psr\n");
	}

	close(psr_fd);
}

void do_fixes() {
	disable_psr();
}

const char g_sys_card_path_format[] =
   "/sys/bus/platform/devices/vgem/drm/card%d";
const char g_dev_card_path_format[] =
   "/dev/dri/card%d";

void * mmap_intel_bo(drm_intel_bo *buffer)
{
  int error = drm_intel_bo_map(buffer, 1);
  if (error) {
    fprintf(stderr, "fail to map a drm buffer\n");
    return NULL;
  }
  assert(buffer->virtual);
	return buffer->virtual;
}

bool setup_drm(struct context *ctx)
{
	int fd = ctx->drm_card_fd;
	drmModeRes *resources = NULL;
	drmModeConnector *connector = NULL;
	drmModeEncoder *encoder = NULL;
	int i, j;

	resources = drmModeGetResources(fd);
	if (!resources) {
		fprintf(stderr, "drmModeGetResources failed\n");
		return false;
	}

	for (i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (connector == NULL)
			continue;

		if (connector->connection == DRM_MODE_CONNECTED &&
				connector->count_modes > 0)
			break;

		drmModeFreeConnector(connector);
	}

	if (i == resources->count_connectors) {
		fprintf(stderr, "no currently active connector found\n");
		drmModeFreeResources(resources);
		return false;
	}

	for (i = 0; i < resources->count_encoders; i++) {
		encoder = drmModeGetEncoder(fd, resources->encoders[i]);

		if (encoder == NULL)
			continue;

		for (j = 0; j < connector->count_encoders; j++) {
			if (encoder->encoder_id == connector->encoders[j])
				break;
		}

		if (j == connector->count_encoders) {
			drmModeFreeEncoder(encoder);
			continue;
		}

		break;
	}

	if (i == resources->count_encoders) {
		fprintf(stderr, "no supported encoder found\n");
		drmModeFreeConnector(connector);
		drmModeFreeResources(resources);
		return false;
	}

	for (i = 0; i < resources->count_crtcs; i++) {
		if (encoder->possible_crtcs & (1 << i)) {
			encoder->crtc_id = resources->crtcs[i];
			break;
		}
	}

	if (i == resources->count_crtcs) {
		fprintf(stderr, "no possible crtc found\n");
		drmModeFreeEncoder(encoder);
		drmModeFreeConnector(connector);
		drmModeFreeResources(resources);
		return false;
	}

	ctx->resources = resources;
	ctx->connector = connector;
	ctx->encoder = encoder;
	ctx->mode = &connector->modes[0];

	return true;
}

#define STEP_SKIP 0
#define STEP_MMAP 1
#define STEP_FAULT 2
#define STEP_FLIP 3
#define STEP_DRAW 4

void show_sequence(const int *sequence)
{
	int sequence_subindex;
	fprintf(stderr, "starting sequence: ");
	for (sequence_subindex = 0; sequence_subindex < 4; sequence_subindex++) {
		switch (sequence[sequence_subindex]) {
		case STEP_SKIP:
			break;
		case STEP_MMAP:
			fprintf(stderr, "mmap ");
			break;
		case STEP_FAULT:
			fprintf(stderr, "fault ");
			break;
		case STEP_FLIP:
			fprintf(stderr, "flip ");
			break;
		case STEP_DRAW:
			fprintf(stderr, "draw ");
			break;
		default:
			fprintf(stderr, "<unknown step %d> (aborting!)\n", sequence[sequence_subindex]);
			abort();
			break;
		}
	}
	fprintf(stderr, "\n");
}

void draw(struct context *ctx)
{
	int i;

	// Run the drawing routine with the key driver events in different
	// sequences.
	const int sequences[4][4] = {
		{ STEP_MMAP, STEP_FAULT, STEP_FLIP, STEP_DRAW },
		{ STEP_MMAP, STEP_FLIP,  STEP_DRAW, STEP_SKIP },
		{ STEP_MMAP, STEP_DRAW,  STEP_FLIP, STEP_SKIP },
		{ STEP_FLIP, STEP_MMAP,  STEP_DRAW, STEP_SKIP },
	};

	int sequence_index = 0;
	int sequence_subindex = 0;

	int fb_idx = 1;

	for (sequence_index = 0; sequence_index < 4; sequence_index++) {
		show_sequence(sequences[sequence_index]);
		for (i = 0; i < 0x100; i++) {
			size_t bo_stride = ctx->stride;
			size_t bo_size = ctx->stride * ctx->mode->vdisplay;
			uint32_t *bo_ptr;
			volatile uint32_t *ptr;

			for (sequence_subindex = 0; sequence_subindex < 4; sequence_subindex++) {
				switch (sequences[sequence_index][sequence_subindex]) {
				case STEP_MMAP:
					bo_ptr = (uint32_t*)mmap_intel_bo(ctx->intel_buffer[fb_idx]);
					ptr = bo_ptr;
					break;

				case STEP_FAULT:
					*ptr = 1234567;
					break;

				case STEP_FLIP:
					drmModePageFlip(ctx->drm_card_fd, ctx->encoder->crtc_id,
						ctx->drm_fb_id[fb_idx],
						0,
						NULL);
					break;

				case STEP_DRAW:
					for (ptr = bo_ptr; ptr < bo_ptr + (bo_size / sizeof(*bo_ptr)); ptr++) {
						int y = ((void*)ptr - (void*)bo_ptr) / bo_stride;
						int x = ((void*)ptr - (void*)bo_ptr - bo_stride * y) / sizeof(*ptr);
						x -= 100;
						y -= 100;
						*ptr = 0xff000000;
						if (x * x + y * y < i * i)
							*ptr |= (i % 0x100) << 8;
						else
							*ptr |= 0xff | (sequence_index * 64 << 16);
					}
					break;

				case STEP_SKIP:
				default:
					break;
				}
			}

			drm_intel_bo_unmap(ctx->intel_buffer[fb_idx]);

			usleep(1e6 / 120); /* 120 Hz */

			fb_idx = fb_idx ^ 1;
		}
	}
}

int main(int argc, char **argv)
{
	int ret = 0;
	struct context ctx;
	size_t i;
	char *drm_card_path = "/dev/dri/card0";

	if (argc >= 2)
		drm_card_path = argv[1];

	do_fixes();

	ctx.drm_card_fd = open(drm_card_path, O_RDWR);
	if (ctx.drm_card_fd < 0) {
		fprintf(stderr, "failed to open %s\n", drm_card_path);
		ret = 1;
		goto fail;
	}

	if (!setup_drm(&ctx)) {
		fprintf(stderr, "failed to setup drm resources\n");
		ret = 1;
		goto fail;
	}

	fprintf(stderr, "display size: %dx%d\n",
		ctx.mode->hdisplay, ctx.mode->vdisplay);

	ctx.buffer_manager = drm_intel_bufmgr_gem_init(ctx.drm_card_fd, 16 * 4096);
	if (!ctx.buffer_manager) {
    fprintf(stderr, "drm_intel_bufmgr_gem_init failed\n");
    goto fail;
	}

	for (i = 0; i < BUFFERS; ++i) {
		uint32_t tiling_mode = I915_TILING_NONE;
		unsigned long stride = 0;
		ctx.intel_buffer[i] = drm_intel_bo_alloc_tiled(
		    ctx.buffer_manager,
		    "chromium-gpu-memory-buffer", ctx.mode->hdisplay, ctx.mode->vdisplay,
		    4, &tiling_mode, &stride, 0);

		if (!ctx.intel_buffer[i]) {
			fprintf(stderr, "failed to create buffer object\n");
			ret = 1;
			goto free_buffers;
		}

		ctx.stride = stride;

		ret = drmModeAddFB(ctx.drm_card_fd, ctx.mode->hdisplay, ctx.mode->vdisplay,
			24, 32, stride, ctx.intel_buffer[i]->handle, &ctx.drm_fb_id[i]);

		if (ret) {
			fprintf(stderr, "failed to add fb\n");
			ret = 1;
			goto free_buffers;
		}
	}

	if (drmModeSetCrtc(ctx.drm_card_fd, ctx.encoder->crtc_id, ctx.drm_fb_id[0],
			0, 0, &ctx.connector->connector_id, 1, ctx.mode)) {
		fprintf(stderr, "failed to set CRTC\n");
		ret = 1;
		goto free_buffers;
	}

	draw(&ctx);

free_buffers:
	for (i = 0; i < BUFFERS; ++i) {
		if (ctx.drm_fb_id[i])
			drmModeRmFB(ctx.drm_card_fd, ctx.drm_fb_id[i]);
		if (ctx.intel_buffer[i])
		  drm_intel_bo_unreference(ctx.intel_buffer[i]);
	}

	drmModeFreeConnector(ctx.connector);
	drmModeFreeEncoder(ctx.encoder);
	drmModeFreeResources(ctx.resources);
close_drm_card:
	close(ctx.drm_card_fd);
fail:
	return ret;
}
