ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mldsa_microbench

SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

MLDSA_DIR := $(VORTEX_HOME)/pqc/third_party/mldsa-native

SRCS := $(SRC_DIR)/main.cpp

# kernel.cpp includes the library's translation unit itself: these are internal
# functions, and sharing a TU is what makes the benchmark see the same inlining
# the real build does.
VX_SRCS := $(SRC_DIR)/kernel.cpp

# The library configuration is the mlkem test's, not a copy. Two configs would
# eventually diverge, and then the per-primitive costs would belong to a
# different build than the end-to-end number they are meant to explain.
MLDSA_FLAGS := -I$(MLDSA_DIR)/mldsa -I$(VORTEX_HOME)/tests/pqc/mldsa -I$(SRC_DIR) -DMLD_CONFIG_FILE='"vortex_mldsa_config.h"'
               -DMLK_CONFIG_FILE='"vortex_mlkem_config.h"'
VX_CFLAGS += $(MLDSA_FLAGS)
CXXFLAGS  += $(MLDSA_FLAGS)

KERNEL_LIB := vortex2

include ../common.mk
