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

KERNEL_LIB := vortex2

include ../common.mk
