# Builds the Sands of Time: Recompiled mods into out/ (see README.md).

PYTHON ?= python3

CODE_MODS    := better_roll look_tracking fairy_text_1_0
TEXTURE_MODS := torch_texture

all: code_mods texture_mods

code_mods:
	$(foreach m,$(CODE_MODS),$(MAKE) -C $(m) && mkdir -p out && cp $(m)/build/*.nrm out/ &&) true

texture_mods:
	$(foreach m,$(TEXTURE_MODS),$(PYTHON) sdk/pack_mod.py $(m) out/sot_$(m).nrm &&) true

clean:
	$(foreach m,$(CODE_MODS),$(MAKE) -C $(m) clean &&) true
	rm -rf out

.PHONY: all code_mods texture_mods clean
