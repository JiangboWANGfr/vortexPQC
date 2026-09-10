ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mlkem_ntt_xn
SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)
MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native

SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp

MLKEM_FLAGS := -I$(MLKEM_DIR)/mlkem -I$(VORTEX_HOME)/tests/pqc/mlkem -I$(SRC_DIR) \
               -I$(VORTEX_HOME)/tests/pqc \
               -DMLK_CONFIG_FILE='"vortex_mlkem_config.h"'
VX_CFLAGS += $(MLKEM_FLAGS)
CXXFLAGS  += $(MLKEM_FLAGS)

ifeq ($(NTT),reg32)
ifneq ($(XLEN),32)
$(error NTT=reg32 currently requires XLEN=32)
endif
	VX_CFLAGS += -DPQC_NTT_REG32
	CXXFLAGS  += -DPQC_NTT_REG32
else ifeq ($(NTT),smem32)
ifneq ($(XLEN),32)
$(error NTT=smem32 currently requires XLEN=32)
endif
	VX_CFLAGS += -DPQC_NTT_SMEM32
	CXXFLAGS  += -DPQC_NTT_SMEM32
else ifneq ($(NTT),)
$(error NTT must be reg32, smem32, or unset -- got '$(NTT)')
endif

ifeq ($(NTTMUL),ise)
	VX_CFLAGS += -DPQC_NTTMUL_K
	CXXFLAGS  += -DPQC_NTTMUL_K
else ifneq ($(NTTMUL),)
$(error NTTMUL must be ise or unset -- got '$(NTTMUL)')
endif

ifeq ($(NTTBF),ise)
ifneq ($(NTT),reg32)
$(error NTTBF=ise requires NTT=reg32)
endif
	VX_CFLAGS += -DPQC_NTTBF_K
	CXXFLAGS  += -DPQC_NTTBF_K
else ifneq ($(NTTBF),)
$(error NTTBF must be ise or unset -- got '$(NTTBF)')
endif

VX_HDRS := $(SRC_DIR)/common.h $(VORTEX_HOME)/tests/pqc/pqc_stack.h \
           $(VORTEX_HOME)/tests/pqc/mlkem_coop_ntt.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_ntt.h
HDRS    := $(SRC_DIR)/common.h $(VORTEX_HOME)/tests/pqc/pqc_stack.h \
        $(VORTEX_HOME)/tests/pqc/pqc_config.h

KERNEL_LIB := vortex2

include ../common.mk
