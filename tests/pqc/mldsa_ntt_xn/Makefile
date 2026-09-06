ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mldsa_ntt_xn
SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)
MLDSA_DIR := $(VORTEX_HOME)/pqc/third_party/mldsa-native

SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp

MLDSA_FLAGS := -I$(MLDSA_DIR)/mldsa -I$(VORTEX_HOME)/tests/pqc/mldsa -I$(SRC_DIR) \
               -I$(VORTEX_HOME)/tests/pqc \
               -DMLD_CONFIG_FILE='"vortex_mldsa_config.h"'
VX_CFLAGS += $(MLDSA_FLAGS)
CXXFLAGS  += $(MLDSA_FLAGS)

VX_HDRS := $(SRC_DIR)/common.h $(VORTEX_HOME)/tests/pqc/pqc_stack.h
HDRS    := $(SRC_DIR)/common.h $(VORTEX_HOME)/tests/pqc/pqc_stack.h \
        $(VORTEX_HOME)/tests/pqc/pqc_config.h

KERNEL_LIB := vortex2

include ../common.mk
