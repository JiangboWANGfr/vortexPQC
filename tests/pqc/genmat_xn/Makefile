ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := genmat_xn
SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)
MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native

SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp

MLKEM_FLAGS := -I$(MLKEM_DIR)/mlkem -I$(VORTEX_HOME)/tests/pqc/mlkem -I$(SRC_DIR) \
               -I$(VORTEX_HOME)/tests/pqc \
               -DMLK_CONFIG_FILE='"vortex_gx_config.h"'
VX_CFLAGS += $(MLKEM_FLAGS)
CXXFLAGS  += $(MLKEM_FLAGS)

KERNEL_LIB := vortex2
include ../common.mk
