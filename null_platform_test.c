/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES

#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <drm_fourcc.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

const char * get_gl_error()
{
	switch (glGetError()) {
	case GL_NO_ERROR:
		return "GL_NO_ERROR";
	case GL_INVALID_ENUM:
		return "GL_INVALID_ENUM";
	case GL_INVALID_VALUE:
		return "GL_INVALID_VALUE";
	case GL_INVALID_OPERATION:
		return "GL_INVALID_OPERATION";
	case GL_INVALID_FRAMEBUFFER_OPERATION:
		return "GL_INVALID_FRAMEBUFFER_OPERATION";
	case GL_OUT_OF_MEMORY:
		return "GL_OUT_OF_MEMORY";
	default:
		return "Unknown error";
	}
}

const char * get_egl_error()
{
	switch (eglGetError()) {
	case EGL_SUCCESS:
		return "EGL_SUCCESS";
	case EGL_NOT_INITIALIZED:
		return "EGL_NOT_INITIALIZED";
	case EGL_BAD_ACCESS:
		return "EGL_BAD_ACCESS";
	case EGL_BAD_ALLOC:
		return "EGL_BAD_ALLOC";
	case EGL_BAD_ATTRIBUTE:
		return "EGL_BAD_ATTRIBUTE";
	case EGL_BAD_CONTEXT:
		return "EGL_BAD_CONTEXT";
	case EGL_BAD_CONFIG:
		return "EGL_BAD_CONFIG";
	case EGL_BAD_CURRENT_SURFACE:
		return "EGL_BAD_CURRENT_SURFACE";
	case EGL_BAD_DISPLAY:
		return "EGL_BAD_DISPLAY";
	case EGL_BAD_SURFACE:
		return "EGL_BAD_SURFACE";
	case EGL_BAD_MATCH:
		return "EGL_BAD_MATCH";
	case EGL_BAD_PARAMETER:
		return "EGL_BAD_PARAMETER";
	case EGL_BAD_NATIVE_PIXMAP:
		return "EGL_BAD_NATIVE_PIXMAP";
	case EGL_BAD_NATIVE_WINDOW:
		return "EGL_BAD_NATIVE_WINDOW";
	case EGL_CONTEXT_LOST:
		return "EGL_CONTEXT_LOST";
	default:
		return "Unknown error";
	}
}

#define BUFFERS 2

struct context {
	int drm_card_fd;
	struct gbm_device *drm_gbm;

	EGLDisplay egl_display;
	EGLContext egl_ctx;

	drmModeConnector * connector;
	drmModeEncoder * encoder;
	drmModeModeInfo * mode;

	struct gbm_bo * gbm_buffer[BUFFERS];
	EGLImageKHR egl_image[BUFFERS];
	uint32_t drm_fb_id[BUFFERS];
	unsigned gl_fb[BUFFERS];
	unsigned gl_rb[BUFFERS];

};

bool setup_drm(struct context * ctx)
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
		return false;
	}

	ctx->connector = connector;
	ctx->encoder = encoder;
	ctx->mode = &connector->modes[0];

	return true;
}

float f(int i) {
	int a = i % 40;
	int b = (i / 40) % 6;
	switch (b) {
	case 0:
	case 1:
		return 0.0f;
	case 3:
	case 4:
		return 1.0f;
	case 2:
		return (a / 40.0f);
	case 5:
		return 1.0f - (a / 40.0f);
	default:
		return 0.0f;
	}
}

static void page_flip_handler(int fd, unsigned int frame, unsigned int sec,
			      unsigned int usec, void *data)
{
	int *waiting_for_flip = data;
	*waiting_for_flip = 0;
}

