ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

SRC_DIR := $(VORTEX_HOME)/hw/unittest/ksg25
RTL_DIR := $(VORTEX_HOME)/hw/rtl
TOP := VX_alu_ksg25_tb
OBJ_DIR := obj_dir
THREADS ?= 1

override CONFIGS += -DVX_CFG_EXT_KSG25_ENABLE -DVX_CFG_NUM_WARPS=8 \
    -DVX_CFG_NUM_THREADS=32 -DVX_CFG_SIMD_WIDTH=32 -DVX_CFG_NUM_ALU_LANES=32
XCONFIGS := $(shell python3 $(ROOT_DIR)/ci/gen_config.py --config=$(VORTEX_HOME)/VX_config.toml --cflags='$(CONFIGS) -DVX_CFG_XLEN=$(XLEN)')

RTL_INCLUDE := -I$(ROOT_DIR)/hw -I$(ROOT_DIR)/sw -I$(RTL_DIR) \
    -I$(RTL_DIR)/libs -I$(RTL_DIR)/interfaces -I$(RTL_DIR)/core
RTL_SOURCES := $(RTL_DIR)/VX_gpu_pkg.sv $(SRC_DIR)/sim/$(TOP).sv
RTL_DEPS := $(RTL_DIR)/core/VX_alu_ksg25.sv \
    $(wildcard $(RTL_DIR)/libs/*.sv $(RTL_DIR)/interfaces/*.sv $(RTL_DIR)/*.vh)

.PHONY: all run clean
all: $(OBJ_DIR)/V$(TOP)

$(OBJ_DIR)/V$(TOP): $(RTL_SOURCES) $(RTL_DEPS) $(ROOT_DIR)/hw/VX_config.vh Makefile
	$(VERILATOR_PATH)/bin/verilator --binary --timing --assert -Wall \
	    -Wno-DECLFILENAME -Wno-REDEFMACRO -Wno-GENUNNAMED -Wno-UNOPTFLAT \
	    ../verilator.vlt -j $(THREADS) -DSIMULATION $(XCONFIGS) \
	    $(RTL_INCLUDE) $(RTL_SOURCES) \
	    --top-module $(TOP) --Mdir $(OBJ_DIR)

run: all
	./$(OBJ_DIR)/V$(TOP)
	python3 $(SRC_DIR)/sim/check_contract.py ./$(OBJ_DIR)/V$(TOP)

clean:
	rm -rf $(OBJ_DIR)
