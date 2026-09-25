ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

SRC_DIR := $(VORTEX_HOME)/hw/unittest/mem_to_axi
RTL_DIR := $(VORTEX_HOME)/hw/rtl
TOP := VX_mem_to_axi_tb
THREADS ?= 4
OBJ_DIR := obj_$(XLEN)
RTL_INCLUDE := -I$(ROOT_DIR)/hw -I$(ROOT_DIR)/sw -I$(RTL_DIR) -I$(RTL_DIR)/libs
RTL_DEPS := $(wildcard $(RTL_DIR)/libs/*.sv) $(SRC_DIR)/$(TOP).sv

.PHONY: all run clean
all: $(OBJ_DIR)/V$(TOP)

$(OBJ_DIR)/V$(TOP): $(RTL_DEPS) $(ROOT_DIR)/hw/VX_config.vh Makefile
	$(VERILATOR_PATH)/bin/verilator --binary --timing --assert -Wall \
	    -Wno-DECLFILENAME -Wno-REDEFMACRO -Wno-GENUNNAMED -Wno-UNUSEDSIGNAL \
	    -j $(THREADS) -DSIMULATION -DVX_CFG_XLEN=$(XLEN) \
	    $(RTL_INCLUDE) $(SRC_DIR)/$(TOP).sv \
	    --top-module $(TOP) --Mdir $(OBJ_DIR)

run: all
	./$(OBJ_DIR)/V$(TOP)

clean:
	rm -rf obj_*