void draw(struct context * ctx)
{
	int i;
	const GLchar *vertexShaderStr =
		"attribute vec4 vPosition;\n"
		"attribute vec4 vColor;\n"
		"varying vec4 vFillColor;\n"
		"void main() {\n"
		"  gl_Position = vPosition;\n"
		"  vFillColor = vColor;\n"
		"}\n";
	const GLchar *fragmentShaderStr =
		"precision mediump float;\n"
		"varying vec4 vFillColor;\n"
		"void main() {\n"
		"  gl_FragColor = vFillColor;\n"
		"}\n";
	GLint vertexShader, fragmentShader, program, status;

	vertexShader = glCreateShader(GL_VERTEX_SHADER);
	if (!vertexShader) {
		fprintf(stderr, "Failed to create vertex shader. Error=0x%x\n", glGetError());
		return;
	}
	glShaderSource(vertexShader, 1, &vertexShaderStr, NULL);
	glCompileShader(vertexShader);
	glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &status);
	if (!status) {
		fprintf(stderr, "Failed to compile vertex shader. Error=0x%x\n", glGetError());
		return;
	}

	fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	if (!fragmentShader) {
		fprintf(stderr, "Failed to create fragment shader. Error=0x%x\n", glGetError());
		return;
	}
	glShaderSource(fragmentShader, 1, &fragmentShaderStr, NULL);
	glCompileShader(fragmentShader);
	glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &status);
	if (!status) {
		fprintf(stderr, "Failed to compile fragment shader. Error=0x%x\n", glGetError());
		return;
	}


	program = glCreateProgram();
	if (!program) {
		fprintf(stderr, "Failed to create program.\n");
		return;
	}
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glBindAttribLocation(program, 0, "vPosition");
	glBindAttribLocation(program, 1, "vColor");
	glLinkProgram(program);
	glGetShaderiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		fprintf(stderr, "Failed to link program.\n");
		return;
	}

	int fb_idx = 1;
	for (i = 0; i <= 500; i++) {
		int waiting_for_flip = 1;
		GLfloat verts[] = {
			0.0f, -0.5f, 0.0f,
			-0.5f, 0.5f, 0.0f,
			0.5f, 0.5f, 0.0f
		};
		GLfloat colors[] = {
			1.0f, 0.0f, 0.0f, 1.0f,
			0.0f, 1.0f, 0.0f, 1.0f,
			0.0f, 0.0f, 1.0f, 1.0f
		};

		glBindFramebuffer(GL_FRAMEBUFFER, ctx->gl_fb[fb_idx]);
		glViewport(0, 0,(GLint) ctx->mode->hdisplay,
			(GLint) ctx->mode->vdisplay);

		glClearColor(f(i), f(i + 80), f(i + 160), 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		glUseProgram(program);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, verts);
		glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, colors);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glDrawArrays(GL_TRIANGLES, 0, 3);

		usleep(1e6 / 120); /* 120 Hz */
		glFinish();
		drmModePageFlip(ctx->drm_card_fd, ctx->encoder->crtc_id,
				ctx->drm_fb_id[fb_idx],
				DRM_MODE_PAGE_FLIP_EVENT,
				&waiting_for_flip);

		while (waiting_for_flip) {
			drmEventContext evctx = {
				.version = DRM_EVENT_CONTEXT_VERSION,
				.page_flip_handler = page_flip_handler,
			};

			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(0, &fds);
			FD_SET(ctx->drm_card_fd, &fds);

			int ret = select(ctx->drm_card_fd + 1, &fds, NULL, NULL, NULL);
			if (ret < 0) {
				fprintf(stderr, "select err: %s\n", strerror(errno));
				return;
			} else if (ret == 0) {
				fprintf(stderr, "select timeout\n");
				return;
			} else if (FD_ISSET(0, &fds)) {
				fprintf(stderr, "user interrupted\n");
				return;
			}
			drmHandleEvent(ctx->drm_card_fd, &evctx);
		}
		fb_idx = fb_idx ^ 1;
	}

	glDeleteProgram(program);
}

