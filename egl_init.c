/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdio.h>
#include <string.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <EGL/egl.h>
#include <GL/gl.h>

#include "egl_init.h"

Display* g_x_display = NULL;

struct egl_state_t egl_create_state_window(int x, int y, int w, int h) {
  return egl_create_state_window_shared(EGL_NO_CONTEXT, x, y, w, h);
}

struct egl_state_t egl_create_state_window_shared(EGLContext share_context,
                                                  int x, int y, int w, int h) {
  struct egl_state_t retval;
  retval.x_window_ = 0;
  retval.x_pixmap_ = 0;
  retval.display_ = EGL_NO_DISPLAY;
  retval.surface_ = EGL_NO_SURFACE;
  retval.context_ = EGL_NO_CONTEXT;

  if (g_x_display == NULL) {
    fprintf(stdout, "Using display: %s\n", XDisplayName(NULL));
    g_x_display = XOpenDisplay(NULL);
    if (g_x_display == NULL)
    {
      fprintf(stderr, "XOpenDisplay() failed.\n");
      goto error;
    }
  }

  Window x_root_window = DefaultRootWindow(g_x_display);

  XSetWindowAttributes x_swa;
  memset(&x_swa, 0, sizeof(x_swa));
  x_swa.background_pixmap = None;
  retval.x_window_ = XCreateWindow(g_x_display, x_root_window, x, y, w, h, 0,
                                   CopyFromParent, InputOutput, CopyFromParent,
                                   CWBackPixmap, &x_swa);
  if (retval.x_window_ == 0) {
    fprintf(stderr, "XCreateWindow() failed.\n");
    goto error;
  }

  XMapWindow(g_x_display, retval.x_window_);

  retval.display_ = eglGetDisplay(g_x_display);
  if (retval.display_ == EGL_NO_DISPLAY) {
    fprintf(stderr, "eglGetDisplay() failed.\n");
    goto error;
  }

  if (!eglInitialize(retval.display_, NULL, NULL)) {
    fprintf(stderr, "eglInitialize() failed with error: %x\n", eglGetError());
    goto error;
  }

  static const EGLint config_attribs[] = {
    EGL_BUFFER_SIZE, 24,
    EGL_BLUE_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_RED_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
    EGL_NONE
  };

  EGLint num_configs;
  if (!eglChooseConfig(retval.display_, config_attribs, NULL, 0,
                       &num_configs)) {
    fprintf(stderr, "eglChooseConfig() failed with error: %x\n", eglGetError());
    goto error;
  }

  EGLConfig egl_config;
  if (!eglChooseConfig(retval.display_, config_attribs, &egl_config, 1,
      &num_configs)) {
    fprintf(stderr, "eglChooseConfig() failed with error: %x\n", eglGetError());
    goto error;
  }

  retval.surface_ = eglCreateWindowSurface(retval.display_, egl_config,
                                           retval.x_window_, NULL);
  if (retval.surface_ == EGL_NO_SURFACE) {
    fprintf(stderr, "eglCreateWindowSurface() failed with error: %x\n",
            eglGetError());
    goto error;
  }

  static const EGLint context_attributes[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };
  retval.context_ = eglCreateContext(retval.display_, egl_config,
                                     share_context, context_attributes);
  if (retval.context_ == EGL_NO_CONTEXT) {
    fprintf(stderr, "eglCreateContext() failed with error: %x\n",
            eglGetError());
    goto error;
  }

  return retval;

error:
  egl_destroy_state(&retval);
  return retval;
}

struct egl_state_t egl_create_state_pixmap(int w, int h) {
  struct egl_state_t retval;
  retval.x_window_ = 0;
  retval.x_pixmap_ = 0;
  retval.display_ = EGL_NO_DISPLAY;
  retval.surface_ = EGL_NO_SURFACE;
  retval.context_ = EGL_NO_CONTEXT;

  if (g_x_display == NULL) {
    fprintf(stdout, "Using display: %s\n", XDisplayName(NULL));
    g_x_display = XOpenDisplay(NULL);
    if (g_x_display == NULL)
    {
      fprintf(stderr, "XOpenDisplay() failed.\n");
      goto error;
    }
  }

  {
    Window x_root_window = DefaultRootWindow(g_x_display);
    Window root;
    int x, y;
    unsigned int w, h, b, d;
    XGetGeometry(g_x_display, x_root_window, &root, &x, &y, &w, &h, &b, &d);
    retval.x_pixmap_ = XCreatePixmap(g_x_display, x_root_window, w, h, d);
  }
  if (retval.x_pixmap_ == 0) {
    fprintf(stderr, "XCreatePixmap() failed.\n");
    goto error;
  }

  retval.display_ = eglGetDisplay(g_x_display);
  if (retval.display_ == EGL_NO_DISPLAY) {
    fprintf(stderr, "eglGetDisplay() failed.\n");
    goto error;
  }

  if (!eglInitialize(retval.display_, NULL, NULL)) {
    fprintf(stderr, "eglInitialize() failed with error: %x\n", eglGetError());
    goto error;
  }

