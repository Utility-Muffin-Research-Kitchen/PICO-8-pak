SHELL := /bin/sh
ROOT := $(CURDIR)
TOOLCHAIN_IMAGE ?= ghcr.io/utility-muffin-research-kitchen/mlp1-toolchain@sha256:66aac16fb8b07e663c9b4d66970f272df195a6eba98dfad8286eabbaa617faf9
PACKAGE := build/package/PICO8.pak

.PHONY: package-mlp1 check dist-pakrat clean
package-mlp1:
	mkdir -p $(PACKAGE)/bin
	cp pak/launch.sh pak/launch-cart.sh pak/pak.json $(PACKAGE)/
	cp pak/bin/pico8-launch $(PACKAGE)/bin/
	cp LICENSE README.md $(PACKAGE)/
	docker run --rm -v "$(ROOT):/workspace" -w /workspace "$(TOOLCHAIN_IMAGE)" sh -ec '\
	  aarch64-buildroot-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra src/wget.c $$(pkg-config --cflags --libs libcurl) -o $(PACKAGE)/bin/wget; \
	  aarch64-buildroot-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra src/message.c $$(pkg-config --cflags --libs sdl2 SDL2_ttf) -o $(PACKAGE)/bin/pico8-message; \
	  aarch64-buildroot-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra src/import.c $$(pkg-config --cflags --libs libpng openssl sqlite3) -lz -o $(PACKAGE)/bin/pico8-import'
	python3 scripts/check-package.py $(PACKAGE)

check:
	python3 scripts/check.py
	python3 scripts/check-import.py

# The deliberately impossible minimum keeps development artifacts out of a
# production store. Replace both metadata mirrors with the qualified Leaf
# release before distributing; do not invent an ungated first-version floor.
dist-pakrat: package-mlp1
	python3 -c 'import json; assert json.load(open("pak/pak.json"))["min_leaf_version"] != "9999.0.0", "Set the qualified minimum Leaf release before distribution"'
	mkdir -p build/dist
	cd build/package && zip -qr -X ../dist/PICO8.mlp1.pak.zip PICO8.pak

clean:
	rm -rf build
