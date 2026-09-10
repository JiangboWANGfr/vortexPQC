ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := keccak_sg25
SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)
MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native
THETA ?= 0
RHOPI ?= 0
CHII ?= 0
KROUND ?= 0

$(foreach stage,THETA RHOPI CHII KROUND,$(if $(filter 0 1,$($(stage))),,$(error $(stage) must be 0 or 1)))
ifeq ($(KROUND),1)
ifneq ($(filter 1,$(THETA) $(RHOPI) $(CHII)),)
$(error KROUND cannot be combined with THETA, RHOPI, or CHII)
endif
VX_CFLAGS += -DSG25_KROUND_ISA
CXXFLAGS += -DSG25_KROUND_ISA
endif
$(foreach stage,THETA RHOPI CHII,$(if $(filter 1,$($(stage))),$(eval VX_CFLAGS += -DSG25_$(stage)_ISA)$(eval CXXFLAGS += -DSG25_$(stage)_ISA)))
ifneq ($(filter 1,$(THETA) $(RHOPI) $(CHII)),)
VX_CFLAGS += -DSG25_ISE
CXXFLAGS += -DSG25_ISE
endif

SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp

CXXFLAGS += -I$(MLKEM_DIR)/mlkem -I$(SRC_DIR) -I$(VORTEX_HOME)/tests/pqc \
            -DMLK_CONFIG_FILE='"vortex_ksg25_config.h"'

VX_HDRS := $(SRC_DIR)/common.h
ifneq ($(filter 1,$(THETA) $(RHOPI) $(CHII)),)
VX_HDRS += $(VORTEX_HOME)/sw/kernel/include/pqc/vx_ksg25.h
endif
ifeq ($(KROUND),1)
VX_HDRS += $(VORTEX_HOME)/sw/kernel/include/pqc/vx_kround25.h
endif
HDRS := $(SRC_DIR)/common.h $(SRC_DIR)/vortex_ksg25_config.h \
        $(SRC_DIR)/reference.h $(VORTEX_HOME)/tests/pqc/pqc_config.h

KERNEL_LIB := vortex2

include ../common.mk
