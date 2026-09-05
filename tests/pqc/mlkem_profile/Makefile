ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mlkem_profile

SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

# vortex_prof_config.h includes the mlkem test's config and adds the two
# counting backends, so -I reaches both directories.
MLKEM_FLAGS := -I$(MLKEM_DIR)/mlkem -I$(VORTEX_HOME)/tests/pqc/mlkem -I$(SRC_DIR) \
               -DMLK_CONFIG_FILE='"vortex_prof_config.h"'
VX_CFLAGS += $(MLKEM_FLAGS)
CXXFLAGS  += $(MLKEM_FLAGS)

# ABLATE=keccak|ntt turns the matching hooks into no-ops that still claim
# success. The build then computes garbage, on purpose: the cycle delta against
# ABLATE unset is that primitive's true cost, measured rather than estimated
# from a call count times a microbenchmark.
ifeq ($(ABLATE),keccak)
	ABLATE_FLAGS := -DPQC_ABLATE_KECCAK
else ifeq ($(ABLATE),ntt)
	ABLATE_FLAGS := -DPQC_ABLATE_NTT
else
	ABLATE_FLAGS :=
endif
VX_CFLAGS += $(ABLATE_FLAGS)
CXXFLAGS  += $(ABLATE_FLAGS)

KERNEL_LIB := vortex2

include ../common.mk
