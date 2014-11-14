# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Pull in chromium os defaults
OUT ?= $(PWD)/build-opt-local

include common.mk

PC_DEPS = libdrm egl gbm
PC_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PC_DEPS))
PC_LIBS := $(shell $(PKG_CONFIG) --libs $(PC_DEPS))

DRM_LIBS = -lGLESv2
CFLAGS += $(PC_CFLAGS)
LDLIBS += $(PC_LIBS)

all: CC_BINARY(null_platform_test)

CC_BINARY(null_platform_test): null_platform_test.o
CC_BINARY(null_platform_test): LDLIBS += $(DRM_LIBS)
