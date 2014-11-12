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
		return "No error has been recorded.";
	case GL_INVALID_ENUM:
		return "An unacceptable value is specified for an enumerated argument. The offending command is ignored and has no other side effect than to set the error flag.";
	case GL_INVALID_VALUE:
		return "A numeric argument is out of range. The offending command is ignored and has no other side effect than to set the error flag.";
	case GL_INVALID_OPERATION:
		return "The specified operation is not allowed in the current state. The offending command is ignored and has no other side effect than to set the error flag.";
	case GL_INVALID_FRAMEBUFFER_OPERATION:
		return "The command is trying to render to or read from the framebuffer while the currently bound framebuffer is not framebuffer complete (i.e. the return value from glCheckFramebufferStatus is not GL_FRAMEBUFFER_COMPLETE). The offending command is ignored and has no other side effect than to set the error flag.";
	case GL_OUT_OF_MEMORY:
		return "There is not enough memory left to execute the command. The state of the GL is undefined, except for the state of the error flags, after this error is recorded.";
	default:
		return "Unknown error";
	}
}

const char * get_egl_error()
{
	switch (eglGetError()) {
	case EGL_SUCCESS:
		return "The last function succeeded without error.";
	case EGL_NOT_INITIALIZED:
		return "EGL is not initialized, or could not be initialized, for the specified EGL display connection.";
	case EGL_BAD_ACCESS:
		return "EGL cannot access a requested resource (for example a context is bound in another thread).";
	case EGL_BAD_ALLOC:
		return "EGL failed to allocate resources for the requested operation.";
	case EGL_BAD_ATTRIBUTE:
		return "An unrecognized attribute or attribute value was passed in the attribute list.";
	case EGL_BAD_CONTEXT:
		return "An EGLContext argument does not name a valid EGL rendering context.";
	case EGL_BAD_CONFIG:
		return "An EGLConfig argument does not name a valid EGL frame buffer configuration.";
	case EGL_BAD_CURRENT_SURFACE:
		return "The current surface of the calling thread is a window, pixel buffer or pixmap that is no longer valid.";
	case EGL_BAD_DISPLAY:
		return "An EGLDisplay argument does not name a valid EGL display connection.";
	case EGL_BAD_SURFACE:
		return "An EGLSurface argument does not name a valid surface (window, pixel buffer or pixmap) configured for GL rendering.";
	case EGL_BAD_MATCH:
		return "Arguments are inconsistent (for example, a valid context requires buffers not supplied by a valid surface).";
	case EGL_BAD_PARAMETER:
		return "One or more argument values are invalid.";
	case EGL_BAD_NATIVE_PIXMAP:
		return "A NativePixmapType argument does not refer to a valid native pixmap.";
	case EGL_BAD_NATIVE_WINDOW:
		return "A NativeWindowType argument does not refer to a valid native window.";
	case EGL_CONTEXT_LOST:
		return "A power management event has occurred. The application must destroy all contexts and reinitialise OpenGL ES state and objects to continue rendering.";
	default:
		return "Unknown error";
	}
}

struct context {
	int card_fd;
	struct gbm_device * gbm_dev;
	struct gbm_bo * gbm_buffer;

	EGLDisplay egl_display;
	EGLContext egl_ctx;
	EGLImageKHR egl_image;

	drmModeConnector * connector;
	drmModeEncoder * encoder;
	drmModeModeInfo * mode;
	uint32_t drm_fb_id;

	unsigned gl_fb;
	unsigned gl_rb;

};

