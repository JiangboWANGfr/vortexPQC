ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mlkem

SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native

SRCS := $(SRC_DIR)/main.cpp

# One translation unit: kernel.cpp includes the library's single-TU amalgam
# directly. Splitting it would duplicate the file-scope arena.
VX_SRCS := $(SRC_DIR)/kernel.cpp

# The library is reached through its own config hook, so the submodule tree is
# read-only to this test. Both the device and the host side need it: common.h
# takes the key and ciphertext sizes from mlkem_native.h.
MLKEM_FLAGS := -I$(MLKEM_DIR)/mlkem -I$(SRC_DIR) -I$(VORTEX_HOME)/tests/pqc \
               -DMLK_CONFIG_FILE='"vortex_mlkem_config.h"'
VX_CFLAGS += $(MLKEM_FLAGS)
CXXFLAGS  += $(MLKEM_FLAGS)

# The known-answer vectors are host-side only: the device has no use for 4.7 KB
# of expected output, and the file carries all three parameter sets.
CXXFLAGS  += -I$(MLKEM_DIR)/test/test_vectors

# VX_HDRS must be set before ../common.mk: it uses ?= and filters these out of
# the compiler inputs, so they act as prerequisites only.
VX_HDRS := $(SRC_DIR)/common.h $(SRC_DIR)/vortex_mlkem_config.h \
           $(SRC_DIR)/mlk_vortex_alloc.h $(VORTEX_HOME)/tests/pqc/pqc_stack.h

# The host binary reads kernel_arg_t out of these; without them a layout
# change rebuilds the kernel and leaves the host on the old struct.
HDRS := $(SRC_DIR)/common.h \
        $(SRC_DIR)/vortex_mlkem_config.h \
        $(SRC_DIR)/mlk_vortex_alloc.h \
        $(VORTEX_HOME)/tests/pqc/pqc_stack.h

KERNEL_LIB := vortex2

include ../common.mk
