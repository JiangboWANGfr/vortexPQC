ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mldsa_profile

SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

MLDSA_DIR := $(VORTEX_HOME)/pqc/third_party/mldsa-native

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

# vortex_mldsa_prof_config.h includes the mldsa test's config, so -I reaches
# both directories; mld_vortex_alloc.h lives with the baseline too.
MLDSA_FLAGS := -I$(MLDSA_DIR)/mldsa -I$(VORTEX_HOME)/tests/pqc/mldsa -I$(SRC_DIR) \
               -DMLD_CONFIG_FILE='"vortex_mldsa_prof_config.h"'
VX_CFLAGS += $(MLDSA_FLAGS)
CXXFLAGS  += $(MLDSA_FLAGS)

# ABLATE=keccak|ntt turns the matching hooks into no-ops that still claim
# success; the cycle delta against ABLATE unset is that primitive's cost.
# The build then computes garbage, on purpose.
ifeq ($(ABLATE),keccak)
	ABLATE_FLAGS := -DPQC_ABLATE_KECCAK
else ifeq ($(ABLATE),ntt)
	ABLATE_FLAGS := -DPQC_ABLATE_NTT
else
	ABLATE_FLAGS :=
endif
# Ablation on ML-DSA is only meaningful for keypair, so an ablated build is
# built KEYPAIR_ONLY -- see the comment in kernel.cpp.
ifneq ($(ABLATE),)
	ABLATE_FLAGS += -DPQC_KEYPAIR_ONLY
endif
VX_CFLAGS += $(ABLATE_FLAGS)
CXXFLAGS  += $(ABLATE_FLAGS)

KERNEL_LIB := vortex2

include ../common.mk
