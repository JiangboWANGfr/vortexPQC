ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mlkem_profile

SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

# vortex_prof_config.h includes the mlkem test's config and adds the two
# counting backends, so -I reaches both directories.
MLKEM_FLAGS := -I$(MLKEM_DIR)/mlkem -I$(VORTEX_HOME)/tests/pqc/mlkem -I$(SRC_DIR) -I$(VORTEX_HOME)/tests/pqc \
               -DMLK_CONFIG_FILE='"vortex_prof_config.h"'
VX_CFLAGS += $(MLKEM_FLAGS)
CXXFLAGS  += $(MLKEM_FLAGS)
# Host only: the profile must consume the SAME coins as the baseline it claims
# to profile. tests/pqc/mlkem/Makefile:26 adds this the same way.
CXXFLAGS  += -I$(MLKEM_DIR)/test/test_vectors

# ABLATE=keccak|ntt turns the matching hooks into no-ops that still claim
# success. The build then computes garbage, on purpose: the cycle delta against
# ABLATE unset is that primitive's true cost, measured rather than estimated
# from a call count times a microbenchmark.
# KECCAK=pe routes the permutation to the KECCAKF instruction, inside this
# counting build, so the PE arm and its own baseline and floor are one build.
ifeq ($(KECCAK),pe)
	VX_CFLAGS += -DPQC_KECCAK_PE
	CXXFLAGS  += -DPQC_KECCAK_PE
else ifneq ($(KECCAK),)
$(error KECCAK must be pe or unset -- got '$(KECCAK)')
endif

ifeq ($(ABLATE),keccak)
	ABLATE_FLAGS := -DPQC_ABLATE_KECCAK
else ifeq ($(ABLATE),ntt)
	ABLATE_FLAGS := -DPQC_ABLATE_NTT
else ifeq ($(ABLATE),)
	ABLATE_FLAGS :=
else
# Without this, ABLATE=both -- or any typo -- builds the un-ablated baseline and
# reports it as an ablation. That is how a "measured" 26,233,434 for a
# configuration that does not exist got written down. A tab in front of the
# $(error) would make it a recipe line and it would never fire.
$(error ABLATE must be keccak, ntt, or unset -- got '$(ABLATE)')
endif
VX_CFLAGS += $(ABLATE_FLAGS)
CXXFLAGS  += $(ABLATE_FLAGS)

# The host binary reads kernel_arg_t out of these; without them a layout
# change rebuilds the kernel and leaves the host on the old struct.
HDRS := $(SRC_DIR)/common.h \
        $(SRC_DIR)/mlk_prof_counters.h \
        $(VORTEX_HOME)/tests/pqc/pqc_config.h

KERNEL_LIB := vortex2

include ../common.mk
