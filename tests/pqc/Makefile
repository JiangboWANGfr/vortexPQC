ROOT_DIR := $(realpath ../..)
include $(ROOT_DIR)/config.mk

# --- master list ------------------------------------------------------
TESTS := \
    mlkem mlkem_microbench mlkem_profile \
    mldsa

BACKENDS := simx rtlsim aved

.PHONY: all clean $(addprefix run-,$(BACKENDS))

all:   $(addprefix build-,$(TESTS))
clean: $(addprefix clean-,$(TESTS))

define backend_rule
.PHONY: run-$(1)
run-$(1): $$(addprefix run-$(1)-,$$(TESTS))
endef
$(foreach b,$(BACKENDS),$(eval $(call backend_rule,$(b))))

.PHONY: build-% clean-%

build-%:
	$(MAKE) -C $* all

clean-%:
	$(MAKE) -C $* clean

# run-<backend>-<test>: split "<backend>-<test>" on the first dash.
# Subdir names must not contain '-'.
run-%:
	$(MAKE) -C $(word 2,$(subst -, ,$*)) run-$(word 1,$(subst -, ,$*))
