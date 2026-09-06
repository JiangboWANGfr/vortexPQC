ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mlkem_width

SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

MLKEM_FLAGS := -I$(MLKEM_DIR)/mlkem -I$(VORTEX_HOME)/tests/pqc/mlkem -I$(SRC_DIR) \
               -DMLK_CONFIG_FILE='"vortex_width_config.h"'
VX_CFLAGS += $(MLKEM_FLAGS)
CXXFLAGS  += $(MLKEM_FLAGS)

# ARENA= is retired: the per-hart arena is unconditional now, inherited from
# tests/pqc/mlkem/vortex_mlkem_config.h.

ifneq ($(PAINT),)
	VX_CFLAGS += -DPQC_PAINT_SLABS=$(PAINT)
endif

ifeq ($(SERIAL),1)
	VX_CFLAGS += -DPQC_SERIAL_FIPS202_ONLY
	CXXFLAGS  += -DPQC_SERIAL_FIPS202_ONLY
endif

# The host binary reads kernel_arg_t out of these; without them a layout
# change rebuilds the kernel and leaves the host on the old struct.
HDRS := $(SRC_DIR)/common.h \
        $(SRC_DIR)/mlk_width_counters.h \
        $(VORTEX_HOME)/tests/pqc/pqc_stack.h

KERNEL_LIB := vortex2

include ../common.mk
