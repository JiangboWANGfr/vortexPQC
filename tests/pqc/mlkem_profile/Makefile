ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mlkem_profile

PARAM ?= 768
ifeq ($(filter $(PARAM),512 768 1024),)
$(error PARAM must be one of 512 768 1024)
endif
VX_CFLAGS += -DMLK_CONFIG_PARAMETER_SET=$(PARAM)
CXXFLAGS += -DMLK_CONFIG_PARAMETER_SET=$(PARAM)

SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

# vortex_prof_config.h includes the mlkem test's config and adds the two
# counting backends, so -I reaches both directories.
MLKEM_FLAGS := -I$(MLKEM_DIR)/mlkem -I$(VORTEX_HOME)/tests/pqc/mlkem -I$(VORTEX_HOME)/tests/pqc/mlkem_width \
               -I$(SRC_DIR) -I$(VORTEX_HOME)/tests/pqc \
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
# KECCAK selects the permutation backend while every other ML-KEM optimization
# stays fixed. sg1 is the serial C reference; SG25 variants distribute one
# state over lanes 0..24.
ifeq ($(KECCAK),pe)
	VX_CFLAGS += -DPQC_KECCAK_PE
	CXXFLAGS  += -DPQC_KECCAK_PE
else ifeq ($(KECCAK),asm)
ifneq ($(XLEN),32)
$(error KECCAK=asm requires XLEN=32)
endif
	VX_CFLAGS += -DPQC_KECCAK_ASM
	CXXFLAGS  += -DPQC_KECCAK_ASM
	VX_LIBS += $(CURDIR)/pqrv_keccak.o
else ifeq ($(KECCAK),sg1)
	VX_CFLAGS += -DPQC_KECCAK_SG1
	CXXFLAGS  += -DPQC_KECCAK_SG1
else ifeq ($(KECCAK),sg25)
	VX_CFLAGS += -DPQC_KECCAK_SG25
	CXXFLAGS  += -DPQC_KECCAK_SG25
else ifeq ($(KECCAK),sg25_sw)
	VX_CFLAGS += -DPQC_KECCAK_SG25 -DPQC_KECCAK_SG25_SW
	CXXFLAGS  += -DPQC_KECCAK_SG25 -DPQC_KECCAK_SG25_SW
else ifeq ($(KECCAK),kround)
	VX_CFLAGS += -DPQC_KECCAK_SG25 -DPQC_KECCAK_KROUND25
	CXXFLAGS  += -DPQC_KECCAK_SG25 -DPQC_KECCAK_KROUND25
else ifneq ($(KECCAK),)
$(error KECCAK must be sg1, asm, sg25_sw, sg25, kround, pe, or unset -- got '$(KECCAK)')
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

ifeq ($(SERIAL),1)
	VX_CFLAGS += -DPQC_SERIAL_FIPS202_ONLY
	CXXFLAGS  += -DPQC_SERIAL_FIPS202_ONLY
else ifneq ($(SERIAL),)
$(error SERIAL must be 1 or unset -- got '$(SERIAL)')
endif

ifneq ($(ARITH),)
ifneq ($(filter mulcache basemul reduce all,$(ARITH)),$(ARITH))
$(error ARITH must be mulcache, basemul, reduce, all, or unset)
endif
ifneq ($(NTT),reg32)
$(error ARITH requires NTT=reg32)
endif
	VX_CFLAGS += -DPQC_ARITH_COOP
	CXXFLAGS  += -DPQC_ARITH_COOP
ifneq ($(filter mulcache all,$(ARITH)),)
	VX_CFLAGS += -DPQC_ARITH_MULCACHE
	CXXFLAGS  += -DPQC_ARITH_MULCACHE
endif
ifneq ($(filter basemul all,$(ARITH)),)
	VX_CFLAGS += -DPQC_ARITH_BASEMUL
	CXXFLAGS  += -DPQC_ARITH_BASEMUL
