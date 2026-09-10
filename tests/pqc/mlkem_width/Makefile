ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mlkem_width

SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

MLKEM_FLAGS := -I$(MLKEM_DIR)/mlkem -I$(VORTEX_HOME)/tests/pqc/mlkem -I$(SRC_DIR) -I$(VORTEX_HOME)/tests/pqc \
               -DMLK_CONFIG_FILE='"vortex_width_config.h"'
VX_CFLAGS += $(MLKEM_FLAGS)
CXXFLAGS  += $(MLKEM_FLAGS)

# ARENA= is retired: the per-hart arena is unconditional now, inherited from
# tests/pqc/mlkem/vortex_mlkem_config.h.

ifneq ($(PAINT),)
	VX_CFLAGS += -DPQC_PAINT_SLABS=$(PAINT)
endif

# KECCAK=pe swaps the engine inside the SIMT backend and leaves its lane mapping
# unchanged. SG25 arms select one state distributed across a 32-lane warp;
# sg25_sw uses shuffles, sg25 uses the six stage instructions, and kround uses
# the two single-writeback halves of a whole-round comparator.
ifeq ($(KECCAK),pe)
	VX_CFLAGS += -DPQC_KECCAK_PE
	CXXFLAGS  += -DPQC_KECCAK_PE
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
$(error KECCAK must be pe, sg25, sg25_sw, kround, or unset -- got '$(KECCAK)')
endif

ifeq ($(SERIAL),1)
	VX_CFLAGS += -DPQC_SERIAL_FIPS202_ONLY
	CXXFLAGS  += -DPQC_SERIAL_FIPS202_ONLY
endif

# The host binary reads kernel_arg_t out of these; without them a layout
# change rebuilds the kernel and leaves the host on the old struct.
HDRS := $(SRC_DIR)/common.h \
        $(SRC_DIR)/mlk_width_counters.h \
        $(VORTEX_HOME)/tests/pqc/pqc_stack.h \
        $(VORTEX_HOME)/tests/pqc/pqc_config.h

ifneq ($(filter sg25 sg25_sw kround,$(KECCAK)),)
VX_HDRS += $(SRC_DIR)/sg25_fips202.h
endif
ifeq ($(KECCAK),sg25)
VX_HDRS += $(VORTEX_HOME)/sw/kernel/include/pqc/vx_ksg25.h
endif
ifeq ($(KECCAK),kround)
VX_HDRS += $(VORTEX_HOME)/sw/kernel/include/pqc/vx_kround25.h
endif

KERNEL_LIB := vortex2

include ../common.mk