  static const EGLint config_attribs[] = {
    EGL_BUFFER_SIZE, 24,
    EGL_BLUE_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_RED_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
    EGL_NONE
  };

  EGLint num_configs;
  if (!eglChooseConfig(retval.display_, config_attribs, NULL, 0,
                       &num_configs)) {
    fprintf(stderr, "eglChooseConfig() failed with error: %x\n",
            eglGetError());
    goto error;
  }

  EGLConfig egl_config;
  if (!eglChooseConfig(retval.display_, config_attribs, &egl_config, 1,
      &num_configs)) {
    fprintf(stderr, "eglChooseConfig() failed with error: %x\n",
            eglGetError());
    goto error;
  }

  retval.surface_ = eglCreatePixmapSurface(retval.display_, egl_config,
                                           retval.x_pixmap_, NULL);
  if (retval.surface_ == EGL_NO_SURFACE) {
    fprintf(stderr, "eglCreatePixmapSurface() failed with error: %x\n",
            eglGetError());
    goto error;
  }

  static const EGLint context_attributes[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };
  retval.context_ = eglCreateContext(retval.display_, egl_config,
                                     EGL_NO_CONTEXT, context_attributes);
  if (retval.context_ == EGL_NO_CONTEXT) {
    fprintf(stderr, "eglCreateContext() failed with error: %x\n",
            eglGetError());
    goto error;
  }

  return retval;

error:
  egl_destroy_state(&retval);
  return retval;
}

struct egl_state_t egl_create_state_pbuffer(int w, int h) {
  return egl_create_state_pbuffer_shared(EGL_NO_CONTEXT, w, h);
}

struct egl_state_t egl_create_state_pbuffer_shared(EGLContext share_context,
                                                   int w, int h) {
  struct egl_state_t retval;
  retval.x_window_ = 0;
  retval.x_pixmap_ = 0;
  retval.display_ = EGL_NO_DISPLAY;
  retval.surface_ = EGL_NO_SURFACE;
  retval.context_ = EGL_NO_CONTEXT;

  if (g_x_display == NULL) {
    fprintf(stdout, "Using display: %s\n", XDisplayName(NULL));
    g_x_display = XOpenDisplay(NULL);
    if (g_x_display == NULL)
    {
      fprintf(stderr, "XOpenDisplay() failed.\n");
      goto error;
    }
  }

  retval.display_ = eglGetDisplay(g_x_display);
  if (retval.display_ == EGL_NO_DISPLAY) {
    fprintf(stderr, "eglGetDisplay() failed.\n");
    goto error;
  }

  if (!eglInitialize(retval.display_, NULL, NULL)) {
    fprintf(stderr, "eglInitialize() failed with error: %x\n", eglGetError());
    goto error;
  }

  static const EGLint config_attribs[] = {
    EGL_BUFFER_SIZE, 24,
    EGL_BLUE_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_RED_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
    EGL_NONE
  };

  EGLint num_configs;
  if (!eglChooseConfig(retval.display_, config_attribs, NULL, 0,
                       &num_configs)) {
    fprintf(stderr, "eglChooseConfig() failed with error: %x\n",
            eglGetError());
    goto error;
  }

  EGLConfig egl_config;
  if (!eglChooseConfig(retval.display_, config_attribs, &egl_config, 1,
      &num_configs)) {
    fprintf(stderr, "eglChooseConfig() failed with error: %x\n",
            eglGetError());
    goto error;
  }

  EGLint pbuffer_attribs[] = {
    EGL_WIDTH, w,
    EGL_HEIGHT, h,
    EGL_NONE,
  };
  retval.surface_ = eglCreatePbufferSurface(retval.display_, egl_config,
                                            pbuffer_attribs);
  if (retval.surface_ == EGL_NO_SURFACE) {
    fprintf(stderr, "eglCreatePixmapSurface() failed with error: %x\n",
            eglGetError());
    goto error;
  }

  static const EGLint context_attributes[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };
  retval.context_ = eglCreateContext(retval.display_, egl_config,
                                     share_context, context_attributes);
  if (retval.context_ == EGL_NO_CONTEXT) {
    fprintf(stderr, "eglCreateContext() failed with error: %x\n",
            eglGetError());
    goto error;
  }

  return retval;

error:
  egl_destroy_state(&retval);
  return retval;
}

void egl_destroy_state(struct egl_state_t* state) {
  if (state->context_ != EGL_NO_CONTEXT) {
    eglDestroyContext(state->display_, state->context_);
    state->context_ = EGL_NO_CONTEXT;
  }
  if (state->surface_ != EGL_NO_SURFACE) {
    eglDestroySurface(state->display_, state->surface_);
    state->surface_ = EGL_NO_SURFACE;
  }
  if (state->display_ != EGL_NO_DISPLAY) {
    state->display_ = EGL_NO_DISPLAY;
  }
  if (state->x_pixmap_ != 0) {
    XFreePixmap(g_x_display, state->x_pixmap_);
    state->x_pixmap_ = 0;
  }
  if (state->x_window_ != 0) {
    XDestroyWindow(g_x_display, state->x_window_);
    state->x_window_ = 0;
  }
}
