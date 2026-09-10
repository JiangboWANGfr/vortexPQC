ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := lsu_model_probe
SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp

CXXFLAGS += -I$(VORTEX_HOME)/tests/pqc

VX_HDRS := $(SRC_DIR)/common.h
HDRS := $(SRC_DIR)/common.h $(VORTEX_HOME)/tests/pqc/pqc_config.h

KERNEL_LIB := vortex2

include ../common.mk
