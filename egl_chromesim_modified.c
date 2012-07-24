/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>

#include "egl_init.h"

#define countof(x) (sizeof(x) / sizeof(*x))

static const GLclampf color_wheel[4][4] = {
  {  51.0 / 255.0, 105.0 / 255.0, 232.0 / 255.0, 1.0 },
  { 213.0 / 255.0,  15.0 / 255.0,  37.0 / 255.0, 1.0 },
  { 238.0 / 255.0, 178.0 / 255.0,  17.0 / 255.0, 1.0 },
  {   0.0 / 255.0, 153.0 / 255.0,  37.0 / 255.0, 1.0 }
};

struct producer_t {
  struct egl_state_t egl_state_;
  GLuint gl_textures_[2];
  int texture_index_;
  GLint gl_uniform_vRotation_;
  GLint gl_uniform_fScale_;
  GLint gl_uniform_vTranslation_;
  GLint gl_uniform_vColor_;
  uint64_t last_frame_;
};

struct consumer_t {
  struct egl_state_t egl_state_;
};

GLuint create_shader(const GLchar* shader_text, GLenum shader_type);

struct producer_t create_producer(EGLContext share_context,
                                  int width, int height);
void destroy_producer(struct producer_t* producer);
GLint run_producer(struct producer_t* producer);

struct consumer_t create_consumer(int xpos, int ypos, int width, int height);
void destroy_consumer(struct consumer_t* consumer);
int run_consumer(struct consumer_t* consumer, GLint texture);

static int xpos = 0;
static int ypos = 0;
static int width = 512;
static int height = 512;

int main(int argc, char** argv) {
  int i;
  /*
   * Why is it that every project I ever write eventually comes with a
   * text parser of some kind? ;-)
   */
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

  int retval = 0;
  struct producer_t producer;
  memset(&producer, 0, sizeof(producer));
  struct consumer_t consumer;
  memset(&consumer, 0, sizeof(&consumer));

  consumer = create_consumer(xpos, ypos, width, height);
  if (consumer.egl_state_.context_ == EGL_NO_CONTEXT) {
    retval = -1;
    goto exit;
  }

  producer = create_producer(consumer.egl_state_.context_, width, height);
  if (producer.egl_state_.context_ == EGL_NO_CONTEXT) {
    retval = -1;
    goto exit;
  }

  /*
   * Render the first frontbuffer
   */
  GLuint last_texture = run_producer(&producer);
  if (last_texture == 0) {
    retval = 1;
    goto exit;
  }

  int count = 0;
  while (count < 3) {
    /*
     * Render the backbuffer
     */
    GLuint texture = run_producer(&producer);
    if (texture == 0) {
      retval = 1;
      goto exit;
    }
    /*
     * Present the frontbuffer
     */
    int runval = run_consumer(&consumer, last_texture);
    if (runval != 0) {
      retval = 2;
      goto exit;
    }
    last_texture = texture;
    count += 1;
  }

exit:
  destroy_consumer(&consumer);
  destroy_producer(&producer);
  return retval;
}

