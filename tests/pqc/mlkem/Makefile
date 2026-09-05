ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := mlkem

SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)

MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native

SRCS := $(SRC_DIR)/main.cpp

# mlkem_native_tu.cpp is the shim that pulls the library's single translation
# unit in; the library itself is never listed, copied or patched.
VX_SRCS := $(SRC_DIR)/kernel.cpp $(SRC_DIR)/mlkem_native_tu.cpp

# The library is reached through its own config hook, so the submodule tree is
# read-only to this test. Both the device and the host side need it: common.h
# takes the key and ciphertext sizes from mlkem_native.h.
MLKEM_FLAGS := -I$(MLKEM_DIR)/mlkem -I$(SRC_DIR) -DMLK_CONFIG_FILE='"vortex_mlkem_config.h"'
VX_CFLAGS += $(MLKEM_FLAGS)
CXXFLAGS  += $(MLKEM_FLAGS)

# The known-answer vectors are host-side only: the device has no use for 4.7 KB
# of expected output, and the file carries all three parameter sets.
CXXFLAGS  += -I$(MLKEM_DIR)/test/test_vectors

KERNEL_LIB := vortex2

include ../common.mk
