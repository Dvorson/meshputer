# Meshputer developer commands. PORT is auto-detected; override with PORT=/dev/cu.usbmodemXXXX.
# BOARD selects the device: cardputer (default) or tab5, e.g. `make flash BOARD=tab5`.

PIO   ?= pio
BOARD ?= cardputer
PORT  ?= $(firstword $(wildcard /dev/cu.usbmodem*) $(wildcard /dev/ttyACM*))
TOOL  = uv run -q tools/meshputer.py $(if $(PORT),--port $(PORT))

.PHONY: help setup build flash monitor test interop console shot logs release site serve screenshots clean

help:
	@echo "make setup            install PlatformIO and the serial tool deps (needs uv)"
	@echo "make build            compile the firmware (BOARD=cardputer|tab5)"
	@echo "make flash            build and flash over USB (BOARD=cardputer|tab5)"
	@echo "make monitor          raw serial monitor (Ctrl+C to quit)"
	@echo "make test             native unit tests for src/core"
	@echo "make interop          check Nostr DMs interoperate with rust-nostr"
	@echo "make console CMD=...  console commands, e.g. CMD='status \"send ch0 hi\"'"
	@echo "make shot [OUT=f.png] save the device screen as PNG (Tab5: STEP=2 for half size)"
	@echo "make logs             stream device log lines"
	@echo "make screenshots      README screens from demo data (reboot the device after)"
	@echo "make release          single flashable images for both boards in dist/"
	@echo "make site             browser-installer site in build/site (preview: make serve)"

setup:
	uv tool install platformio
	uv run -q tools/meshputer.py --help >/dev/null

build:
	$(PIO) run -e $(BOARD)

flash:
	@test -n "$(PORT)" || { echo "No device found on USB: plug it in (or pass PORT=...)"; exit 1; }
	$(PIO) run -e $(BOARD) -t upload --upload-port $(PORT)

monitor:
	$(PIO) device monitor $(if $(PORT),-p $(PORT))

test:
	bash tests/run_tests.sh

# Cross-checks the Nostr core against rust-nostr (no network, no device).
interop: test
	uv run -q tests/interop/interop.py

console:
	$(TOOL) run $(CMD)

OUT ?= screen.png
STEP ?= 1
shot:
	$(TOOL) shot $(OUT) --step $(STEP)

logs:
	$(TOOL) logs

# Puts fictional demo data on the device (RAM only) and captures docs/screens.
screenshots:
	$(TOOL) screenshots docs/screens --board $(BOARD)

VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
release:
	tools/merge_firmware.sh $(VERSION) cardputer
	tools/merge_firmware.sh $(VERSION) tab5

site:
	tools/build_site.sh $(VERSION) build/site

serve: site
	cd build/site && python3 -m http.server 8000

clean:
	$(PIO) run -t clean
	rm -rf build dist
