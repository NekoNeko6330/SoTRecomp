include ../sdk/mod.mk

PYTHON ?= python3

# ModOcarina's eye textures (13053ec), as C arrays
src/eye_textures.h: $(wildcard assets/*.png) ../sdk/png2c.py
	$(PYTHON) ../sdk/png2c.py $@ $(sort $(wildcard assets/*.png))

$(BUILD_DIR)/src/eyes.o: src/eye_textures.h