struct producer_t create_producer(EGLContext share_context,
    int width, int height) {
  struct producer_t producer;
  memset(&producer, 0, sizeof(producer));
  GLenum gl_error = GL_NO_ERROR;
  GLuint gl_framebuffer = 0;
  GLuint gl_program = 0;
  GLuint gl_shader = 0;
  struct timeval time;
  int i;

  producer.egl_state_ = egl_create_state_pbuffer_shared(share_context, 16, 16);
  if (producer.egl_state_.context_ == EGL_NO_CONTEXT) {
    goto error;
  }

  if (!eglMakeCurrent(producer.egl_state_.display_,
                      producer.egl_state_.surface_,
                      producer.egl_state_.surface_,
                      producer.egl_state_.context_)) {
    fprintf(stderr, "eglMakeCurrent() failed with error: %x\n", eglGetError());
    goto error;
  }

  glViewport(0, 0, width, height);

  glGenTextures(countof(producer.gl_textures_), producer.gl_textures_);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glGenTextures() failed with error: %x\n", gl_error);
    goto error;
  }

  for (i = 0; i < countof(producer.gl_textures_); i += 1) {
    glBindTexture(GL_TEXTURE_2D, producer.gl_textures_[i]);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, 0);
    if ((gl_error = glGetError()) != GL_NO_ERROR) {
      fprintf(stderr, "glTexImage2D() failed with error: %x\n",
              gl_error);
      goto error;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if ((gl_error = glGetError()) != GL_NO_ERROR) {
      fprintf(stderr, "OpenGL failure setting up texture with error: %x\n",
              gl_error);
      goto error;
    }
  }

  glGenFramebuffers(1, &gl_framebuffer);
  if (gl_framebuffer == 0) {
    fprintf(stderr, "glGenFramebuffers() failed with error: %x\n",
            glGetError());
    goto error;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, gl_framebuffer);

  gl_program = glCreateProgram();
  if (gl_program == 0) {
    fprintf(stderr, "glCreateProgram(): failed with error: %x\n",
            glGetError());
    goto error;
  }
  if (!glIsProgram(gl_program)) {
    fprintf(stderr, "gl_program is not a program.\n");
    goto error;
  }

  /*
   * Rotating the vertices -- we want some movement so we can see jerkiness
   */
  static const GLchar vertex_shader[] =
    "attribute vec4 vPosition;\n"
    "uniform vec2 vRotation;\n"
    "uniform float fScale;\n"
    "uniform vec2 vTranslation;\n"
    "void main() {\n"
    "\tvec4 pos;\n"
    "\tpos[0] = vRotation[0] * vPosition[0] + vRotation[1] * vPosition[1];\n"
    "\tpos[1] = vRotation[0] * vPosition[1] - vRotation[1] * vPosition[0];\n"
    "\tpos[2] = vPosition[2];\n"
    "\tpos[3] = vPosition[3];\n"
    "\tpos[0] = pos[0] * fScale + vTranslation[0];\n"
    "\tpos[1] = pos[1] * fScale + vTranslation[1];\n"
    "\tgl_Position = pos;\n"
    "\tgl_PointSize = 4.0;\n"
    "}\n";
  gl_shader = create_shader(vertex_shader, GL_VERTEX_SHADER);
  if (gl_shader == 0) {
    goto error;
  }
  if (!glIsShader(gl_shader)) {
    fprintf(stderr, "gl_shader is not a shader.\n");
    goto error;
  }
  glAttachShader(gl_program, gl_shader);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glAttachShader() failed with error: %x\n",
            gl_error);
    goto error;
  }
  glDeleteShader(gl_shader);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glDeleteShader() failed with error: %x\n",
            gl_error);
    goto error;
  }
  gl_shader = 0;

  /*
   * Solid-color fragment shader
   */
  const GLchar fragment_shader[] =
    "uniform lowp vec4 vColor;\n"
    "void main() {\n"
    "\tgl_FragColor = vColor;\n"
    "}\n";
  gl_shader = create_shader(fragment_shader, GL_FRAGMENT_SHADER);
  if (gl_shader == 0) {
    goto error;
  }
  if (!glIsShader(gl_shader)) {
    fprintf(stderr, "gl_shader is not a shader.\n");
    goto error;
  }
  glAttachShader(gl_program, gl_shader);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glAttachShader() failed with error: %x\n",
            gl_error);
    goto error;
  }
  glDeleteShader(gl_shader);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glDeleteShader() failed with error: %x\n",
            gl_error);
    goto error;
  }
  gl_shader = 0;

  glLinkProgram(gl_program);
  GLint param;
  glGetProgramiv(gl_program, GL_LINK_STATUS, &param);
  if (param != GL_TRUE) {
    GLchar error[1024];
    glGetProgramInfoLog(gl_program, sizeof(error), NULL, error);
    fprintf(stderr, "glLinkProgram() failed with error: %s\n",
            error);
    goto error;
  }
  glUseProgram(gl_program);

  producer.gl_uniform_vRotation_ =
    glGetUniformLocation(gl_program, "vRotation");
  if (producer.gl_uniform_vRotation_ == -1) {
    fprintf(stderr, "glGetUniformLocation() lookup failed with error: %x\n",
            glGetError());
    goto error;
  }
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glGetUniformLocation() failed with error: %x\n",
            gl_error);
    goto error;
  }

  producer.gl_uniform_fScale_ = glGetUniformLocation(gl_program, "fScale");
  if (producer.gl_uniform_fScale_ == -1) {
    fprintf(stderr, "glGetUniformLocation() lookup failed with error: %x\n",
            glGetError());
    goto error;
  }
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glGetUniformLocation() failed with error: %x\n",
            gl_error);
    goto error;
  }

  producer.gl_uniform_vTranslation_ =
    glGetUniformLocation(gl_program, "vTranslation");
  if (producer.gl_uniform_vTranslation_ == -1) {
    fprintf(stderr, "glGetUniformLocation() lookup failed with error: %x\n",
            glGetError());
    goto error;
  }
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glGetUniformLocation() failed with error: %x\n",
            gl_error);
    goto error;
  }
  producer.gl_uniform_vColor_ = glGetUniformLocation(gl_program, "vColor");
  if (producer.gl_uniform_vColor_ == -1) {
    fprintf(stderr, "glGetUniformLocation() lookup failed with error: %x\n",
            glGetError());
    goto error;
  }
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glGetUniformLocation() failed with error: %x\n",
            gl_error);
    goto error;
  }

  glEnableVertexAttribArray(0);
  glBindAttribLocation(gl_program, 0, "vPosition");

  static const GLfloat triangle_vertices[] = {
     0.0f,  0.433013f, 0.5f, 1.0f,
     0.5f, -0.433013f, 0.5f, 1.0f,
    -0.5f, -0.433013f, 0.5f, 1.0f,
  };
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE,
                        4 * sizeof(GLfloat), triangle_vertices);

  glDeleteProgram(gl_program);
  gl_program = 0;

  gettimeofday(&time, NULL);
  producer.last_frame_ = (time.tv_sec * 1000) + (time.tv_usec / 1000);

  return producer;

