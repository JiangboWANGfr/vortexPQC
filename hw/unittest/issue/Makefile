ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := issue

RTL_DIR := $(VORTEX_HOME)/hw/rtl
DPI_DIR := $(VORTEX_HOME)/hw/dpi

SRC_DIR := $(VORTEX_HOME)/hw/unittest/$(PROJECT)

CXXFLAGS := -I$(SRC_DIR) -I$(VORTEX_HOME)/hw/unittest/common -I$(SW_COMMON_DIR)
CXXFLAGS += -I$(ROOT_DIR)/sw -I$(ROOT_DIR)/hw

SRCS += $(SRC_DIR)/main.cpp

DBG_TRACE_FLAGS := -DDBG_TRACE_CACHE

RTL_PKGS := $(RTL_DIR)/VX_gpu_pkg.sv

RTL_INCLUDE := -I$(ROOT_DIR)/sw -I$(ROOT_DIR)/hw -I$(RTL_DIR) -I$(DPI_DIR) -I$(RTL_DIR)/libs
RTL_INCLUDE += -I$(RTL_DIR)/interfaces -I$(RTL_DIR)/mem -I$(RTL_DIR)/vm -I$(RTL_DIR)/core -I$(SRC_DIR)

TOP := VX_issue_top

include ../common.mk

.PHONY: run-watchdog
run-watchdog:
	$(VERILATOR_PATH)/bin/verilator --binary --timing --assert -Wall -Wno-fatal \
	    -Wno-DECLFILENAME -Wno-REDEFMACRO -j $(THREADS) -DSIMULATION \
	    -DVX_CFG_XLEN=$(XLEN) -DVX_CFG_NUM_WARPS=8 -DVX_DBG_STALL_TIMEOUT=64 \
	    $(RTL_INCLUDE) $(RTL_DIR)/VX_gpu_pkg.sv \
	    $(SRC_DIR)/sim/VX_scoreboard_watchdog_tb.sv \
	    --top-module VX_scoreboard_watchdog_tb --Mdir obj_watchdog
	python3 $(SRC_DIR)/sim/check_watchdog.py ./obj_watchdog/VVX_scoreboard_watchdog_tb
