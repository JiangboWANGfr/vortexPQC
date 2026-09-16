ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := keccak_sg5
SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)
MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native

SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp

MLKEM_FLAGS := -I$(MLKEM_DIR)/mlkem -I$(SRC_DIR) -I$(VORTEX_HOME)/tests/pqc \
               -DMLK_CONFIG_FILE='"vortex_ksg5_config.h"'
VX_CFLAGS += $(MLKEM_FLAGS)
CXXFLAGS  += $(MLKEM_FLAGS)

VX_HDRS := $(SRC_DIR)/common.h $(SRC_DIR)/vortex_ksg5_config.h $(SRC_DIR)/permute.h \
        $(VORTEX_HOME)/tests/pqc/pqc_stack.h
HDRS    := $(SRC_DIR)/common.h $(SRC_DIR)/vortex_ksg5_config.h \
        $(VORTEX_HOME)/tests/pqc/pqc_stack.h \
        $(VORTEX_HOME)/tests/pqc/pqc_config.h

ifeq ($(KECCAK),pe)
	VX_CFLAGS += -DPQC_KECCAK_PE
	CXXFLAGS  += -DPQC_KECCAK_PE
endif

KERNEL_LIB := vortex2

include ../common.mk
