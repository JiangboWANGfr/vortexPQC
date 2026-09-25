ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := shared_ntt_xn
SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)
SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp

SCHEME ?= K
ifeq ($(SCHEME),K)
LIBRARY := mlkem
FLAGS := -DMLK_CONFIG_FILE='"vortex_mlkem_config.h"'
else ifeq ($(SCHEME),D)
LIBRARY := mldsa
FLAGS := -DMLD_CONFIG_FILE='"vortex_mldsa_config.h"' -DPQC_NTT_D
else
$(error SCHEME must be K or D)
endif

FLAGS += -I$(VORTEX_HOME)/pqc/third_party/$(LIBRARY)-native/$(LIBRARY) \
         -I$(VORTEX_HOME)/tests/pqc/$(LIBRARY) -I$(VORTEX_HOME)/tests/pqc \
         -I$(SRC_DIR) -DPQC_NTT_REG32
ifeq ($(NTT),ise)
FLAGS += -DPQC_NTT_ISE -DPQC_NTTMUL_$(SCHEME) -DPQC_NTTBF_$(SCHEME)
else ifneq ($(filter-out sw,$(NTT)),)
$(error NTT must be sw or ise)
endif
VX_CFLAGS += $(FLAGS)
CXXFLAGS += $(FLAGS)
HDRS := $(SRC_DIR)/common.h $(VORTEX_HOME)/tests/pqc/pqc_config.h
VX_HDRS := $(HDRS) $(VORTEX_HOME)/tests/pqc/pqc_stack.h \
           $(VORTEX_HOME)/tests/pqc/mlkem_coop_ntt.h \
           $(VORTEX_HOME)/tests/pqc/mldsa_ntt_reg32.h \
           $(VORTEX_HOME)/sw/kernel/include/pqc/vx_ntt.h
KERNEL_LIB := vortex2
include ../common.mk