error:
  if (gl_shader != 0) {
    glDeleteShader(gl_shader);
    gl_shader = 0;
  }
  if (gl_program != 0) {
    glDeleteProgram(gl_program);
    gl_program = 0;
  }
  if (gl_framebuffer != 0) {
    glDeleteFramebuffers(1, &gl_framebuffer);
    gl_framebuffer = 0;
  }

  destroy_producer(&producer);
  return producer;
}

void destroy_producer(struct producer_t* producer) {
  producer->last_frame_ = 0;
  producer->gl_uniform_vColor_ = -1;
  producer->gl_uniform_vTranslation_ = -1;
  producer->gl_uniform_fScale_ = -1;
  producer->gl_uniform_vRotation_ = -1;
  producer->texture_index_ = 0;
  if (producer->gl_textures_[0] != 0) {
    int i = 0;
    glDeleteTextures(countof(producer->gl_textures_), producer->gl_textures_);
    for (i = 0; i < countof(producer->gl_textures_); i += 1) {
      producer->gl_textures_[i] = 0;
    }
  }
  egl_destroy_state(&producer->egl_state_);
}

GLint run_producer(struct producer_t* producer) {
  GLenum gl_error = GL_NO_ERROR;

  if (!eglMakeCurrent(producer->egl_state_.display_,
                      producer->egl_state_.surface_,
                      producer->egl_state_.surface_,
                      producer->egl_state_.context_)) {
    fprintf(stderr, "eglMakeCurrent() failed with error: %x\n", eglGetError());
    return 0;
  }

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         producer->gl_textures_[producer->texture_index_], 0);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glFramebufferTexture2D() failed with error: %x\n",
            gl_error);
    return 0;
  }

  gl_error = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (gl_error != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "glCheckFramebufferStatus() failed with status: %x\n",
            gl_error);
    return 0;
  }

  struct timeval time;
  uint64_t msecs_delta;
  gettimeofday(&time, NULL);
  msecs_delta = (time.tv_sec * 1000) + (time.tv_usec / 1000) -
    producer->last_frame_;

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
    return 0;
  }

  glClear(GL_COLOR_BUFFER_BIT);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glClearColor() failed with error: %x\n", gl_error);
    return 0;
  }

  static const int multiple = 32;

  const double angle = msecs_delta * (3.14159265358 / 2.0 / 1000.0);
  GLfloat triangle_rotation[2];
  triangle_rotation[0] = cos(angle);
  triangle_rotation[1] = -sin(angle);
  glUniform2fv(producer->gl_uniform_vRotation_, 1, triangle_rotation);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glUniform2fv() failed with error: %x\n", gl_error);
    return 0;
  }

  const GLfloat triangle_scale = 1.0 / multiple;
  glUniform1f(producer->gl_uniform_fScale_, triangle_scale);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glUniform1f() failed with error: %x\n", gl_error);
    return 0;
  }

  static const GLfloat triangle_color[] = {
    1.0f, 1.0f, 1.0f, 1.0f,
  };
  glUniform4fv(producer->gl_uniform_vColor_, 1, triangle_color);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glUniform4fv() failed with error: %x\n", gl_error);
    return 0;
  }

  int x, y;
  for (y = 0; y < multiple; y += 1) {
    for (x = 0; x < multiple; x += 1) {
      GLfloat triangle_translation[2];
      triangle_translation[0] = -1.0 + (1.0 + x * 2) / multiple;
      triangle_translation[1] = -1.0 + (1.0 + y * 2) / multiple;
      glUniform2fv(producer->gl_uniform_vTranslation_, 1, triangle_translation);
      if ((gl_error = glGetError()) != GL_NO_ERROR) {
        fprintf(stderr, "glUniform2fv() failed with error: %x\n", gl_error);
       return 0;
      }
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }
  }

  /*
   * Static black box, literally
   */
  const static uint32_t black_tex[] = {
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
    0xff000000, 0xff000000, 0xff000000, 0xff000000,
  };

  glBindTexture(GL_TEXTURE_2D,
                producer->gl_textures_[producer->texture_index_]);

  glTexSubImage2D(GL_TEXTURE_2D, 0, width / 2 - 4, height / 2 - 4, 8, 8,
                  GL_RGBA, GL_UNSIGNED_BYTE, black_tex);

  glFlush();

  /*
   * Return the rendered backbuffer; flip buffers
   */
  GLuint texture = producer->gl_textures_[producer->texture_index_];
  producer->texture_index_ =
    (producer->texture_index_ + 1) % countof(producer->gl_textures_);
  return texture;
}

