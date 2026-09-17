SHELL := /bin/bash
PAPER := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD ?= $(abspath $(PAPER)/../../../build/dac_paper/latex)

.PHONY: all figures
all: figures
	mkdir -p $(BUILD)
	cd $(PAPER) && latexmk -pdf -interaction=nonstopmode -halt-on-error -outdir=$(BUILD) main.tex
	cp $(BUILD)/main.pdf $(PAPER)/vortex_pqc_dac_draft.pdf

figures:
	python3 $(PAPER)/generate_results.py
