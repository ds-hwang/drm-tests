/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EGL_INIT_H_
#define EGL_INIT_H_

#include <X11/Xlib.h>
#include <EGL/egl.h>

struct egl_state_t {
  Window x_window_;
  Pixmap x_pixmap_;
  EGLDisplay display_;
  EGLSurface surface_;
  EGLContext context_;
};

extern Display* g_x_display;

struct egl_state_t egl_create_state_window(int x, int y, int w, int h);
struct egl_state_t egl_create_state_window_shared(EGLContext share_context,
                                                  int x, int y, int w, int h);
struct egl_state_t egl_create_state_pixmap(int w, int h);
struct egl_state_t egl_create_state_pbuffer(int w, int h);
struct egl_state_t egl_create_state_pbuffer_shared(EGLContext share_context,
                                                   int w, int h);
void egl_destroy_state(struct egl_state_t* state);

#endif // EGL_INIT_H_
