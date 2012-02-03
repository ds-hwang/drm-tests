/*
 * Copyright © 2011 Kristian Høgsberg
 * Copyright © 2011 Benjamin Franzke
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES

#include <gbm.h>
#include <GL/gl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#ifdef GL_OES_EGL_image
static PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC glEGLImageTargetRenderbufferStorageOES_func;
#endif

struct kms {
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   drmModeModeInfo mode;
   uint32_t fb_id[2];
};

GLfloat x = 1.0;
GLfloat y = 1.0;
GLfloat xstep = 1.0f;
GLfloat ystep = 1.0f;
GLfloat rsize = 50;

int quit = 0;

static EGLBoolean
setup_kms(int fd, struct kms *kms)
{
   drmModeRes *resources;
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   int i;

   resources = drmModeGetResources(fd);
   if (!resources) {
      fprintf(stderr, "drmModeGetResources failed\n");
      return EGL_FALSE;
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
      fprintf(stderr, "No currently active connector found.\n");
      return EGL_FALSE;
   }

   for (i = 0; i < resources->count_encoders; i++) {
      encoder = drmModeGetEncoder(fd, resources->encoders[i]);

      if (encoder == NULL)
	 continue;

      if (encoder->encoder_id == connector->encoder_id)
	 break;

      drmModeFreeEncoder(encoder);
   }

   kms->connector = connector;
   kms->encoder = encoder;
   kms->mode = connector->modes[0];

   return EGL_TRUE;
}

static void
render_stuff(int width, int height)
{
   glViewport(0, 0, (GLint) width, (GLint) height);

   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();

   glOrtho(0, width, 0, height, 1.0, -1.0);

   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();


   glClear(GL_COLOR_BUFFER_BIT);
   glColor3f(1.0f, 0.0f, 0.0f);

   glRectf(x, y, x + rsize, y + rsize);

   glFlush();

   if (x <= 0 || x >= width - rsize)
     xstep *= -1;

   if (y <= 0 || y >= height - rsize)
     ystep *= -1;

   x += xstep;
   y += ystep;
}

static const char device_name[] = "/dev/dri/card0";

static void
page_flip_handler(int fd, unsigned int frame,
		  unsigned int sec, unsigned int usec, void *data)
{
  ;
}

void quit_handler(int signum)
{
  quit = 1;
  printf("Quitting!\n");
}

int main(int argc, char *argv[])
{
   EGLDisplay dpy;
   EGLContext ctx;
   EGLImageKHR image[2];
   EGLint major, minor;
   const char *ver, *extensions;
   GLuint fb, color_rb[2];
   uint32_t handle, stride;
   struct kms kms;
   int ret, fd, i, frames = 0, current = 0;
   struct gbm_device *gbm;
   struct gbm_bo *bo[2];
   drmModeCrtcPtr saved_crtc;
   time_t start, end;

   signal (SIGINT, quit_handler);

   fd = open(device_name, O_RDWR);
   if (fd < 0) {
      /* Probably permissions error */
      fprintf(stderr, "couldn't open %s, skipping\n", device_name);
      return -1;
   }

   gbm = gbm_create_device(fd);
   if (gbm == NULL) {
      fprintf(stderr, "couldn't create gbm device\n");
      ret = -1;
      goto close_fd;
   }

   dpy = eglGetDisplay(gbm);
   if (dpy == EGL_NO_DISPLAY) {
      fprintf(stderr, "eglGetDisplay() failed\n");
      ret = -1;
      goto destroy_gbm_device;
   }

   if (!eglInitialize(dpy, &major, &minor)) {
      printf("eglInitialize() failed\n");
      ret = -1;
      goto egl_terminate;
   }

   ver = eglQueryString(dpy, EGL_VERSION);
   printf("EGL_VERSION = %s\n", ver);

   extensions = eglQueryString(dpy, EGL_EXTENSIONS);
   printf("EGL_EXTENSIONS: %s\n", extensions);

   if (!strstr(extensions, "EGL_KHR_surfaceless_opengl")) {
      printf("No support for EGL_KHR_surfaceless_opengl\n");
      ret = -1;
      goto egl_terminate;
   }

   if (!setup_kms(fd, &kms)) {
      ret = -1;
      goto egl_terminate;
   }

   eglBindAPI(EGL_OPENGL_API);
   ctx = eglCreateContext(dpy, NULL, EGL_NO_CONTEXT, NULL);
   if (ctx == NULL) {
      fprintf(stderr, "failed to create context\n");
      ret = -1;
      goto egl_terminate;
   }

   if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
      fprintf(stderr, "failed to make context current\n");
      ret = -1;
      goto destroy_context;
   }

