ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

SRC_DIR := $(VORTEX_HOME)/hw/unittest/pqc_unit
RTL_DIR := $(VORTEX_HOME)/hw/rtl
TOP ?= VX_pqc_unit_tb
MEM_LANES ?= 4
UNROLL ?= 1
THREADS ?= 1
OBJ_DIR := obj_$(TOP)_$(XLEN)_$(MEM_LANES)_$(UNROLL)

RTL_INCLUDE := -I$(ROOT_DIR)/hw -I$(ROOT_DIR)/sw -I$(RTL_DIR) \
    -I$(RTL_DIR)/libs -I$(RTL_DIR)/interfaces -I$(RTL_DIR)/pqc \
    -I$(SRC_DIR) -I$(SRC_DIR)/sim
RTL_SOURCES := $(RTL_DIR)/VX_gpu_pkg.sv $(RTL_DIR)/pqc/VX_pqc_pkg.sv \
    $(SRC_DIR)/sim/$(TOP).sv
RTL_DEPS := $(wildcard $(RTL_DIR)/pqc/*.sv $(SRC_DIR)/sim/*)

.PHONY: all run run-agu clean
all: $(OBJ_DIR)/V$(TOP)

$(OBJ_DIR)/V$(TOP): $(RTL_DEPS) $(ROOT_DIR)/hw/VX_config.vh Makefile
	$(VERILATOR_PATH)/bin/verilator --binary --timing --assert -Wall -Wno-fatal \
	    -Wno-DECLFILENAME -Wno-REDEFMACRO --output-split 10000 -j $(THREADS) \
	    -DVX_CFG_XLEN=$(XLEN) -DVX_CFG_EXT_PQC_ENABLE \
	    -DVX_CFG_NUM_LSU_LANES=$(MEM_LANES) -DPQC_TOP_UNROLL=$(UNROLL) \
	    $(CONFIGS) $(RTL_INCLUDE) $(RTL_SOURCES) \
	    --top-module $(TOP) --Mdir $(OBJ_DIR)

run: all
	./$(OBJ_DIR)/V$(TOP) $(OPTS)

run-agu:
	$(MAKE) TOP=VX_pqc_agu_tb run

clean:
	rm -rf obj_VX_pqc_*
