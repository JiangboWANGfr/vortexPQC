ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := nttbf_k
SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp

CXXFLAGS += -I$(VORTEX_HOME)/tests/pqc

VX_HDRS := $(SRC_DIR)/common.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_ntt.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_pqc_defs.h
HDRS := $(SRC_DIR)/common.h $(VORTEX_HOME)/tests/pqc/pqc_config.h

KERNEL_LIB := vortex2

include ../common.mk
