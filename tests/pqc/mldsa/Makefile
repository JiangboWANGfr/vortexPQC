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
VX_HDRS := $(SRC_DIR)/common.h $(SRC_DIR)/mld_simt_counters.h $(SRC_DIR)/vortex_mldsa_config.h \
           $(SRC_DIR)/mld_vortex_alloc.h $(VORTEX_HOME)/tests/pqc/pqc_stack.h

# The host binary reads kernel_arg_t out of these; without them a layout
# change rebuilds the kernel and leaves the host on the old struct.
HDRS := $(SRC_DIR)/common.h \
        $(SRC_DIR)/vortex_mldsa_config.h \
        $(SRC_DIR)/mld_vortex_alloc.h \
        $(VORTEX_HOME)/tests/pqc/pqc_stack.h

# MLDSA_RAM=full drops MLD_CONFIG_REDUCE_RAM: matrix A is materialised instead of
# re-expanded per use, which restores the library's 4-wide Keccak batching at the
# cost of a much larger arena. See vortex_mldsa_config.h.
ifeq ($(MLDSA_RAM),full)
	VX_CFLAGS += -DPQC_MLDSA_RAM_FULL
	CXXFLAGS  += -DPQC_MLDSA_RAM_FULL
else ifneq ($(filter-out low,$(MLDSA_RAM)),)
$(error MLDSA_RAM must be low or full -- got '$(MLDSA_RAM)')
endif

# SIMT_KECCAK=1 enables the lane-cooperative Keccak backend so -t means something.
ifeq ($(SIMT_KECCAK),1)
	VX_CFLAGS += -DPQC_SIMT_KECCAK
	CXXFLAGS  += -DPQC_SIMT_KECCAK
else ifneq ($(SIMT_KECCAK),)
$(error SIMT_KECCAK must be 1 or unset -- got '$(SIMT_KECCAK)')
endif

KERNEL_LIB := vortex2

include ../common.mk
