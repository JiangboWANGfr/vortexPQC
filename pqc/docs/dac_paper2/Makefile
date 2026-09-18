SHELL := /bin/bash
PAPER := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD ?= $(abspath $(PAPER)/../../../build/dac_paper2/latex)

.PHONY: all data
all: data
	mkdir -p $(BUILD)
	cd $(PAPER) && latexmk -pdf -interaction=nonstopmode -halt-on-error -outdir=$(BUILD) main.tex
	cp $(BUILD)/main.pdf $(PAPER)/vortex_pqc_dac2_draft.pdf

data:
	python3 $(PAPER)/generate_results.py
