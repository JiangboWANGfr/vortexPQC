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
               -I$(VORTEX_HOME)/tests/pqc \
               -DMLD_CONFIG_FILE='"vortex_mldsa_prof_config.h"'
VX_CFLAGS += $(MLDSA_FLAGS)
CXXFLAGS  += $(MLDSA_FLAGS)

# KECCAK=pe routes the permutation to the KECCAKF instruction, inside this
# counting build, so the PE arm and its own baseline and floor are one build.
ifeq ($(KECCAK),pe)
	VX_CFLAGS += -DPQC_KECCAK_PE
	CXXFLAGS  += -DPQC_KECCAK_PE
else ifneq ($(KECCAK),)
$(error KECCAK must be pe or unset -- got '$(KECCAK)')
endif

# ABLATE=keccak|ntt turns the matching hooks into no-ops that still claim
# success; the cycle delta against ABLATE unset is that primitive's cost.
# The build then computes garbage, on purpose.
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
# Ablation on ML-DSA is only meaningful for keypair, so an ablated build is
# built KEYPAIR_ONLY -- see the comment in kernel.cpp.
ifneq ($(ABLATE),)
	ABLATE_FLAGS += -DPQC_KEYPAIR_ONLY
endif
VX_CFLAGS += $(ABLATE_FLAGS)
CXXFLAGS  += $(ABLATE_FLAGS)

# The host binary reads kernel_arg_t out of these; without them a layout
# change rebuilds the kernel and leaves the host on the old struct.
HDRS := $(SRC_DIR)/common.h \
        $(SRC_DIR)/mld_prof_counters.h \
        $(VORTEX_HOME)/tests/pqc/pqc_stack.h \
        $(VORTEX_HOME)/tests/pqc/pqc_config.h

# MLDSA_RAM=full drops MLD_CONFIG_REDUCE_RAM: matrix A is materialised instead of
# re-expanded per use, which restores the library's 4-wide Keccak batching at the
# cost of a much larger arena. See vortex_mldsa_config.h.
ifeq ($(MLDSA_RAM),full)
	VX_CFLAGS += -DPQC_MLDSA_RAM_FULL
	CXXFLAGS  += -DPQC_MLDSA_RAM_FULL
else ifneq ($(filter-out low,$(MLDSA_RAM)),)
$(error MLDSA_RAM must be low or full -- got '$(MLDSA_RAM)')
endif

KERNEL_LIB := vortex2

include ../common.mk