struct consumer_t create_consumer(int xpos, int ypos, int width, int height) {
  struct consumer_t consumer;
  memset(&consumer, 0, sizeof(consumer));
  GLenum gl_error = GL_NO_ERROR;
  GLuint gl_program = 0;
  GLuint gl_shader = 0;
  GLint gl_uniform = 0;

  consumer.egl_state_ = egl_create_state_window(xpos, ypos, width, height);
  if (consumer.egl_state_.context_ == EGL_NO_CONTEXT) {
    goto error;
  }

  if (!eglMakeCurrent(consumer.egl_state_.display_,
                      consumer.egl_state_.surface_,
                      consumer.egl_state_.surface_,
                      consumer.egl_state_.context_)) {
    fprintf(stderr, "eglMakeCurrent() failed with error: %x\n", eglGetError());
    goto error;
  }

  if (!eglSwapInterval(consumer.egl_state_.display_, 1)) {
    fprintf(stderr, "eglSwapInterval() failed.\n");
    goto error;
  }

  gl_program = glCreateProgram();
  if (gl_program == 0) {
    fprintf(stderr, "glCreateProgram(): failed with error: %x\n",
            glGetError());
    goto error;
  }
  if (!glIsProgram(gl_program)) {
    fprintf(stderr, "gl_program is not a program.\n");
    goto error;
  }

