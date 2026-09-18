ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

SRC_DIR := $(VORTEX_HOME)/hw/unittest/pqc_unit
RTL_DIR := $(VORTEX_HOME)/hw/rtl
TOP ?= VX_pqc_unit_tb
MEM_LANES ?= 4
ALU_LANES ?= 4
UNROLL ?= 1
THREADS ?= 1
OBJ_DIR := obj_$(TOP)_$(XLEN)_$(MEM_LANES)_$(ALU_LANES)_$(UNROLL)
EXT_CONFIG := -DVX_CFG_EXT_PQC_ENABLE

RTL_INCLUDE := -I$(ROOT_DIR)/hw -I$(ROOT_DIR)/sw -I$(RTL_DIR) \
    -I$(RTL_DIR)/libs -I$(RTL_DIR)/interfaces -I$(RTL_DIR)/pqc \
    -I$(SRC_DIR) -I$(SRC_DIR)/sim
RTL_SOURCES := $(RTL_DIR)/VX_gpu_pkg.sv $(RTL_DIR)/pqc/VX_pqc_pkg.sv \
    $(SRC_DIR)/sim/$(TOP).sv
RTL_DEPS := $(wildcard $(RTL_DIR)/pqc/*.sv $(SRC_DIR)/sim/*)

ifeq ($(TOP),VX_pqc_sfu_tb)
RTL_INCLUDE += -I$(RTL_DIR)/core -I$(RTL_DIR)/rtu -I$(RTL_DIR)/fpu
RTL_SOURCES := $(RTL_DIR)/VX_gpu_pkg.sv $(RTL_DIR)/rtu/VX_rtu_pkg.sv \
    $(RTL_DIR)/pqc/VX_pqc_pkg.sv $(SRC_DIR)/sim/$(TOP).sv
RTL_DEPS += $(wildcard $(RTL_DIR)/core/*.sv $(RTL_DIR)/rtu/*.sv $(RTL_DIR)/libs/*.sv)
override CONFIGS += -DVX_CFG_EXT_RTU_ENABLE -DVX_CFG_EXT_F_DISABLE
endif

ifneq ($(filter VX_pqc_nttmul_tb VX_pqc_nttmul_d_tb,$(TOP)),)
EXT_CONFIG := -DVX_CFG_EXT_NTT_ENABLE
RTL_INCLUDE += -I$(RTL_DIR)/core
RTL_DEPS += $(wildcard $(RTL_DIR)/core/*.sv $(RTL_DIR)/libs/*.sv)
override CONFIGS += -DVX_CFG_NUM_THREADS=$(ALU_LANES) \
                    -DVX_CFG_SIMD_WIDTH=$(ALU_LANES) \
                    -DVX_CFG_NUM_ALU_LANES=$(ALU_LANES)
endif

.PHONY: all run run-agu run-sfu run-nttmul run-nttmul-d clean
all: $(OBJ_DIR)/V$(TOP)

$(OBJ_DIR)/V$(TOP): $(RTL_DEPS) $(ROOT_DIR)/hw/VX_config.vh Makefile
	$(VERILATOR_PATH)/bin/verilator --binary --timing --assert -Wall -Wno-fatal \
	    -Wno-DECLFILENAME -Wno-REDEFMACRO --output-split 10000 -j $(THREADS) \
	    -DVX_CFG_XLEN=$(XLEN) $(EXT_CONFIG) \
	    -DVX_CFG_NUM_LSU_LANES=$(MEM_LANES) -DPQC_TOP_UNROLL=$(UNROLL) \
	    $(CONFIGS) $(RTL_INCLUDE) $(RTL_SOURCES) \
	    --top-module $(TOP) --Mdir $(OBJ_DIR)

run: all
	./$(OBJ_DIR)/V$(TOP) $(OPTS)

run-agu:
	$(MAKE) TOP=VX_pqc_agu_tb run

run-sfu:
	$(MAKE) TOP=VX_pqc_sfu_tb run

run-nttmul:
	$(MAKE) TOP=VX_pqc_nttmul_tb run

run-nttmul-d:
	$(MAKE) TOP=VX_pqc_nttmul_d_tb run

clean:
	rm -rf obj_VX_pqc_*