#ifdef GL_OES_EGL_image
   glEGLImageTargetRenderbufferStorageOES_func =
      (PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)
      eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
#else
   fprintf(stderr, "GL_OES_EGL_image not supported at compile time\n");
#endif

   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);

   glGenRenderbuffers(2, color_rb);
   for (i = 0; i < 2; i++) {

     glBindRenderbuffer(GL_RENDERBUFFER, color_rb[i]);

     bo[i]  = gbm_bo_create(gbm, kms.mode.hdisplay, kms.mode.vdisplay,
			    GBM_BO_FORMAT_XRGB8888,
			    GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
     if (bo[i] == NULL) {
       fprintf(stderr, "failed to create gbm bo\n");
       ret = -1;
       goto unmake_current;
     }
     handle = gbm_bo_get_handle(bo[i]).u32;
     stride = gbm_bo_get_pitch(bo[i]);

     image[i] = eglCreateImageKHR(dpy, NULL, EGL_NATIVE_PIXMAP_KHR,
				  bo[i], NULL);
     if (image[i] == EGL_NO_IMAGE_KHR) {
       fprintf(stderr, "failed to create egl image\n");
       ret = -1;
       goto destroy_gbm_bo;
     }

#ifdef GL_OES_EGL_image
     glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, image[i]);
#else
     fprintf(stderr, "GL_OES_EGL_image was not found at compile time\n");
#endif

     ret = drmModeAddFB(fd,
			kms.mode.hdisplay, kms.mode.vdisplay,
			24, 32, stride, handle, &kms.fb_id[i]);
     if (ret) {
       fprintf(stderr, "failed to create fb\n");
       goto rm_rb;
     }
   }

   saved_crtc = drmModeGetCrtc(fd, kms.encoder->crtc_id);
   if (saved_crtc == NULL)
      goto rm_fb;

   time(&start);
   do {
     drmEventContext evctx;
     fd_set rfds;

     glFramebufferRenderbuffer(GL_FRAMEBUFFER,
			       GL_COLOR_ATTACHMENT0,
			       GL_RENDERBUFFER,
			       color_rb[current]);

     if ((ret = glCheckFramebufferStatus(GL_FRAMEBUFFER)) !=
	 GL_FRAMEBUFFER_COMPLETE) {
       fprintf(stderr, "framebuffer not complete: %x\n", ret);
       ret = 1;
       goto rm_rb;
     }

     render_stuff(kms.mode.hdisplay, kms.mode.vdisplay);

     ret = drmModePageFlip(fd, kms.encoder->crtc_id,
			   kms.fb_id[current],
			   DRM_MODE_PAGE_FLIP_EVENT, 0);
     if (ret) {
       fprintf(stderr, "failed to page flip: %m\n");
       goto free_saved_crtc;
     }

     FD_ZERO(&rfds);
     FD_SET(fd, &rfds);

     while (select(fd + 1, &rfds, NULL, NULL, NULL) == -1)
       NULL;

     memset(&evctx, 0, sizeof evctx);
     evctx.version = DRM_EVENT_CONTEXT_VERSION;
     evctx.page_flip_handler = page_flip_handler;

     drmHandleEvent(fd, &evctx);

     current ^= 1;
     frames++;
   } while (!quit);
   time(&end);

   printf("Frames per second: %.2lf\n", frames / difftime(end, start));

   ret = drmModeSetCrtc(fd, saved_crtc->crtc_id, saved_crtc->buffer_id,
                        saved_crtc->x, saved_crtc->y,
                        &kms.connector->connector_id, 1, &saved_crtc->mode);
   if (ret) {
      fprintf(stderr, "failed to restore crtc: %m\n");
   }

free_saved_crtc:
   drmModeFreeCrtc(saved_crtc);
rm_rb:
   glFramebufferRenderbuffer(GL_FRAMEBUFFER,
			     GL_COLOR_ATTACHMENT0,
			     GL_RENDERBUFFER, 0);
   glBindRenderbuffer(GL_RENDERBUFFER, 0);
   glDeleteRenderbuffers(2, color_rb);
rm_fb:
   for (i = 0; i < 2; i++) {
     drmModeRmFB(fd, kms.fb_id[i]);
     eglDestroyImageKHR(dpy, image[i]);
   }
destroy_gbm_bo:
   for (i = 0; i < 2; i++)
     gbm_bo_destroy(bo[i]);
unmake_current:
   eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
destroy_context:
   eglDestroyContext(dpy, ctx);
egl_terminate:
   eglTerminate(dpy);
destroy_gbm_device:
   gbm_device_destroy(gbm);
close_fd:
   close(fd);

   return ret;
}
