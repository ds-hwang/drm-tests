# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Pull in chromium os defaults
OUT ?= $(PWD)/build-opt-local

include common.mk

DRM_LIBS = -lGLESv2

all: CC_BINARY(egl_chromesim_modified) CC_BINARY(egl_clear)

CC_BINARY(egl_chromesim_modified): egl_chromesim_modified.o egl_init.o
CC_BINARY(egl_chromesim_modified): LDLIBS += $(DRM_LIBS)

CC_BINARY(egl_clear): egl_clear.o egl_init.o
CC_BINARY(egl_clear): LDLIBS += $(DRM_LIBS)
