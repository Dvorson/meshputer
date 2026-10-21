#!/usr/bin/env bash
# Assembles the GitHub Pages site with the browser installer:
#   tools/build_site.sh <version> <out-dir>
# One manifest covers both boards; the installer picks the build for the chip
# it finds (ESP32-S3: Cardputer, ESP32-P4: Tab5). It flashes the parts at
# their own offsets, so updating keeps the NVS partition (saved radio,
# pairing, settings). The merged images are offered as downloads for
# M5Burner / Launcher / esptool at 0x0.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${1:?version}"
OUT="${2:?output dir}"
BOOT_APP0="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

rm -rf "$OUT"
mkdir -p "$OUT/screens"
cp web/index.html "$OUT/"
cp docs/screens/*.png "$OUT/screens/"

for BOARD in cardputer tab5; do
    tools/merge_firmware.sh "$VERSION" "$BOARD" >/dev/null
    BUILD=".pio/build/$BOARD"
    mkdir -p "$OUT/firmware/$BOARD"
    cp "$BUILD/bootloader.bin" "$BUILD/partitions.bin" "$BUILD/firmware.bin" "$BOOT_APP0" "$OUT/firmware/$BOARD/"
    cp "dist/meshputer-$BOARD-$VERSION.bin" "$OUT/firmware/meshputer-$BOARD.bin"
done

cat > "$OUT/manifest.json" <<EOF
{
  "name": "Meshputer",
  "version": "$VERSION",
  "new_install_prompt_erase": false,
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "parts": [
        { "path": "firmware/cardputer/bootloader.bin", "offset": 0 },
        { "path": "firmware/cardputer/partitions.bin", "offset": 32768 },
        { "path": "firmware/cardputer/boot_app0.bin", "offset": 57344 },
        { "path": "firmware/cardputer/firmware.bin", "offset": 65536 }
      ]
    },
    {
      "chipFamily": "ESP32-P4",
      "parts": [
        { "path": "firmware/tab5/bootloader.bin", "offset": 8192 },
        { "path": "firmware/tab5/partitions.bin", "offset": 32768 },
        { "path": "firmware/tab5/boot_app0.bin", "offset": 57344 },
        { "path": "firmware/tab5/firmware.bin", "offset": 65536 }
      ]
    }
  ]
}
EOF
sed -i.bak "s/{{VERSION}}/$VERSION/g" "$OUT/index.html" && rm "$OUT/index.html.bak"
echo "$OUT"
