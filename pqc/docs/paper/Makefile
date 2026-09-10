ROOT := $(abspath ../../..)
BUILD := $(ROOT)/build/ieee_pqc
ASSETS := $(BUILD)/assets
OUTPUT := $(CURDIR)/vortex_pqc_ieee_draft.pdf

.PHONY: all
all:
	python3 generate_results.py --output $(ASSETS)
	TEXINPUTS="$(ASSETS):" latexmk -pdf -interaction=nonstopmode -halt-on-error -outdir=$(BUILD) main.tex
	mkdir -p $(dir $(OUTPUT))
	cp $(BUILD)/main.pdf $(OUTPUT)
