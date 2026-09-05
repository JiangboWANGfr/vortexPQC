ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mldsa

SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

MLDSA_DIR := $(VORTEX_HOME)/pqc/third_party/mldsa-native

SRCS := $(SRC_DIR)/main.cpp

# kernel.cpp pulls the library in itself; see the comment there for why a
# separate translation unit does not work for ML-DSA.
VX_SRCS := $(SRC_DIR)/kernel.cpp

MLDSA_FLAGS := -I$(MLDSA_DIR)/mldsa -I$(SRC_DIR) -DMLD_CONFIG_FILE='"vortex_mldsa_config.h"'
VX_CFLAGS += $(MLDSA_FLAGS)
CXXFLAGS  += $(MLDSA_FLAGS)

KERNEL_LIB := vortex2

include ../common.mk
