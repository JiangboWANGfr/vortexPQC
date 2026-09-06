ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mldsa

SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

MLDSA_DIR := $(VORTEX_HOME)/pqc/third_party/mldsa-native

SRCS := $(SRC_DIR)/main.cpp

# kernel.cpp pulls the library in itself; see the comment there for why a
# separate translation unit does not work for ML-DSA.
VX_SRCS := $(SRC_DIR)/kernel.cpp

MLDSA_FLAGS := -I$(MLDSA_DIR)/mldsa -I$(SRC_DIR) -I$(VORTEX_HOME)/tests/pqc \
               -DMLD_CONFIG_FILE='"vortex_mldsa_config.h"'
VX_CFLAGS += $(MLDSA_FLAGS)
CXXFLAGS  += $(MLDSA_FLAGS)

# VX_HDRS must be set before ../common.mk: it uses ?= and filters these out of
# the compiler inputs, so they act as prerequisites only.
VX_HDRS := $(SRC_DIR)/common.h $(SRC_DIR)/vortex_mldsa_config.h \
           $(SRC_DIR)/mld_vortex_alloc.h $(VORTEX_HOME)/tests/pqc/pqc_stack.h

KERNEL_LIB := vortex2

include ../common.mk
