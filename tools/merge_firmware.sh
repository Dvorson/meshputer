#!/usr/bin/env bash
# Builds the firmware and merges bootloader + partitions + app into one image
# that flashes at 0x0 (web installer, M5Burner, Launcher "full firmware").
#   tools/merge_firmware.sh [version]   ->  dist/meshputer-<version>.bin
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${1:-$(git describe --tags --always --dirty 2>/dev/null || echo dev)}"
ENV=cardputer
BUILD=".pio/build/$ENV"
PIO_HOME="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
BOOT_APP0="$PIO_HOME/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

pio run -e "$ENV"
[ -f "$BOOT_APP0" ] || { echo "boot_app0.bin not found at $BOOT_APP0" >&2; exit 1; }

mkdir -p dist
OUT="dist/meshputer-$VERSION.bin"
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 merge_bin \
    -o "$OUT" \
    --flash_mode dio --flash_freq 80m --flash_size 8MB \
    0x0 "$BUILD/bootloader.bin" \
    0x8000 "$BUILD/partitions.bin" \
    0xe000 "$BOOT_APP0" \
    0x10000 "$BUILD/firmware.bin"
echo "$OUT"