  /*
   * Passthrough vertex shader
   */
  static const GLchar vertex_shader[] =
    "attribute vec4 vPosition;\n"
    "attribute vec2 vTexcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "\tgl_Position = vPosition;\n"
    "\tv_texcoord = vTexcoord;\n"
    "}\n";
  gl_shader = create_shader(vertex_shader, GL_VERTEX_SHADER);
  if (gl_shader == 0) {
    goto error;
  }
  if (!glIsShader(gl_shader)) {
    fprintf(stderr, "gl_shader is not a shader.\n");
    goto error;
  }
  glAttachShader(gl_program, gl_shader);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glAttachShader() failed with error: %x\n",
            gl_error);
    goto error;
  }
  glDeleteShader(gl_shader);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glDeleteShader() failed with error: %x\n",
            gl_error);
    goto error;
  }
  gl_shader = 0;

  /*
   * Texture-mapped quad
   */
  static const GLchar fragment_shader[] =
    "varying highp vec2 v_texcoord;\n"
    "uniform sampler2D s_sampler;\n"
    "void main() {\n"
    "\tgl_FragColor = texture2D(s_sampler, v_texcoord);\n"
    "}\n";
  gl_shader = create_shader(fragment_shader, GL_FRAGMENT_SHADER);
  if (gl_shader == 0) {
    goto error;
  }
  if (!glIsShader(gl_shader)) {
    fprintf(stderr, "gl_shader is not a shader.\n");
    goto error;
  }
  glAttachShader(gl_program, gl_shader);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glAttachShader() failed with error: %x\n",
            gl_error);
    goto error;
  }
  glDeleteShader(gl_shader);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glDeleteShader() failed with error: %x\n",
            gl_error);
    goto error;
  }
  gl_shader = 0;

  glLinkProgram(gl_program);
  GLint param;
  glGetProgramiv(gl_program, GL_LINK_STATUS, &param);
  if (param != GL_TRUE) {
    GLchar error[1024];
    glGetProgramInfoLog(gl_program, sizeof(error), NULL, error);
    fprintf(stderr, "glLinkProgram() failed with error: %s\n",
            error);
    goto error;
  }
  glUseProgram(gl_program);

  static const GLfloat vertices[] = {
     -0.75f,  0.75f, 0.5f, 1.0f,
      0.75f,  0.75f, 0.5f, 1.0f,
     -0.75f, -0.75f, 0.5f, 1.0f,
      0.75f, -0.75f, 0.5f, 1.0f,
  };
  glEnableVertexAttribArray(0);
  glBindAttribLocation(gl_program, 0, "vPosition");
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                        vertices);

  static const GLfloat texcoords[] = {
    0.0f, 1.0f,
    1.0f, 1.0f,
    0.0f, 0.0f,
    1.0f, 0.0f,
  };
  glEnableVertexAttribArray(1);
  glBindAttribLocation(gl_program, 1, "vTexcoord");
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat),
                        texcoords);

  gl_uniform = glGetUniformLocation(gl_program, "s_sampler");
  if (gl_uniform == -1) {
    fprintf(stderr, "glGetUniformLocation() failed with error: %x\n",
            glGetError());
    goto error;
  }

  glDeleteProgram(gl_program);
  gl_program = 0;

  glActiveTexture(GL_TEXTURE0 + 0);
  glUniform1i(gl_uniform, 0);

  return consumer;

error:
  gl_uniform = 0;
  if (gl_shader != 0) {
    glDeleteShader(gl_shader);
    gl_shader = 0;
  }
  if (gl_program != 0) {
    glDeleteProgram(gl_program);
    gl_program = 0;
  }

  destroy_consumer(&consumer);
  return consumer;
}

void destroy_consumer(struct consumer_t* consumer) {
  egl_destroy_state(&consumer->egl_state_);
}

int run_consumer(struct consumer_t* consumer, GLint texture) {
  GLenum gl_error = GL_NO_ERROR;

  if (!eglMakeCurrent(consumer->egl_state_.display_,
                      consumer->egl_state_.surface_,
                      consumer->egl_state_.surface_,
                      consumer->egl_state_.context_)) {
    fprintf(stderr, "eglMakeCurrent() failed with error: %x\n", eglGetError());
    return 1;
  }

  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "OpenGL failure setting up texture with error: %x\n",
            gl_error);
    return 1;
  }

  glClearColor(1.0, 1.0, 1.0, 1.0);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glClearColor() failed with error: %x\n", gl_error);
    return 1;
  }

  glClear(GL_COLOR_BUFFER_BIT);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glClearColor() failed with error: %x\n", gl_error);
    return 1;
  }

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  glFlush();

  if (!eglSwapBuffers(consumer->egl_state_.display_,
                      consumer->egl_state_.surface_)) {
    fprintf(stderr, "eglSwapBuffers() failed with error: %x\n",
            eglGetError());
    return 1;
  }

  return 0;
}

GLuint create_shader(const GLchar* shader_text, GLenum shader_type)
{
  GLenum gl_error = GL_NO_ERROR;
  GLuint gl_shader = glCreateShader(shader_type);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glCreateShader() failed with error: %x\n",
            gl_error);
    return 0;
  }
  glShaderSource(gl_shader, 1, &shader_text, NULL);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glShaderSource() failed with error: %x\n",
            gl_error);
    return 0;
  }
  glCompileShader(gl_shader);
  if ((gl_error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "glCompileShader() failed with error: %x\n",
            gl_error);
    return 0;
  }

  GLint param;
  glGetShaderiv(gl_shader, GL_COMPILE_STATUS, &param);
  if (param != GL_TRUE) {
    GLchar error[1024];
    glGetShaderInfoLog(gl_shader, sizeof(error), NULL, error);
    fprintf(stderr, "glCompileShader() compilation failed with error: %s\n",
            error);
    fprintf(stderr, "%s\n", shader_text);
    return 0;
  }
  return gl_shader;
}

