# PQC tests reuse the regression harness verbatim -- same directory depth, same
# build/run targets, same CONFIGS handling. Duplicating it here would drift from
# upstream the first time regression/common.mk changes, so include it instead.
include $(VORTEX_HOME)/tests/regression/common.mk