endif
ifneq ($(filter reduce all,$(ARITH)),)
	VX_CFLAGS += -DPQC_ARITH_REDUCE
	CXXFLAGS  += -DPQC_ARITH_REDUCE
endif
endif

ifeq ($(ARITH_MUL),ise)
ifeq ($(filter mulcache basemul all,$(ARITH)),)
$(error ARITH_MUL=ise requires ARITH=mulcache, basemul, or all)
endif
	VX_CFLAGS += -DPQC_ARITH_NTTMUL
	CXXFLAGS  += -DPQC_ARITH_NTTMUL
else ifneq ($(ARITH_MUL),)
$(error ARITH_MUL must be ise or unset)
endif

ifeq ($(LINEAR),warp)
ifneq ($(NTT),reg32)
$(error LINEAR=warp requires NTT=reg32)
endif
	VX_CFLAGS += -DPQC_LINEAR_WARP
	CXXFLAGS  += -DPQC_LINEAR_WARP
else ifneq ($(LINEAR),)
$(error LINEAR must be warp or unset)
endif

ifeq ($(ZEROIZE),warp)
ifneq ($(NTT),reg32)
$(error ZEROIZE=warp requires NTT=reg32)
endif
	VX_CFLAGS += -DPQC_ZEROIZE_WARP
	CXXFLAGS  += -DPQC_ZEROIZE_WARP
else ifneq ($(ZEROIZE),)
$(error ZEROIZE must be warp or unset)
endif

ifneq ($(NTT),)
ifneq ($(filter coop reg32 smem32,$(NTT)),$(NTT))
$(error NTT must be coop, reg32, smem32, or unset -- got '$(NTT)')
endif
ifneq ($(ABLATE),)
$(error cooperative NTT requires ABLATE unset)
endif
	VX_CFLAGS += -DPQC_NTT_COOP
	CXXFLAGS  += -DPQC_NTT_COOP
ifeq ($(NTT),reg32)
	VX_CFLAGS += -DPQC_NTT_REG32
	CXXFLAGS  += -DPQC_NTT_REG32
else ifeq ($(NTT),smem32)
	VX_CFLAGS += -DPQC_NTT_SMEM32
	CXXFLAGS  += -DPQC_NTT_SMEM32
endif
endif

ifeq ($(NTTMUL),ise)
ifeq ($(NTT),)
$(error NTTMUL=ise requires a cooperative NTT layout)
endif
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

ifeq ($(SAMPLER),warp)
ifneq ($(NTT),reg32)
$(error SAMPLER=warp requires NTT=reg32)
endif
	VX_CFLAGS += -DPQC_REJ_WARP
	CXXFLAGS  += -DPQC_REJ_WARP
else ifneq ($(SAMPLER),)
$(error SAMPLER must be warp or unset)
endif

ifeq ($(CODEC),warp)
ifneq ($(NTT),reg32)
$(error CODEC=warp requires NTT=reg32)
endif
	VX_CFLAGS += -DPQC_CODEC_WARP
	CXXFLAGS  += -DPQC_CODEC_WARP
else ifeq ($(CODEC),all)
ifneq ($(NTT),reg32)
$(error CODEC=all requires NTT=reg32)
endif
	VX_CFLAGS += -DPQC_CODEC_WARP -DPQC_CODEC_COMPRESS_WARP
	CXXFLAGS  += -DPQC_CODEC_WARP -DPQC_CODEC_COMPRESS_WARP
else ifneq ($(CODEC),)
$(error CODEC must be warp, all, or unset)
endif

ifeq ($(NOISE),warp)
ifneq ($(NTT),reg32)
$(error NOISE=warp requires NTT=reg32)
endif
	VX_CFLAGS += -DPQC_NOISE_WARP
	CXXFLAGS  += -DPQC_NOISE_WARP
else ifneq ($(NOISE),)
$(error NOISE must be warp or unset)
endif

ifeq ($(PROFILE_ARITH),1)
	VX_CFLAGS += -DPQC_PROFILE_ARITH
	CXXFLAGS  += -DPQC_PROFILE_ARITH
