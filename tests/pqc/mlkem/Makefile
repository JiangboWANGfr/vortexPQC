ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mlkem

SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native

SRCS := $(SRC_DIR)/main.cpp

# One translation unit: kernel.cpp includes the library's single-TU amalgam
# directly. Splitting it would duplicate the file-scope arena.
VX_SRCS := $(SRC_DIR)/kernel.cpp

# The library is reached through its own config hook, so the submodule tree is
# read-only to this test. Both the device and the host side need it: common.h
# takes the key and ciphertext sizes from mlkem_native.h.
MLKEM_FLAGS := -I$(MLKEM_DIR)/mlkem -I$(SRC_DIR) -I$(VORTEX_HOME)/tests/pqc \
               -DMLK_CONFIG_FILE='"vortex_mlkem_config.h"'
VX_CFLAGS += $(MLKEM_FLAGS)
CXXFLAGS  += $(MLKEM_FLAGS)

# The known-answer vectors are host-side only: the device has no use for 4.7 KB
# of expected output, and the file carries all three parameter sets.
CXXFLAGS  += -I$(MLKEM_DIR)/test/test_vectors

# VX_HDRS must be set before ../common.mk: it uses ?= and filters these out of
# the compiler inputs, so they act as prerequisites only.
VX_HDRS := $(SRC_DIR)/common.h $(SRC_DIR)/vortex_mlkem_config.h \
           $(SRC_DIR)/vortex_keccak_pe.h \
           $(SRC_DIR)/mlk_vortex_alloc.h $(VORTEX_HOME)/tests/pqc/pqc_stack.h

# The host binary reads kernel_arg_t out of these; without them a layout
# change rebuilds the kernel and leaves the host on the old struct.
HDRS := $(SRC_DIR)/common.h \
        $(SRC_DIR)/vortex_mlkem_config.h \
        $(SRC_DIR)/mlk_vortex_alloc.h \
        $(VORTEX_HOME)/tests/pqc/pqc_stack.h \
        $(VORTEX_HOME)/tests/pqc/pqc_config.h

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
else ifeq ($(KECCAK),pe)
# The KECCAKF instruction. Needs the extension enabled in the simulator's
# resolved config as well, so the run carries CONFIGS=-DVX_CFG_EXT_PQC_ENABLE.
	VX_CFLAGS += -DPQC_KECCAK_PE
	CXXFLAGS  += -DPQC_KECCAK_PE
else ifneq ($(KECCAK),)
$(error KECCAK must be asm, pe, or unset -- got '$(KECCAK)')
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
