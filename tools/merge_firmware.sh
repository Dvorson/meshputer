#!/usr/bin/env bash
# Builds the firmware and merges bootloader + partitions + app into one image
# that flashes at 0x0 (M5Burner, Launcher "full firmware", esptool).
#   tools/merge_firmware.sh [version] [cardputer|tab5]  ->  dist/meshputer-<board>-<version>.bin
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${1:-$(git describe --tags --always --dirty 2>/dev/null || echo dev)}"
BOARD="${2:-cardputer}"
BUILD=".pio/build/$BOARD"
OUT="dist/meshputer-$BOARD-$VERSION.bin"

pio run -e "$BOARD"
mkdir -p dist

case "$BOARD" in
cardputer)
    BOOT_APP0="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    [ -f "$BOOT_APP0" ] || { echo "boot_app0.bin not found at $BOOT_APP0" >&2; exit 1; }
    pio pkg exec -p "tool-esptoolpy@~1.40501.0" -- esptool.py --chip esp32s3 merge_bin \
        -o "$OUT" \
        --flash_mode dio --flash_freq 80m --flash_size 8MB \
        0x0 "$BUILD/bootloader.bin" \
        0x8000 "$BUILD/partitions.bin" \
        0xe000 "$BOOT_APP0" \
        0x10000 "$BUILD/firmware.bin"
    ;;
tab5)
    # The P4 platform merges its own image: bootloader at 0x2000, then
    # partitions, otadata and the app at the usual offsets.
    cp "$BUILD/firmware.factory.bin" "$OUT"
    ;;
*)
    echo "unknown board: $BOARD (cardputer or tab5)" >&2
    exit 1
    ;;
esac
echo "$OUT"