bool setup_drm(struct context * ctx)
{
	int fd = ctx->card_fd;
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


	glViewport(0, 0,(GLint) ctx->mode->hdisplay,
		(GLint) ctx->mode->vdisplay);

	for (i = 0; i <= 500; i++) {
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
		drmModePageFlip(ctx->card_fd, ctx->encoder->crtc_id,
				ctx->drm_fb_id, 0, NULL);
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
	char* card_path = "/dev/dri/card1";

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
		card_path = argv[1];

	ctx.card_fd = open(card_path, O_RDWR);
	if (ctx.card_fd < 0) {
		fprintf(stderr, "failed to open %s\n", card_path);
		ret = 1;
		goto fail;
	}

	ctx.gbm_dev = gbm_create_device(ctx.card_fd);
	if (!ctx.gbm_dev) {
		fprintf(stderr, "failed to create gbm device\n");
		ret = 1;
		goto close_card;
	}

	ctx.egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (ctx.egl_display == EGL_NO_DISPLAY) {
		fprintf(stderr, "failed to get egl display\n");
		ret = 1;
		goto destroy_device;
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
		NULL /* No framebuffer */,
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

	ctx.gbm_buffer = gbm_bo_create(ctx.gbm_dev,
		ctx.mode->hdisplay, ctx.mode->vdisplay, GBM_BO_FORMAT_XRGB8888,
		GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

	if (!ctx.gbm_buffer) {
		fprintf(stderr, "failed to create buffer object\n");
		ret = 1;
		goto destroy_context;
	}

	bo_handle = gbm_bo_get_handle(ctx.gbm_buffer).u32;
	bo_stride = gbm_bo_get_stride(ctx.gbm_buffer);

	if (drmPrimeHandleToFD(ctx.card_fd, bo_handle, DRM_CLOEXEC,
			&drm_prime_fd))	{
		fprintf(stderr, "failed to turn handle into fd\n");
		ret = 1;
		goto destroy_buffer;
	}

	glGenFramebuffers(1, &ctx.gl_fb);
	glBindFramebuffer(GL_FRAMEBUFFER, ctx.gl_fb);
	glGenRenderbuffers(1, &ctx.gl_rb);
	glBindRenderbuffer(GL_RENDERBUFFER, ctx.gl_rb);

	const EGLint khr_image_attrs[] = {
		EGL_WIDTH, ctx.mode->hdisplay,
		EGL_HEIGHT, ctx.mode->vdisplay,
		EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_XRGB8888,
		EGL_DMA_BUF_PLANE0_FD_EXT, drm_prime_fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
		EGL_DMA_BUF_PLANE0_PITCH_EXT, bo_stride,
		EGL_NONE
	};

	ctx.egl_image = eglCreateImageKHR(ctx.egl_display, EGL_NO_CONTEXT,
		EGL_LINUX_DMA_BUF_EXT, NULL, khr_image_attrs);
	if (ctx.egl_image == EGL_NO_IMAGE_KHR) {
		fprintf(stderr, "failed to create egl image: %s\n",
			get_egl_error());
		ret = 1;
		goto delete_gl_buffers;
	}

	glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, ctx.egl_image);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_RENDERBUFFER, ctx.gl_rb);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
			GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "failed to create framebuffer: %s\n", get_gl_error());
		ret = 1;
		goto destroy_image;
	}

	ret = drmModeAddFB(ctx.card_fd, ctx.mode->hdisplay, ctx.mode->vdisplay,
		24, 32, bo_stride, bo_handle, &ctx.drm_fb_id);

	if (ret) {
		fprintf(stderr, "failed to add fb\n");
		ret = 1;
		goto destroy_image;
	}

	fprintf(stderr, "drm fb id: %d\n", ctx.drm_fb_id);
        fprintf(stderr, "calling drmModeSetCrtc with crtc_id = %d\n", ctx.encoder->crtc_id);

	if (drmModeSetCrtc(ctx.card_fd, ctx.encoder->crtc_id, ctx.drm_fb_id,
			0, 0, &ctx.connector->connector_id, 1, ctx.mode)) {
		fprintf(stderr, "failed to set CRTC\n");
		ret = 1;
		goto remove_fb;
	}

	draw(&ctx);

remove_fb:
	drmModeRmFB(ctx.card_fd, ctx.drm_fb_id);
destroy_image:
	eglDestroyImageKHR(ctx.egl_display, ctx.egl_image);
delete_gl_buffers:
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &ctx.gl_fb);
	glDeleteRenderbuffers(1, &ctx.gl_rb);
destroy_buffer:
	gbm_bo_destroy(ctx.gbm_buffer);
destroy_context:
	eglDestroyContext(ctx.egl_display, ctx.egl_ctx);
free_drm:
	drmModeFreeConnector(ctx.connector);
	drmModeFreeEncoder(ctx.encoder);
terminate_display:
	eglTerminate(ctx.egl_display);
destroy_device:
	gbm_device_destroy(ctx.gbm_dev);
close_card:
	close(ctx.card_fd);
fail:
	return ret;
}
