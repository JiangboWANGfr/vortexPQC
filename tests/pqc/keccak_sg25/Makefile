ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := keccak_sg25
SRC_DIR := $(VORTEX_HOME)/tests/pqc/$(PROJECT)
MLKEM_DIR := $(VORTEX_HOME)/pqc/third_party/mlkem-native
THETA ?= 0
RHOPI ?= 0
CHII ?= 0
KROUND ?= 0
MAPPING ?= sg25
UNROLL ?= 0

ifneq ($(filter sg1 asm sg5 sg25,$(MAPPING)),$(MAPPING))
$(error MAPPING must be sg1, asm, sg5, or sg25)
endif
ifneq ($(MAPPING),sg25)
ifneq ($(filter 1,$(THETA) $(RHOPI) $(CHII) $(KROUND) $(UNROLL)),)
$(error MAPPING=$(MAPPING) cannot use stage, KROUND, or UNROLL options)
endif
ifeq ($(MAPPING),sg1)
MAPPING_DEFINE := SG25_MAPPING_SG1
else ifeq ($(MAPPING),asm)
MAPPING_DEFINE := SG25_MAPPING_ASM
else
MAPPING_DEFINE := SG25_MAPPING_SG5
endif
VX_CFLAGS += -D$(MAPPING_DEFINE)
CXXFLAGS += -D$(MAPPING_DEFINE)
endif
ifeq ($(MAPPING),asm)
ifneq ($(XLEN),32)
$(error MAPPING=asm requires XLEN=32)
endif
VX_LIBS += $(CURDIR)/pqrv_keccak.o
endif
ifeq ($(UNROLL),1)
VX_CFLAGS += -DSG25_UNROLL
CXXFLAGS += -DSG25_UNROLL
endif

$(foreach stage,THETA RHOPI CHII KROUND UNROLL,$(if $(filter 0 1,$($(stage))),,$(error $(stage) must be 0 or 1)))
ifeq ($(KROUND),1)
ifneq ($(filter 1,$(THETA) $(RHOPI) $(CHII)),)
$(error KROUND cannot be combined with THETA, RHOPI, or CHII)
endif
VX_CFLAGS += -DSG25_KROUND_ISA
CXXFLAGS += -DSG25_KROUND_ISA
endif
$(foreach stage,THETA RHOPI CHII,$(if $(filter 1,$($(stage))),$(eval VX_CFLAGS += -DSG25_$(stage)_ISA)$(eval CXXFLAGS += -DSG25_$(stage)_ISA)))
ifneq ($(filter 1,$(THETA) $(RHOPI) $(CHII)),)
VX_CFLAGS += -DSG25_ISE
CXXFLAGS += -DSG25_ISE
endif

SRCS := $(SRC_DIR)/main.cpp
VX_SRCS := $(SRC_DIR)/kernel.cpp

CXXFLAGS += -I$(MLKEM_DIR)/mlkem -I$(SRC_DIR) -I$(VORTEX_HOME)/tests/pqc \
            -DMLK_CONFIG_FILE='"vortex_ksg25_config.h"'
ifneq ($(MAPPING),sg25)
VX_CFLAGS += -I$(MLKEM_DIR)/mlkem -I$(SRC_DIR) \
             -DMLK_CONFIG_FILE='"vortex_ksg25_config.h"'
endif

VX_HDRS := $(SRC_DIR)/common.h $(SRC_DIR)/mapping.h \
           $(VORTEX_HOME)/tests/pqc/keccak_sg5/permute.h
ifneq ($(filter 1,$(THETA) $(RHOPI) $(CHII)),)
VX_HDRS += $(VORTEX_HOME)/sw/kernel/include/pqc/vx_ksg25.h
endif
ifeq ($(KROUND),1)
VX_HDRS += $(VORTEX_HOME)/sw/kernel/include/pqc/vx_kround25.h
endif
HDRS := $(SRC_DIR)/common.h $(SRC_DIR)/vortex_ksg25_config.h \
        $(SRC_DIR)/reference.h $(VORTEX_HOME)/tests/pqc/pqc_config.h

KERNEL_LIB := vortex2

include ../common.mk

ifeq ($(MAPPING),asm)
pqrv_keccak.o: $(VORTEX_HOME)/pqc/third_party/pqrv/fips202_rv32im.S $(CONFIG_STAMP)
	$(VX_CXX) $(VX_CFLAGS) -c $< -o $@
vx_start.o: pqrv_keccak.o
kernel.elf: pqrv_keccak.o
clean: clean-pqrv
clean-pqrv:
	rm -f pqrv_keccak.o
.PHONY: clean-pqrv
endif
