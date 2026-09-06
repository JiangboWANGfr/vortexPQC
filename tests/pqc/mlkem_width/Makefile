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

KERNEL_LIB := vortex2

include ../common.mk
