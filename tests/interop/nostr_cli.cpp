// Tiny CLI over src/core/nostr for cross-checking against other Nostr
// implementations (tests/interop/interop.py drives it).
//   nostr_cli pub <sec_hex>
//   nostr_cli wrap <sec_hex> <recipient_pub_hex> <text>     -> gift wrap JSON
//   nostr_cli unwrap <sec_hex> < wrap.json                  -> "<sender_hex>\t<text>"

#include <cstdio>
#include <iostream>
#include <iterator>
#include <random>
#include <string>

#include "core/crypto.h"
#include "core/nostr.h"

static void osRandom(uint8_t* out, size_t len) {
    static std::random_device rd;
    for (size_t i = 0; i < len; ++i) out[i] = uint8_t(rd());
}

int main(int argc, char** argv) {
    Crypto::setRandom(osRandom);
    if (argc < 3) return 2;
    std::string cmd = argv[1];
    uint8_t sec[32];
    Nostr::Keys keys;
    if (!Crypto::fromHex(argv[2], sec, 32) || !Nostr::keysFromSecret(sec, keys)) return 3;

    if (cmd == "pub") {
        std::cout << keys.pubHex() << "\n";
        return 0;
    }
    if (cmd == "wrap" && argc >= 5) {
        uint8_t to[32];
        if (!Crypto::fromHex(argv[3], to, 32)) return 4;
        Nostr::Event wrap;
        if (!Nostr::giftWrap(keys, to, argv[4], 1792000000, wrap)) return 5;
        std::cout << Nostr::toJson(wrap) << "\n";
        return 0;
    }
    if (cmd == "unwrap") {
        std::string json((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
        Nostr::Event wrap;
        Nostr::DirectMessage dm;
        if (!Nostr::fromJson(json, wrap) || !Nostr::unwrap(keys, wrap, dm)) return 6;
        std::cout << dm.fromHex << "\t" << dm.text << "\n";
        return 0;
    }
    return 2;
}
