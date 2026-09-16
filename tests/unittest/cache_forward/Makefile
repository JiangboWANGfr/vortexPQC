ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := cache_forward
SRC_DIR := $(VORTEX_HOME)/tests/unittest/$(PROJECT)
SIMX_DIR := $(VORTEX_HOME)/sim/simx

SRCS := $(SRC_DIR)/main.cpp $(SIMX_DIR)/mem/cache.cpp
CXXFLAGS += -I$(SIMX_DIR) -I$(SIMX_DIR)/mem -I$(SIMX_DIR)/amo -I$(SIM_COMMON_DIR)
CXXFLAGS += -DVX_CFG_XLEN=$(XLEN) -DVX_CFG_XLEN_$(XLEN)
LDFLAGS += -pthread

include ../common.mk
CXXFLAGS += -Wno-pedantic
