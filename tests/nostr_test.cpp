#include "check.h"

#include <cstring>
#include <fstream>
#include <random>
#include <sstream>

#include "core/crypto.h"
#include "core/json.h"
#include "core/nostr.h"

namespace {

void osRandom(uint8_t* out, size_t len) {
    static std::random_device rd;
    for (size_t i = 0; i < len; ++i) out[i] = uint8_t(rd());
}

struct UseOsRandom {
    UseOsRandom() { Crypto::setRandom(osRandom); }
} sUseOsRandom;

std::string hexOf(const uint8_t* p, size_t n) { return Crypto::toHex(p, n); }

void unhex(const std::string& h, uint8_t* out, size_t n) {
    bool ok = Crypto::fromHex(h, out, n);
    CHECK(ok);
}

const Json::Value& vectors() {
    static Json::Value v;
    static bool loaded = false;
    if (!loaded) {
        std::ifstream f("tests/vectors/nip44.vectors.json");
        std::stringstream ss;
        ss << f.rdbuf();
        bool ok = Json::parse(ss.str(), v);
        CHECK(ok);
        loaded = true;
    }
    return *v.get("v2");
}

} // namespace

// ---- primitives ----

TEST(sha256_known_answers) {
    uint8_t h[32];
    Crypto::sha256(reinterpret_cast<const uint8_t*>("abc"), 3, h);
    CHECK_EQ(hexOf(h, 32), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    Crypto::sha256(nullptr, 0, h);
    CHECK_EQ(hexOf(h, 32), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

TEST(base64_round_trip) {
    for (size_t n = 0; n < 20; ++n) {
        std::vector<uint8_t> in(n);
        for (size_t i = 0; i < n; ++i) in[i] = uint8_t(i * 37 + 5);
        Crypto::Bytes out;
        CHECK(Crypto::base64Decode(Crypto::base64Encode(in.data(), in.size()), out));
        CHECK(out == in);
    }
    Crypto::Bytes out;
    CHECK(!Crypto::base64Decode("abc", out));
    CHECK(!Crypto::base64Decode("ab!d", out));
}

TEST(json_parse_and_nip01_quoting) {
    Json::Value v;
    CHECK(Json::parse("[\"EVENT\",\"sub\",{\"kind\":1059,\"s\":\"a\\u00e9\\ud83d\\ude00\\n\"}]", v));
    CHECK(v.isArray());
    CHECK_EQ(v.items[0].str, std::string("EVENT"));
    CHECK_EQ(v.items[2].get("kind")->asInt(), (int64_t)1059);
    CHECK_EQ(v.items[2].get("s")->str, std::string("a\xC3\xA9\xF0\x9F\x98\x80\n"));
    CHECK_EQ(Json::quote("a\"b\\c\nd\te\xC3\xA9"), std::string("\"a\\\"b\\\\c\\nd\\te\xC3\xA9\""));
    CHECK(!Json::parse("[1,2", v));
    CHECK(!Json::parse("{\"a\":1} x", v));
}

// ---- NIP-19 ----

TEST(nip19_spec_examples) {
    uint8_t pub[32], sec[32];
    unhex("7e7e9c42a91bfef19fa929e5fda1b72e0ebc1a4c1141673e2794234d86addf4e", pub, 32);
    CHECK_EQ(Nostr::npub(pub), std::string("npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg"));
    unhex("67dea2ed018072d675f5415ecfaed7d2597555e202d85b3d65ea4e58d2d92ffa", sec, 32);
    CHECK_EQ(Nostr::nsec(sec), std::string("nsec1vl029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5"));

    std::string hrp;
    uint8_t back[32];
    CHECK(Nostr::decodeKey("npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg", hrp, back));
    CHECK_EQ(hrp, std::string("npub"));
    CHECK(memcmp(back, pub, 32) == 0);
    // One flipped character must fail the checksum.
    CHECK(!Nostr::decodeKey("npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptq", hrp, back));
}

// ---- keys, events, BIP-340 ----

TEST(bip340_vector_0_public_key) {
    uint8_t sec[32] = {0};
    sec[31] = 3;
    Nostr::Keys k;
    CHECK(Nostr::keysFromSecret(sec, k));
    CHECK_EQ(k.pubHex(), std::string("f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9"));
}

TEST(event_sign_verify_and_tamper) {
    Nostr::Keys k;
    CHECK(Nostr::generateKeys(k));
    Nostr::Event e;
    e.kind = 1;
    e.createdAt = 1792000000;
    e.tags = {{"t", "meshputer"}};
    e.content = "hello \"mesh\"\nline2 \xF0\x9F\x91\x8D";
    CHECK(Nostr::sign(e, k));
    CHECK(Nostr::verify(e));

    Nostr::Event parsed;
    CHECK(Nostr::fromJson(Nostr::toJson(e), parsed));
    CHECK(Nostr::verify(parsed));

    parsed.content += "!";
    CHECK(!Nostr::verify(parsed));
}

TEST(event_id_matches_nip01_serialization) {
    Nostr::Event e;
    e.pubkey = "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
    e.createdAt = 1;
    e.kind = 1;
    e.content = "a";
    CHECK_EQ(Nostr::serializeForId(e),
             std::string("[0,\"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798\",1,1,[],\"a\"]"));
}

// ---- NIP-44 official vectors ----

TEST(nip44_conversation_keys) {
    for (const auto& t : vectors().get("valid")->get("get_conversation_key")->items) {
        uint8_t sec[32], pub[32], key[32];
        unhex(t.get("sec1")->str, sec, 32);
        unhex(t.get("pub2")->str, pub, 32);
        CHECK(Nostr::conversationKey(sec, pub, key));
        CHECK_EQ(hexOf(key, 32), t.get("conversation_key")->str);
    }
    for (const auto& t : vectors().get("invalid")->get("get_conversation_key")->items) {
        uint8_t sec[32], pub[32], key[32];
        bool okSec = Crypto::fromHex(t.get("sec1")->str, sec, 32);
        bool okPub = Crypto::fromHex(t.get("pub2")->str, pub, 32);
        CHECK(!(okSec && okPub && Nostr::conversationKey(sec, pub, key)));
    }
}

TEST(nip44_padded_lengths) {
    for (const auto& t : vectors().get("valid")->get("calc_padded_len")->items) {
        CHECK_EQ(Nostr::paddedLength(size_t(t.items[0].asInt())), size_t(t.items[1].asInt()));
    }
}

TEST(nip44_encrypt_decrypt_exact) {
    for (const auto& t : vectors().get("valid")->get("encrypt_decrypt")->items) {
        uint8_t sec1[32], sec2[32], key[32], nonce[32];
        unhex(t.get("sec1")->str, sec1, 32);
        unhex(t.get("sec2")->str, sec2, 32);
        unhex(t.get("nonce")->str, nonce, 32);
        Nostr::Keys k2;
        CHECK(Nostr::keysFromSecret(sec2, k2));
        CHECK(Nostr::conversationKey(sec1, k2.pub, key));
        CHECK_EQ(hexOf(key, 32), t.get("conversation_key")->str);

        std::string payload, plain;
        CHECK(Nostr::encrypt(t.get("plaintext")->str, key, nonce, payload));
        CHECK_EQ(payload, t.get("payload")->str);
        CHECK(Nostr::decrypt(t.get("payload")->str, key, plain));
        CHECK_EQ(plain, t.get("plaintext")->str);
    }
}

TEST(nip44_long_messages) {
    for (const auto& t : vectors().get("valid")->get("encrypt_decrypt_long_msg")->items) {
        uint8_t key[32], nonce[32], h[32];
        unhex(t.get("conversation_key")->str, key, 32);
        unhex(t.get("nonce")->str, nonce, 32);
        std::string plain;
        for (int64_t i = 0; i < t.get("repeat")->asInt(); ++i) plain += t.get("pattern")->str;
        Crypto::sha256(reinterpret_cast<const uint8_t*>(plain.data()), plain.size(), h);
        CHECK_EQ(hexOf(h, 32), t.get("plaintext_sha256")->str);
        std::string payload;
        CHECK(Nostr::encrypt(plain, key, nonce, payload));
        Crypto::sha256(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), h);
        CHECK_EQ(hexOf(h, 32), t.get("payload_sha256")->str);
    }
}

TEST(nip44_rejects_invalid) {
    uint8_t key[32] = {1};
    std::string payload;
    for (const auto& len : vectors().get("invalid")->get("encrypt_msg_lengths")->items) {
        if (len.asInt() > 200000) continue;  // the 10 MB case adds nothing but runtime
        CHECK(!Nostr::encrypt(std::string(size_t(len.asInt()), 'a'), key, payload));
    }
    for (const auto& t : vectors().get("invalid")->get("decrypt")->items) {
        uint8_t k[32];
        unhex(t.get("conversation_key")->str, k, 32);
        std::string plain;
        bool ok = Nostr::decrypt(t.get("payload")->str, k, plain);
        if (ok) std::printf("    accepted invalid payload: %s\n", t.get("note")->str.c_str());
        CHECK(!ok);
    }
}

// ---- NIP-17 gift wrap ----

TEST(nip17_gift_wrap_round_trip) {
    Nostr::Keys alice, bob, eve;
    CHECK(Nostr::generateKeys(alice));
    CHECK(Nostr::generateKeys(bob));
    CHECK(Nostr::generateKeys(eve));

    Nostr::Event wrap;
    CHECK(Nostr::giftWrap(alice, bob.pub, "Made it to the ridge \xE2\x9B\xB0", 1792000000, wrap));
    CHECK_EQ(wrap.kind, 1059);
    CHECK(wrap.pubkey != alice.pubHex());  // sender hidden behind an ephemeral key
    CHECK(wrap.createdAt <= 1792000000 && wrap.createdAt > 1792000000 - 2 * 86400);
    CHECK_EQ(wrap.tags[0][1], bob.pubHex());

    Nostr::DirectMessage dm;
    CHECK(Nostr::unwrap(bob, wrap, dm));
    CHECK_EQ(dm.fromHex, alice.pubHex());
    CHECK_EQ(dm.text, std::string("Made it to the ridge \xE2\x9B\xB0"));
    CHECK_EQ(dm.createdAt, (int64_t)1792000000);

    // Only the recipient can open it, and any tampering is rejected.
    CHECK(!Nostr::unwrap(eve, wrap, dm));
    Nostr::Event tampered = wrap;
    tampered.content[10] = tampered.content[10] == 'A' ? 'B' : 'A';
    CHECK(!Nostr::unwrap(bob, tampered, dm));

    Nostr::Event viaJson;
    CHECK(Nostr::fromJson(Nostr::toJson(wrap), viaJson));
    CHECK(Nostr::unwrap(bob, viaJson, dm));
}
