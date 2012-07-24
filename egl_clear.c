/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <X11/cursorfont.h>
#include <EGL/egl.h>
#include <GL/gl.h>

#include "egl_init.h"

static int xpos = 0;
static int ypos = 0;
static int width = 512;
static int height = 512;

int main(int argc, char** argv) {
  int i;
  for (i = 1; i < argc; i += 1) {
    if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--xpos") == 0) {
      if (i + 1 < argc) {
        xpos = atoi(argv[i + 1]);
        i += 1;
      }
      else {
        fprintf(stderr, "main(): invalid arguments.\n");
        return -1;
      }
    } else if (strcmp(argv[i], "-y") == 0 || strcmp(argv[i], "--ypos") == 0) {
      if (i + 1 < argc) {
        ypos = atoi(argv[i + 1]);
        i += 1;
      }
      else {
        fprintf(stderr, "main(): invalid arguments.\n");
        return -1;
      }
    } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--width") == 0) {
      if (i + 1 < argc) {
        width = atoi(argv[i + 1]);
        i += 1;
      }
      else {
        fprintf(stderr, "main(): invalid arguments.\n");
        return -1;
      }
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--height") == 0) {
      if (i + 1 < argc) {
        height = atoi(argv[i + 1]);
        i += 1;
      }
      else {
        fprintf(stderr, "main(): invalid arguments.\n");
        return -1;
      }
    }
    else {
      fprintf(stderr, "main(): invalid arguments.\n");
      return -1;
    }
  }

  fprintf(stdout, "main(): xpos=%d, ypos=%d, width=%d, height=%d\n",
          xpos, ypos, width, height);

  struct egl_state_t egl_state = egl_create_state_window(xpos, ypos,
                                                         width, height);
  if (egl_state.context_ == EGL_NO_CONTEXT) {
    return -1;
  }

  if (!eglMakeCurrent(egl_state.display_, egl_state.surface_,
                      egl_state.surface_, egl_state.context_)) {
    fprintf(stderr, "eglMakeCurrent() failed with error: %x\n", eglGetError());
    return -1;
  }

  if (!eglSwapInterval(egl_state.display_, 1)) {
    fprintf(stderr, "eglSwapInterval() failed.\n");
    return -1;
  }

  GLenum gl_error = GL_NO_ERROR;

  struct timeval time;
  uint64_t msecs, msecs_delta;
  gettimeofday(&time, NULL);
  msecs = (time.tv_sec * 1000) + (time.tv_usec / 1000);
  msecs_delta = 0;

  while (1) {
    int i = (msecs_delta / 1000) % 4;
    static const GLclampf color_wheel[4][4] = {
      {  51.0 / 255.0, 105.0 / 255.0, 232.0 / 255.0, 1.0 },
      { 213.0 / 255.0,  15.0 / 255.0,  37.0 / 255.0, 1.0 },
      { 238.0 / 255.0, 178.0 / 255.0,  17.0 / 255.0, 1.0 },
      {   0.0 / 255.0, 153.0 / 255.0,  37.0 / 255.0, 1.0 }
    };
    glClearColor(color_wheel[i][0], color_wheel[i][1],
                 color_wheel[i][2], color_wheel[i][3]);
    if ((gl_error = glGetError()) != GL_NO_ERROR) {
      fprintf(stderr, "glClearColor() failed with error: %x\n", gl_error);
      return -1;
    }

    glClear(GL_COLOR_BUFFER_BIT);
    if ((gl_error = glGetError()) != GL_NO_ERROR) {
      fprintf(stderr, "glClearColor() failed with error: %x\n", gl_error);
      return -1;
    }

    glFlush();

    if (!eglSwapBuffers(egl_state.display_, egl_state.surface_)) {
      fprintf(stderr, "eglSwapBuffers() failed with error: %x\n",
              eglGetError());
      return -1;
    }

    gettimeofday(&time, NULL);
    uint64_t msecs_new = (time.tv_sec * 1000) + (time.tv_usec / 1000);
    msecs_delta = msecs_new - msecs;
  }

  egl_destroy_state(&egl_state);
  return 0;
}

