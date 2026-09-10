ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mlkem_arith_xn
SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)
MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native

ifneq ($(XLEN),32)
$(error mlkem_arith_xn currently requires XLEN=32)
endif

SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp
MLKEM_FLAGS := -I$(MLKEM_DIR)/mlkem -I$(VORTEX_HOME)/tests/pqc/mlkem \
               -I$(SRC_DIR) -I$(VORTEX_HOME)/tests/pqc \
               -DMLK_CONFIG_FILE='"vortex_mlkem_config.h"'
VX_CFLAGS += $(MLKEM_FLAGS)
CXXFLAGS += $(MLKEM_FLAGS)
HDRS := $(SRC_DIR)/common.h $(VORTEX_HOME)/tests/pqc/pqc_config.h
VX_HDRS := $(HDRS) $(VORTEX_HOME)/tests/pqc/mlkem_coop_arith.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_ntt.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_pqc_defs.h
KERNEL_LIB := vortex2

include ../common.mk
