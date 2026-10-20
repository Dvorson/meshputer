#!/usr/bin/env bash
# Assembles the GitHub Pages site with the browser installer:
#   tools/build_site.sh <version> <out-dir>
# The installer flashes the four parts at their own offsets, so updating keeps
# the NVS partition (saved radio, pairing, settings). The merged image is
# offered as a download for M5Burner / Launcher / esptool at 0x0.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${1:?version}"
OUT="${2:?output dir}"
BUILD=.pio/build/cardputer
BOOT_APP0="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

tools/merge_firmware.sh "$VERSION" >/dev/null

rm -rf "$OUT"
mkdir -p "$OUT/firmware" "$OUT/screens"
cp web/index.html "$OUT/"
cp docs/screens/*.png "$OUT/screens/"
cp "$BUILD/bootloader.bin" "$BUILD/partitions.bin" "$BUILD/firmware.bin" "$BOOT_APP0" "$OUT/firmware/"
cp "dist/meshputer-$VERSION.bin" "$OUT/firmware/meshputer-merged.bin"

cat > "$OUT/manifest.json" <<EOF
{
  "name": "Meshputer",
  "version": "$VERSION",
  "new_install_prompt_erase": false,
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "parts": [
        { "path": "firmware/bootloader.bin", "offset": 0 },
        { "path": "firmware/partitions.bin", "offset": 32768 },
        { "path": "firmware/boot_app0.bin", "offset": 57344 },
        { "path": "firmware/firmware.bin", "offset": 65536 }
      ]
    }
  ]
}
EOF
sed -i.bak "s/{{VERSION}}/$VERSION/g" "$OUT/index.html" && rm "$OUT/index.html.bak"
echo "$OUT"
