/*
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include "bo.h"
#include "dev.h"

#define TABLE_LINEAR			0
#define TABLE_NEGATIVE			1
#define TABLE_POW			2
#define TABLE_STEP			3

#define FLAG_INTERNAL			'i'
#define FLAG_EXTERNAL			'e'
#define FLAG_GAMMA			'g'
#define FLAG_LINEAR			'l'
#define FLAG_NEGATIVE			'n'
#define FLAG_TIME			't'
#define FLAG_CRTCS			'c'
#define FLAG_PERSIST			'p'
#define FLAG_STEP			's'
#define FLAG_HELP			'h'

static struct option command_options[] = {
	{ "internal", no_argument, NULL, FLAG_INTERNAL },
	{ "external", no_argument, NULL, FLAG_EXTERNAL },
	{ "gamma", required_argument, NULL, FLAG_GAMMA },
	{ "linear", no_argument, NULL, FLAG_LINEAR },
	{ "negative", no_argument, NULL, FLAG_NEGATIVE },
	{ "time", required_argument, NULL, FLAG_TIME },
	{ "crtcs", required_argument, NULL, FLAG_CRTCS },
	{ "persist", no_argument, NULL, FLAG_PERSIST },
	{ "step", no_argument, NULL, FLAG_STEP },
	{ "help", no_argument, NULL, FLAG_HELP },
	{ NULL, 0, NULL, 0 }
};

static void
gamma_linear(uint16_t *table, int size)
{
	int i;
	for (i = 0; i < size; i++) {
		float v = (float)(i) / (float)(size - 1);
		v *= 65535.0f;
		table[i] = (uint16_t)v;
	}
}

static void
gamma_inv(uint16_t *table, int size)
{
	int i;
	for (i = 0; i < size; i++) {
		float v = (float)(size - 1 - i) / (float)(size - 1);
		v *= 65535.0f;
		table[i] = (uint16_t)v;
	}
}

static void
gamma_pow(uint16_t *table, int size, float p)
{
	int i;
	for (i = 0; i < size; i++) {
		float v = (float)(i) / (float)(size - 1);
		v = pow(v, p);
		v *= 65535.0f;
		table[i] = (uint16_t)v;
	}
}

static void
gamma_step(uint16_t *table, int size)
{
	int i;
	for (i = 0; i < size; i++) {
		table[i] = (i < size / 2) ? 0 : 65535;
	}
}


static void
fsleep(double secs)
{
	usleep((useconds_t)(1000000.0f * secs));
}

static int
is_internal(uint32_t connector_type)
{
	return connector_type == DRM_MODE_CONNECTOR_LVDS ||
	       connector_type == DRM_MODE_CONNECTOR_eDP ||
	       connector_type == DRM_MODE_CONNECTOR_DSI;
}

int
find_connector_for_encoder(int fd, drmModeRes *resources, uint32_t encoder_id,
			   int internal, uint32_t *connector_id)
{
	drmModeConnector *connector;
	int c, e;
	int found = 0;
	printf("looking for connector for encoder %u\n", encoder_id);
	for (c = 0; c < resources->count_connectors && !found; c++) {
		if (!(connector = drmModeGetConnector(fd, resources->connectors[c])))
			continue;
		printf("trying connector %u connected:%u, modes:%u, type:%u\n",
		       connector->connector_id, connector->connection,
		       connector->count_modes, connector->connector_type);
		if (connector->connection == DRM_MODE_CONNECTED &&
		    connector->count_modes > 0 &&
		    internal == is_internal(connector->connector_type)) {
			for (e = 0; e < connector->count_encoders; e++)
				if (connector->encoders[e] == encoder_id) {
					*connector_id = connector->connector_id;
					found = 1;
					break;
				}
		}
		drmModeFreeConnector(connector);
	}
	return found;
}

int
find_connector_encoder(int fd, drmModeRes *resources, uint32_t crtc_id,
		       int internal, uint32_t *encoder_id,
		       uint32_t *connector_id)
{
	int found = 0;
	drmModeEncoder *encoder;
	int e, c;

	for (e = 0; e < resources->count_encoders && !found; e++) {
		if ((encoder = drmModeGetEncoder(fd,
						 resources->encoders[e]))) {
			printf("trying encoder %u for crtc %u\n",
			       encoder->encoder_id, crtc_id);
			for (c = 0; c < resources->count_crtcs; c++) {
				printf("possible crtcs:%08X\n", encoder->possible_crtcs);
				if (encoder->possible_crtcs & (1u << c) &&
				    resources->crtcs[c] == crtc_id) {
					found =
					find_connector_for_encoder(fd,
								   resources,
								   encoder->encoder_id,
								   internal,
								   connector_id);
					if (found) {
						*encoder_id = encoder->encoder_id;
						break;
					}
				}
			}
		}
		drmModeFreeEncoder(encoder);
	}

	if (!found) {
		fprintf(stderr,
			"Could not find active %s connectors for crtc:%u\n",
			internal?"internal":"external", crtc_id);
	}

	return found;
}

int
find_best_mode(int fd, uint32_t connector_id, drmModeModeInfoPtr mode)
{
	int m;
	int found = 0;
	drmModeConnector *connector;

	connector = drmModeGetConnector(fd, connector_id);
	if (!connector)
		return 0;

	for (m = 0; m < connector->count_modes && !found; m++) {
		if (connector->modes[m].type & DRM_MODE_TYPE_PREFERRED) {
			*mode = connector->modes[m];
			found = 1;
		}
	}

	if (!found) {
		*mode = connector->modes[0];
		found = 1;
	}
	drmModeFreeConnector(connector);
	return found;
}

static void
draw_pattern(struct sp_bo *bo)
{
	uint32_t stripw = bo->width / 256;
	uint32_t striph = bo->height / 4;
	uint32_t x;

	fill_bo(bo, 0, 0, 0, 0);
	for (x = 0; x < 256; x++) {
		draw_rect(bo, x*stripw, 0, stripw, striph, 0, x, x, x);
		draw_rect(bo, x*stripw, striph, stripw, striph, 0, x, 0, 0);
		draw_rect(bo, x*stripw, striph*2, stripw, striph, 0, 0, x, 0);
		draw_rect(bo, x*stripw, striph*3, stripw, striph, 0, 0, 0, x);
	}
}

static int
set_gamma(int fd, drmModeCrtc *crtc, int gamma_table, float gamma)
{
	int res;
	uint16_t *r, *g, *b;
	r = calloc(crtc->gamma_size, sizeof(*r));
	g = calloc(crtc->gamma_size, sizeof(*g));
	b = calloc(crtc->gamma_size, sizeof(*b));

	printf("Setting gamma table %d\n", gamma_table);
	switch (gamma_table) {
		case TABLE_LINEAR:
			gamma_linear(r, crtc->gamma_size);
			gamma_linear(g, crtc->gamma_size);
			gamma_linear(b, crtc->gamma_size);
			break;
		case TABLE_NEGATIVE:
			gamma_inv(r, crtc->gamma_size);
			gamma_inv(g, crtc->gamma_size);
			gamma_inv(b, crtc->gamma_size);
			break;
		case TABLE_POW:
			gamma_pow(r, crtc->gamma_size, gamma);
			gamma_pow(g, crtc->gamma_size, gamma);
			gamma_pow(b, crtc->gamma_size, gamma);
			break;
		case TABLE_STEP:
			gamma_step(r, crtc->gamma_size);
			gamma_step(g, crtc->gamma_size);
			gamma_step(b, crtc->gamma_size);
			break;
	}

	res = drmModeCrtcSetGamma(fd, crtc->crtc_id, crtc->gamma_size, r, g, b);
	if (res != 0) {
		fprintf(stderr, "drmModeCrtcSetGamma(%d) failed: %s\n",
			crtc->crtc_id, strerror(errno));
	}
	free(r); free(g); free(b);
	return res;
}

void help(void)
{
	printf("\
gamma test\n\
command line options:\
\n\
--help - this\n\
--linear - set linear gamma table\n\
--negative - set negative linear gamma table\n\
--step - set step gamma table\n\
--gamma=f - set pow(gamma) gamma table with gamma=f\n\
--time=f - set test time\n\
--crtcs=n - set mask of crtcs to test\n\
--persist - do not reset gamma table at the end of the test\n\
--internal - display tests on internal display\n\
--external - display tests on external display\n\
");
}

int main(int argc, char **argv)
{
	drmModeRes *resources;
	struct sp_dev *dev;
	int internal = 1;
	int persist = 0;
	float time = 5.0;
	float gamma = 2.2f;
	float table = TABLE_LINEAR;
	unsigned int crtcs = 0xFFFF;
	int c;

	for (;;) {
		c = getopt_long(argc, argv, "", command_options, NULL);

		if (c == -1)
			break;

		switch (c) {
			case FLAG_HELP:
				help();
				return 0;

			case FLAG_INTERNAL:
				internal = 1;
				break;

			case FLAG_EXTERNAL:
				internal = 0;
				break;

			case FLAG_GAMMA:
				gamma = strtof(optarg, NULL);
				table = TABLE_POW;
				break;

			case FLAG_LINEAR:
				table = TABLE_LINEAR;
				break;

			case FLAG_NEGATIVE:
				table = TABLE_NEGATIVE;
				break;

			case FLAG_STEP:
				table = TABLE_STEP;
				break;

			case FLAG_TIME:
				time = strtof(optarg, NULL);
				break;

			case FLAG_CRTCS:
				crtcs = strtoul(optarg, NULL, 0);
				break;

			case FLAG_PERSIST:
				persist = 1;
				break;
		}
	}

	dev = create_sp_dev();
	if (!dev) {
		fprintf(stderr, "Creating DRM device failed\n");
		return 1;
	}

	resources = drmModeGetResources(dev->fd);
	if (!resources) {
		fprintf(stderr, "drmModeGetResources failed: %s\n",
			strerror(errno));
		return 1;
	}

	for (c = 0; c < resources->count_crtcs; c++) {
		int ret;
		drmModeCrtc *crtc;
		drmModeModeInfo mode;
		uint32_t encoder_id, connector_id;
		struct sp_bo *bo;

		if (!(crtcs & (1u << c))) continue;

		crtc = drmModeGetCrtc(dev->fd, resources->crtcs[c]);
		if (!crtc) {
			fprintf(stderr, "drmModeGetCrtc(%d) failed: %s\n",
				resources->crtcs[c], strerror(errno));
			continue;
		}

		printf("CRTC:%d gamma size:%d\n", crtc->crtc_id,
		       crtc->gamma_size);

		if (!crtc->gamma_size) {
			fprintf(stderr, "CRTC %d has no gamma table\n",
				crtc->crtc_id);
			drmModeFreeCrtc(crtc);
			continue;
		}

		if (!find_connector_encoder(dev->fd, resources, crtc->crtc_id,
					    internal, &encoder_id,
					    &connector_id)) {
			fprintf(stderr,
				"Could not find connector and encoder for CRTC %d\n",
				crtc->crtc_id);
			drmModeFreeCrtc(crtc);
			continue;
		}
		printf("Using CRTC:%u ENCODER:%u CONNECTOR:%u\n",
		       crtc->crtc_id, encoder_id, connector_id);

		if (!find_best_mode(dev->fd, connector_id, &mode)) {
			fprintf(stderr, "Could not find mode for CRTC %d\n",
			        crtc->crtc_id);
			drmModeFreeCrtc(crtc);
			continue;
		}
		printf("Using mode %s\n", mode.name);

		printf("Creating buffer %ux%u\n", mode.hdisplay, mode.vdisplay);
		bo = create_sp_bo(dev, mode.hdisplay, mode.vdisplay, 24, 32,
				  DRM_FORMAT_XRGB8888, 0);

		draw_pattern(bo);

		ret = drmModeSetCrtc(dev->fd, crtc->crtc_id, bo->fb_id, 0, 0,
				     &connector_id, 1, &mode);
		if (ret < 0) {
			fprintf(stderr, "Could not set mode on CRTC %d %s\n",
				crtc->crtc_id, strerror(errno));
			return 1;
		}

		ret = set_gamma(dev->fd, crtc, table, gamma);
		if (ret != 0) {
			return ret;
		}

		fsleep(time);

		if (!persist) {
			ret = set_gamma(dev->fd, crtc, TABLE_LINEAR, 0.0f);
			if (ret != 0) {
				return ret;
			}
		}

		ret = drmModeSetCrtc(dev->fd, crtc->crtc_id, 0, 0, 0, NULL, 0,
				     NULL);
		if (ret < 0) {
			fprintf(stderr, "Could disable CRTC %d %s\n",
				crtc->crtc_id, strerror(errno));
		}
		free_sp_bo(bo);
	}

	drmModeFreeResources(resources);


	return 0;
}
