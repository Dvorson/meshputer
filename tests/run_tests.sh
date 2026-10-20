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

SECP=lib/secp256k1
CFLAGS=(-I"$NANOPB" -Ilib/meshtastic-protobuf/src -I"$SECP/include" -Isrc -Itests)
# Same configuration as lib/secp256k1/library.json uses for the device.
SECP_FLAGS=(-DENABLE_MODULE_ECDH=1 -DENABLE_MODULE_EXTRAKEYS=1 -DENABLE_MODULE_SCHNORRSIG=1
            -DECMULT_WINDOW_SIZE=4 -DCOMB_BLOCKS=11 -DCOMB_TEETH=6)
CXXFLAGS=(-std=c++17 -Wall -Wextra -g -fsanitize=address,undefined "${CFLAGS[@]}")

# nanopb + generated schema compile once; they rarely change.
objs=()
for src in "$NANOPB"/pb_common.c "$NANOPB"/pb_encode.c "$NANOPB"/pb_decode.c; do
    obj="$OUT/$(basename "$src" .c).o"
    [ "$obj" -nt "$src" ] || cc -std=c11 -g -fsanitize=address,undefined "${CFLAGS[@]}" -c "$src" -o "$obj"
    objs+=("$obj")
done
for src in "$SECP"/src/secp256k1.c "$SECP"/src/precomputed_ecmult.c "$SECP"/src/precomputed_ecmult_gen.c; do
    obj="$OUT/secp_$(basename "$src" .c).o"
    [ "$obj" -nt "$src" ] || cc -std=c11 -O2 -g -w "${SECP_FLAGS[@]}" -I"$SECP/include" -c "$src" -o "$obj"
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

# CLI over the Nostr core, used by `make interop` to cross-check other clients.
c++ "${CXXFLAGS[@]}" tests/interop/nostr_cli.cpp src/core/*.cpp "${objs[@]}" -o "$OUT/nostr_cli"

"$OUT/core_tests"
