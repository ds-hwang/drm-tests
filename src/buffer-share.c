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

#define _GNU_SOURCE /* Needed for O_CLOEXEC */

#include <stdio.h>
#include <stdlib.h>

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES

#include <sys/types.h>
#include <sys/stat.h>

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

struct kms {
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   drmModeModeInfo mode;
};

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
render_to_texture(int width, int height)
{
   GLfloat rsize = 100;
   GLfloat x = (width - rsize) / 2.0 ;
   GLfloat y = (height - rsize) / 2.0;

   glViewport(0, 0, (GLint) width, (GLint) height);

   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();

   glOrtho(0, width, 0, height, 1.0, -1.0);

   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();

   glClearColor(0.0f, 1.0f, 0.0f, 0.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glColor3f(1.0f, 0.0f, 0.0f);

   glRectf(x, y, x + rsize, y + rsize);

   glFinish();
}

static void
render_from_texture(int width, int height)
{
   GLfloat x = width / 2.0;
   GLfloat y = height / 2.0;

   GLfloat Vertices[] =
   {
      0.0f, y, 0.0f,
      0.0f, 0.0f, 0.0f,
      x, 0.0f, 0.0f,
      x, y, 0.0f
   };
   GLfloat TexCoords[] =
   {
      0.0f, 1.0f,
      0.0f, 0.0f,
      1.0f, 0.0f,
      1.0f, 1.0f
   };

   glViewport(0, 0, (GLint) width, (GLint) height);

   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();

   glOrtho(0, width, 0, height, 1.0, -1.0);

   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();

   glClear(GL_COLOR_BUFFER_BIT);

   glEnableClientState(GL_VERTEX_ARRAY);
   glEnableClientState(GL_TEXTURE_COORD_ARRAY);
   glVertexPointer(3, GL_FLOAT, 0, Vertices);
   glTexCoordPointer(2, GL_FLOAT, 0, TexCoords);
   glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
   glDisableClientState(GL_VERTEX_ARRAY);
   glDisableClientState(GL_TEXTURE_COORD_ARRAY);

   glFinish();
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
  printf("Quitting!\n");
}

int main(int argc, char *argv[])
{
   EGLDisplay dpy;
   EGLContext ctx;
   EGLImageKHR image, out_image;
   EGLint major, minor;
   const char *extensions;
   GLuint fb, texture, out_texture;
   uint32_t handle, stride, fb_id, name;
   struct kms kms;
   int ret, fd;
   struct gbm_device *gbm;
   struct gbm_bo *bo;
   drmModeCrtcPtr saved_crtc;
   time_t start, end;
   int is_producer = (argc == 1);

   signal (SIGINT, quit_handler);

   fd = open(device_name, O_RDWR | O_CLOEXEC);
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

   extensions = eglQueryString(dpy, EGL_EXTENSIONS);
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

   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);

   glGenTextures(1, &out_texture);
   glBindTexture(GL_TEXTURE_2D, out_texture);
   glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

   bo  = gbm_bo_create(gbm, kms.mode.hdisplay, kms.mode.vdisplay,
                       GBM_BO_FORMAT_XRGB8888,
                       GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
   if (bo == NULL) {
      fprintf(stderr, "failed to create gbm bo\n");
      ret = -1;
      goto unmake_current;
   }

   out_image = eglCreateImageKHR(dpy, NULL, EGL_NATIVE_PIXMAP_KHR, bo, NULL);
   if (out_image == EGL_NO_IMAGE_KHR) {
      fprintf(stderr, "failed to create egl image\n");
      ret = -1;
      goto destroy_gbm_bo;
   }

   glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, out_image);

   glFramebufferTexture2D(GL_FRAMEBUFFER,
                          GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D,
                          out_texture, 0);

   if ((ret = glCheckFramebufferStatus(GL_FRAMEBUFFER)) !=
       GL_FRAMEBUFFER_COMPLETE) {
      fprintf(stderr, "framebuffer not complete: %x\n", ret);
      ret = 1;
      goto destroy_gbm_bo;
   }

   if (is_producer) {
      char name_str[sizeof(long) + 1] = { 0 };
      char stride_str[sizeof(long) + 1] = { 0 };

      eglExportDRMImageMESA(dpy, out_image, &name, NULL, &stride);

      snprintf(name_str, sizeof(name_str), "%d", (int)name);
      snprintf(stride_str, sizeof(stride_str), "%d", (int)stride);

      render_to_texture(kms.mode.hdisplay, kms.mode.vdisplay);

      drmDropMaster(fd);

      if (!fork())
         execl(argv[0], argv[0], name_str, stride_str, NULL);
       else
         wait(NULL);

      goto destroy_gbm_bo;
   }

   sscanf(argv[1], "%d", &name);
   sscanf(argv[2], "%d", &stride);

   EGLint attribs[] = {
      EGL_WIDTH, kms.mode.hdisplay,
      EGL_HEIGHT, kms.mode.vdisplay,
      EGL_DRM_BUFFER_STRIDE_MESA, stride / 4,
      EGL_DRM_BUFFER_FORMAT_MESA, EGL_DRM_BUFFER_FORMAT_ARGB32_MESA,
      EGL_NONE
   };

   image = eglCreateImageKHR(dpy,
                             EGL_NO_CONTEXT,
                             EGL_DRM_BUFFER_MESA,
                             (void *)(intptr_t)(name),
                             attribs);

   if (image == EGL_NO_IMAGE_KHR) {
      fprintf(stderr, "Failed to import image\n");
      return;
   }

   glEnable(GL_TEXTURE_2D);

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);

   eglExportDRMImageMESA(dpy, out_image, NULL, &handle, &stride);

   render_from_texture(kms.mode.hdisplay, kms.mode.vdisplay);

   ret = drmModeAddFB(fd,
                      kms.mode.hdisplay, kms.mode.vdisplay,
                      24, 32, stride, handle, &fb_id);
   if (ret) {
      fprintf(stderr, "failed to create fb\n");
      goto destroy_gbm_bo;
   }

   saved_crtc = drmModeGetCrtc(fd, kms.encoder->crtc_id);
   if (saved_crtc == NULL)
      goto rm_fb;

   drmEventContext evctx;
   fd_set rfds;

   ret = drmModePageFlip(fd, kms.encoder->crtc_id,
                         fb_id, DRM_MODE_PAGE_FLIP_EVENT, 0);
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

   sleep(60);

   ret = drmModeSetCrtc(fd, saved_crtc->crtc_id, saved_crtc->buffer_id,
                        saved_crtc->x, saved_crtc->y,
                        &kms.connector->connector_id, 1, &saved_crtc->mode);
   if (ret) {
      fprintf(stderr, "failed to restore crtc: %m\n");
   }

free_saved_crtc:
   drmModeFreeCrtc(saved_crtc);
rm_fb:
   drmModeRmFB(fd, fb_id);
   eglDestroyImageKHR(dpy, image);
destroy_gbm_bo:
   gbm_bo_destroy(bo);
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
