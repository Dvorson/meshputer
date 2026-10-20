#!/usr/bin/env bash
# Native unit tests for the portable core (src/core) against the real
# nanopb-generated Meshtastic schema. No device needed.
set -euo pipefail
cd "$(dirname "$0")/.."

ENV=cardputer
NANOPB=".pio/libdeps/$ENV/Nanopb"
if [ ! -f "$NANOPB/pb.h" ]; then
    echo "Fetching library dependencies (one-time)..."
    pio pkg install -e "$ENV" >/dev/null
fi

OUT=build/tests
mkdir -p "$OUT"

CFLAGS=(-I"$NANOPB" -Ilib/meshtastic-protobuf/src -Isrc -Itests)
CXXFLAGS=(-std=c++17 -Wall -Wextra -g -fsanitize=address,undefined "${CFLAGS[@]}")

# nanopb + generated schema compile once; they rarely change.
objs=()
for src in "$NANOPB"/pb_common.c "$NANOPB"/pb_encode.c "$NANOPB"/pb_decode.c; do
    obj="$OUT/$(basename "$src" .c).o"
    [ "$obj" -nt "$src" ] || cc -std=c11 -g -fsanitize=address,undefined "${CFLAGS[@]}" -c "$src" -o "$obj"
    objs+=("$obj")
done
for src in lib/meshtastic-protobuf/src/meshtastic/*.pb.cpp; do
    # deviceonly needs a firmware-side callback; clients never use it.
    case "$src" in *deviceonly.pb.cpp) continue ;; esac
    obj="$OUT/$(basename "$src" .cpp).o"
    [ "$obj" -nt "$src" ] || c++ "${CXXFLAGS[@]}" -w -c "$src" -o "$obj"
    objs+=("$obj")
done

c++ "${CXXFLAGS[@]}" \
    tests/test_main.cpp tests/*_test.cpp \
    src/core/*.cpp \
    "${objs[@]}" \
    -o "$OUT/core_tests"

"$OUT/core_tests"
