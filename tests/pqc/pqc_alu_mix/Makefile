ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := pqc_alu_mix
SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)
MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native
KECCAK_DIR := $(VORTEX_HOME)/tests/pqc/keccak_sg25

SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp
CXXFLAGS += -I$(VORTEX_HOME)/tests/pqc -I$(KECCAK_DIR) -I$(MLKEM_DIR)/mlkem \
            -DMLK_CONFIG_FILE='"vortex_ksg25_config.h"'
HDRS := $(SRC_DIR)/common.h $(KECCAK_DIR)/reference.h $(KECCAK_DIR)/common.h \
        $(KECCAK_DIR)/vortex_ksg25_config.h $(VORTEX_HOME)/tests/pqc/pqc_config.h
VX_HDRS := $(SRC_DIR)/common.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_ksg25.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_kround25.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_ntt.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_pqc_defs.h
KERNEL_LIB := vortex2

include ../common.mk
