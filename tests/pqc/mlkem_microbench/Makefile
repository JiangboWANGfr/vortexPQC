ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mlkem_microbench

SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native

SRCS := $(SRC_DIR)/main.cpp

# kernel.cpp includes the library's translation unit itself: these are internal
# functions, and sharing a TU is what makes the benchmark see the same inlining
# the real build does.
VX_SRCS := $(SRC_DIR)/kernel.cpp

# The library configuration is the mlkem test's, not a copy. Two configs would
# eventually diverge, and then the per-primitive costs would belong to a
# different build than the end-to-end number they are meant to explain.
MLKEM_FLAGS := -I$(MLKEM_DIR)/mlkem -I$(VORTEX_HOME)/tests/pqc/mlkem -I$(SRC_DIR) \
               -DMLK_CONFIG_FILE='"vortex_mlkem_config.h"'
VX_CFLAGS += $(MLKEM_FLAGS)
CXXFLAGS  += $(MLKEM_FLAGS)

# The host binary reads kernel_arg_t out of these; without them a layout
# change rebuilds the kernel and leaves the host on the old struct.
HDRS := $(SRC_DIR)/common.h

# KECCAK=asm builds the "optimized software" Keccak column: PQRV's hand-written
# RV32 assembly instead of mlkem-native's reference C. See
# pqc/third_party/pqrv/README.md and tests/pqc/mlkem/vortex_keccak_pqrv.h.
#
# The .S is assembled separately and linked through VX_LIBS rather than being
# added to VX_SRCS: a %.S in VX_SRCS switches common.mk to a branch that skips
# the two-pass vx_start.o generation (common.mk:193), which would put a
# link-path difference inside a cycle count meant to be attributed to Keccak.
PQRV_DIR := $(VORTEX_HOME)/pqc/third_party/pqrv
ifeq ($(KECCAK),asm)
	VX_CFLAGS += -DPQC_KECCAK_ASM
	CXXFLAGS  += -DPQC_KECCAK_ASM
	VX_LIBS   += $(CURDIR)/pqrv_keccak.o
else ifneq ($(KECCAK),)
$(error KECCAK must be asm or unset -- got '$(KECCAK)')
endif

KERNEL_LIB := vortex2

include ../common.mk

ifeq ($(KECCAK),asm)
pqrv_keccak.o: $(PQRV_DIR)/fips202_rv32im.S
	$(VX_CXX) $(VX_CFLAGS) -c $< -o $@
# Both rules link, so both need the object present first.
vx_start.o: pqrv_keccak.o
kernel.elf: pqrv_keccak.o
clean: clean-pqrv
clean-pqrv:
	rm -f pqrv_keccak.o
.PHONY: clean-pqrv
endif
