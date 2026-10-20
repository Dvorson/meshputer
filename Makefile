# Meshputer developer commands. PORT is auto-detected; override with PORT=/dev/cu.usbmodemXXXX.

PIO  ?= pio
PORT ?= $(firstword $(wildcard /dev/cu.usbmodem*) $(wildcard /dev/ttyACM*))
TOOL  = uv run -q tools/meshputer.py $(if $(PORT),--port $(PORT))

.PHONY: help setup build flash monitor test console shot logs release site serve screenshots clean

help:
	@echo "make setup            install PlatformIO and the serial tool deps (needs uv)"
	@echo "make build            compile the firmware"
	@echo "make flash            build and flash over USB"
	@echo "make monitor          raw serial monitor (Ctrl+C to quit)"
	@echo "make test             native unit tests for src/core"
	@echo "make console CMD=...  console commands, e.g. CMD='status \"send ch0 hi\"'"
	@echo "make shot [OUT=f.png] save the device screen as PNG"
	@echo "make logs             stream device log lines"
	@echo "make screenshots      README screens from demo data (reboot the device after)"
	@echo "make release          single flashable image in dist/"
	@echo "make site             browser-installer site in build/site (preview: make serve)"

setup:
	uv tool install platformio
	uv run -q tools/meshputer.py --help >/dev/null

build:
	$(PIO) run

flash:
	$(PIO) run -t upload $(if $(PORT),--upload-port $(PORT))

monitor:
	$(PIO) device monitor $(if $(PORT),-p $(PORT))

test:
	bash tests/run_tests.sh

console:
	$(TOOL) run $(CMD)

OUT ?= screen.png
shot:
	$(TOOL) shot $(OUT)

logs:
	$(TOOL) logs

# Puts fictional demo data on the device (RAM only) and captures docs/screens.
screenshots:
	$(TOOL) screenshots docs/screens

VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
release:
	tools/merge_firmware.sh $(VERSION)

site:
	tools/build_site.sh $(VERSION) build/site

serve: site
	cd build/site && python3 -m http.server 8000

clean:
	$(PIO) run -t clean
	rm -rf build dist
