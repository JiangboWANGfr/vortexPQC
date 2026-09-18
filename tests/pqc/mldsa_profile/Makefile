ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mldsa_profile

SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

MLDSA_DIR := $(VORTEX_HOME)/pqc/third_party/mldsa-native

SRCS := $(SRC_DIR)/main.cpp $(SRC_DIR)/host_reference.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

# vortex_mldsa_prof_config.h includes the mldsa test's config, so -I reaches
# both directories; mld_vortex_alloc.h lives with the baseline too.
MLDSA_FLAGS := -I$(MLDSA_DIR)/mldsa -I$(VORTEX_HOME)/tests/pqc/mldsa -I$(SRC_DIR) \
               -I$(VORTEX_HOME)/tests/pqc \
               -DMLD_CONFIG_FILE='"vortex_mldsa_prof_config.h"'
VX_CFLAGS += $(MLDSA_FLAGS)
CXXFLAGS  += $(MLDSA_FLAGS)

VX_CFLAGS += -DPQC_MLDSA_WARP_REQUEST
CXXFLAGS += -DPQC_MLDSA_WARP_REQUEST

ifeq ($(KECCAK),pe)
	VX_CFLAGS += -DPQC_KECCAK_PE
	CXXFLAGS += -DPQC_KECCAK_PE
else ifeq ($(KECCAK),sg25_sw)
	VX_CFLAGS += -DPQC_KECCAK_SG25 -DPQC_KECCAK_SG25_SW
	CXXFLAGS += -DPQC_KECCAK_SG25 -DPQC_KECCAK_SG25_SW
else ifeq ($(KECCAK),sg25)
	VX_CFLAGS += -DPQC_KECCAK_SG25
	CXXFLAGS += -DPQC_KECCAK_SG25
else ifeq ($(KECCAK),kround)
	VX_CFLAGS += -DPQC_KECCAK_SG25 -DPQC_KECCAK_KROUND25
	CXXFLAGS += -DPQC_KECCAK_SG25 -DPQC_KECCAK_KROUND25
else ifneq ($(filter-out sg1,$(KECCAK)),)
$(error KECCAK must be sg1, sg25_sw, sg25, kround, pe, or unset)
endif

ifeq ($(NTT),reg32)
	VX_CFLAGS += -DPQC_NTT_REG32
	CXXFLAGS += -DPQC_NTT_REG32
else ifneq ($(NTT),)
$(error NTT must be reg32 or unset)
endif
ifeq ($(NTTMUL),ise)
ifneq ($(NTT),reg32)
$(error NTTMUL=ise requires NTT=reg32)
endif
	VX_CFLAGS += -DPQC_NTTMUL_D
	CXXFLAGS += -DPQC_NTTMUL_D
else ifneq ($(NTTMUL),)
$(error NTTMUL must be ise or unset)
endif
ifeq ($(NTTBF),ise)
ifneq ($(NTT),reg32)
$(error NTTBF=ise requires NTT=reg32)
endif
	VX_CFLAGS += -DPQC_NTTBF_D
	CXXFLAGS += -DPQC_NTTBF_D
else ifneq ($(NTTBF),)
$(error NTTBF must be ise or unset)
endif

ifeq ($(POINTWISE),ise)
ifneq ($(NTTMUL),ise)
$(error POINTWISE=ise requires NTTMUL=ise)
endif
	VX_CFLAGS += -DPQC_POINTWISE_ISE
	CXXFLAGS += -DPQC_POINTWISE_ISE
else ifneq ($(POINTWISE),)
$(error POINTWISE must be ise or unset)
endif
ifeq ($(POINTWISE_L5),w32)
ifneq ($(NTT),reg32)
$(error POINTWISE_L5=w32 requires NTT=reg32)
endif
	VX_CFLAGS += -DPQC_POINTWISE_L5_W32
	CXXFLAGS += -DPQC_POINTWISE_L5_W32
else ifneq ($(POINTWISE_L5),)
$(error POINTWISE_L5 must be w32 or unset)
endif

ifeq ($(UNROLL),1)
ifeq ($(filter sg25 sg25_sw,$(KECCAK)),)
$(error UNROLL=1 requires KECCAK=sg25 or sg25_sw)
endif
	VX_CFLAGS += -DPQC_KECCAK_UNROLL
	CXXFLAGS += -DPQC_KECCAK_UNROLL
else ifneq ($(UNROLL),)
$(error UNROLL must be 1 or unset)
endif

ifneq ($(ABLATE),)
ifneq ($(NTT),)
$(error ABLATE requires software NTT)
endif
ifneq ($(filter-out sg1,$(KECCAK)),)
$(error ABLATE requires KECCAK=sg1 or unset)
endif
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
HDRS := $(SRC_DIR)/common.h $(SRC_DIR)/host_reference.h \
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

VX_HDRS := $(HDRS) $(SRC_DIR)/mld_prof_fips202.h \
           $(SRC_DIR)/mld_prof_arith.h $(SRC_DIR)/vortex_mldsa_prof_config.h \
           $(VORTEX_HOME)/tests/pqc/mldsa/vortex_mldsa_config.h \
           $(VORTEX_HOME)/tests/pqc/mldsa/mld_vortex_alloc.h
ifeq ($(NTT),reg32)
VX_HDRS += $(SRC_DIR)/mld_ntt_reg32.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_ntt.h
endif
ifneq ($(filter ise w32,$(POINTWISE) $(POINTWISE_L5)),)
VX_HDRS += $(SRC_DIR)/mld_pointwise_w32.h
endif
ifneq ($(filter sg25 sg25_sw kround,$(KECCAK)),)
VX_HDRS += $(SRC_DIR)/mld_sg25.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_ksg25.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_kround25.h
endif

KERNEL_LIB := vortex2

include ../common.mk