else ifneq ($(PROFILE_ARITH),)
$(error PROFILE_ARITH must be 1 or unset)
endif

ifneq ($(filter 1 primitives,$(PROFILE_PHASES)),)
ifneq ($(SERIAL),1)
$(error PROFILE_PHASES requires SERIAL=1)
endif
ifneq ($(NTT),reg32)
$(error PROFILE_PHASES requires NTT=reg32)
endif
ifeq ($(PROFILE_PHASES),primitives)
ifeq ($(filter sg25 sg25_sw kround,$(KECCAK)),)
$(error PROFILE_PHASES=primitives requires KECCAK=sg25, sg25_sw, or kround)
endif
ifneq ($(PROFILE_ARITH),)
$(error PROFILE_PHASES=primitives cannot be combined with PROFILE_ARITH)
endif
	VX_CFLAGS += -DPQC_PROFILE_PHASES -DPQC_PROFILE_PRIMITIVES
	CXXFLAGS  += -DPQC_PROFILE_PHASES -DPQC_PROFILE_PRIMITIVES
else
	VX_CFLAGS += -DPQC_PROFILE_PHASES -DPQC_PROFILE_ARITH
	CXXFLAGS  += -DPQC_PROFILE_PHASES -DPQC_PROFILE_ARITH
endif
else ifneq ($(PROFILE_PHASES),)
$(error PROFILE_PHASES must be 1, primitives, or unset)
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
VX_HDRS := $(HDRS) $(SRC_DIR)/mlk_prof_arith.h \
           $(SRC_DIR)/mlk_prof_fips202.h $(SRC_DIR)/vortex_prof_config.h \
           $(SRC_DIR)/mlk_arith_dispatch.h $(SRC_DIR)/mlk_phase_profile.h \
           $(VORTEX_HOME)/tests/pqc/mlkem_coop_arith.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_ntt.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_pqc_defs.h
ifeq ($(CODEC),warp)
VX_HDRS += $(SRC_DIR)/mlk_codec_w32.h
else ifeq ($(CODEC),all)
VX_HDRS += $(SRC_DIR)/mlk_codec_w32.h
endif
ifeq ($(NOISE),warp)
VX_HDRS += $(SRC_DIR)/mlk_noise_w32.h
endif
ifeq ($(LINEAR),warp)
VX_HDRS += $(SRC_DIR)/mlk_linear_w32.h
endif
ifeq ($(ZEROIZE),warp)
VX_HDRS += $(SRC_DIR)/mlk_zeroize_w32.h
endif
ifneq ($(filter sg25 sg25_sw kround,$(KECCAK)),)
VX_HDRS += $(SRC_DIR)/profile_sg25_fips202.h \
           $(VORTEX_HOME)/tests/pqc/mlkem_width/sg25_fips202.h
endif
ifeq ($(KECCAK),sg25)
VX_HDRS += $(VORTEX_HOME)/sw/kernel/include/pqc/vx_ksg25.h
endif
ifeq ($(KECCAK),kround)
VX_HDRS += $(VORTEX_HOME)/sw/kernel/include/pqc/vx_kround25.h
endif
ifneq ($(NTT),)
VX_HDRS += $(VORTEX_HOME)/tests/pqc/mlkem_coop_ntt.h $(SRC_DIR)/mlk_coop_dispatch.h
endif

KERNEL_LIB := vortex2

include ../common.mk

ifeq ($(KECCAK),asm)
pqrv_keccak.o: $(VORTEX_HOME)/pqc/third_party/pqrv/fips202_rv32im.S $(CONFIG_STAMP)
	$(VX_CXX) $(VX_CFLAGS) -c $< -o $@
vx_start.o: pqrv_keccak.o
kernel.elf: pqrv_keccak.o
clean: clean-pqrv
clean-pqrv:
	rm -f pqrv_keccak.o
.PHONY: clean-pqrv
endif