int main(int argc, char ** argv)
{
	int ret = 0;
	struct context ctx;
	EGLint egl_major, egl_minor;
	const char * extensions;
	uint32_t bo_handle;
	uint32_t bo_stride;
	int drm_prime_fd;
	EGLint num_configs;
	EGLConfig egl_config;
	size_t i;
	char* drm_card_path = "/dev/dri/card0";

	const EGLint config_attribs[] = {
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_DEPTH_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
	const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	if (argc >= 2)
		drm_card_path = argv[1];

	ctx.drm_card_fd = open(drm_card_path, O_RDWR);
	if (ctx.drm_card_fd < 0) {
		fprintf(stderr, "failed to open %s\n", drm_card_path);
		ret = 1;
		goto fail;
	}

	ctx.drm_gbm = gbm_create_device(ctx.drm_card_fd);
	if (!ctx.drm_gbm) {
		fprintf(stderr, "failed to create gbm device on %s\n", drm_card_path);
		ret = 1;
		goto close_drm_card;
	}

	ctx.egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (ctx.egl_display == EGL_NO_DISPLAY) {
		fprintf(stderr, "failed to get egl display\n");
		ret = 1;
		goto destroy_drm_gbm;
	}

	if (!eglInitialize(ctx.egl_display, &egl_major, &egl_minor)) {
		fprintf(stderr, "failed to initialize egl: %s\n",
			get_egl_error());
		ret = 1;
		goto terminate_display;
	}

	fprintf(stderr, "EGL %d.%d\n", egl_major, egl_minor);
	fprintf(stderr, "EGL %s\n",
		eglQueryString(ctx.egl_display, EGL_VERSION));

	extensions = eglQueryString(ctx.egl_display, EGL_EXTENSIONS);
	fprintf(stderr, "EGL Extensions: %s\n", extensions);

	if (!setup_drm(&ctx)) {
		fprintf(stderr, "failed to setup drm resources\n");
		ret = 1;
		goto terminate_display;
	}

	fprintf(stderr, "display size: %dx%d\n",
		ctx.mode->hdisplay, ctx.mode->vdisplay);

	if (!eglChooseConfig(ctx.egl_display, config_attribs, NULL, 0,
				&num_configs)) {
		fprintf(stderr, "eglChooseConfig() failed with error: %x\n", eglGetError());
		goto free_drm;
	}
	if (!eglChooseConfig(ctx.egl_display, config_attribs, &egl_config, 1,
				&num_configs)) {
		fprintf(stderr, "eglChooseConfig() failed with error: %x\n", eglGetError());
		goto free_drm;
	}

	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		fprintf(stderr, "failed to bind OpenGL ES: %s\n",
			get_egl_error());
		ret = 1;
		goto free_drm;
	}

	ctx.egl_ctx = eglCreateContext(ctx.egl_display,
		egl_config,
		EGL_NO_CONTEXT /* No shared context */,
		context_attribs);
	if (ctx.egl_ctx == EGL_NO_CONTEXT) {
		fprintf(stderr, "failed to create OpenGL ES Context: %s\n",
			get_egl_error());
		ret = 1;
		goto free_drm;
	}

	if (!eglMakeCurrent(ctx.egl_display,
			EGL_NO_SURFACE /* No default draw surface */,
			EGL_NO_SURFACE /* No default draw read */,
			ctx.egl_ctx)) {
		fprintf(stderr, "failed to make the OpenGL ES Context current: %s\n",
			get_egl_error());
		ret = 1;
		goto destroy_context;
	}

	fprintf(stderr, "GL extensions: %s\n", glGetString(GL_EXTENSIONS));

	glGenFramebuffers(BUFFERS, ctx.gl_fb);
	glGenRenderbuffers(BUFFERS, ctx.gl_rb);

	for (i = 0; i < BUFFERS; ++i) {
		ctx.gbm_buffer[i] = gbm_bo_create(ctx.drm_gbm,
			ctx.mode->hdisplay, ctx.mode->vdisplay, GBM_BO_FORMAT_XRGB8888,
			GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

		if (!ctx.gbm_buffer[i]) {
			fprintf(stderr, "failed to create buffer object\n");
			ret = 1;
			goto free_buffers;
		}

		bo_handle = gbm_bo_get_handle(ctx.gbm_buffer[i]).u32;
		bo_stride = gbm_bo_get_stride(ctx.gbm_buffer[i]);

		drm_prime_fd = gbm_bo_get_fd(ctx.gbm_buffer[i]);

		if (drm_prime_fd < 0) {
			fprintf(stderr, "failed to turn handle into fd\n");
			ret = 1;
			goto free_buffers;
		}

		const EGLint khr_image_attrs[] = {
			EGL_WIDTH, ctx.mode->hdisplay,
			EGL_HEIGHT, ctx.mode->vdisplay,
			EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_XRGB8888,
			EGL_DMA_BUF_PLANE0_FD_EXT, drm_prime_fd,
			EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
			EGL_DMA_BUF_PLANE0_PITCH_EXT, bo_stride,
			EGL_NONE
		};

		ctx.egl_image[i] = eglCreateImageKHR(ctx.egl_display, EGL_NO_CONTEXT,
			EGL_LINUX_DMA_BUF_EXT, NULL, khr_image_attrs);
		if (ctx.egl_image[i] == EGL_NO_IMAGE_KHR) {
			fprintf(stderr, "failed to create egl image: %s\n",
				get_egl_error());
			ret = 1;
			goto free_buffers;
		}

		glBindRenderbuffer(GL_RENDERBUFFER, ctx.gl_rb[i]);
		glBindFramebuffer(GL_FRAMEBUFFER, ctx.gl_fb[i]);
		glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, ctx.egl_image[i]);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_RENDERBUFFER, ctx.gl_rb[i]);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
				GL_FRAMEBUFFER_COMPLETE) {
			fprintf(stderr, "failed to create framebuffer: %s\n", get_gl_error());
			ret = 1;
			goto free_buffers;
		}

		ret = drmModeAddFB(ctx.drm_card_fd, ctx.mode->hdisplay, ctx.mode->vdisplay,
			24, 32, bo_stride, bo_handle, &ctx.drm_fb_id[i]);

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
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	for (i = 0; i < BUFFERS; ++i) {
		if (ctx.drm_fb_id[i])
			drmModeRmFB(ctx.drm_card_fd, ctx.drm_fb_id[i]);
		if (ctx.egl_image[i])
			eglDestroyImageKHR(ctx.egl_display, ctx.egl_image[i]);
		if (ctx.gl_fb[i])
			glDeleteFramebuffers(1, &ctx.gl_fb[i]);
		if (ctx.gl_rb[i])
			glDeleteRenderbuffers(1, &ctx.gl_rb[i]);
		if (ctx.gbm_buffer[i])
			gbm_bo_destroy(ctx.gbm_buffer[i]);
	}
destroy_context:
	eglDestroyContext(ctx.egl_display, ctx.egl_ctx);
free_drm:
	drmModeFreeConnector(ctx.connector);
	drmModeFreeEncoder(ctx.encoder);
terminate_display:
	eglTerminate(ctx.egl_display);
destroy_drm_gbm:
	gbm_device_destroy(ctx.drm_gbm);
close_drm_card:
	close(ctx.drm_card_fd);
fail:
	return ret;
}
