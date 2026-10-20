#include "core/nostr.h"

#include <cstring>

#include <secp256k1.h>
#include <secp256k1_ecdh.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_schnorrsig.h>

#include "core/crypto.h"

namespace Nostr {

namespace {

secp256k1_context* ctx() {
    static secp256k1_context* c = nullptr;
    if (!c) {
        c = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
        uint8_t seed[32];
        Crypto::randomBytes(seed, sizeof(seed));
        (void)secp256k1_context_randomize(c, seed);  // side-channel blinding
    }
    return c;
}

// ---- bech32 (NIP-19) ----

const char* kCharset = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

uint32_t polymod(const std::vector<uint8_t>& values) {
    static const uint32_t gen[5] = {0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3};
    uint32_t chk = 1;
    for (uint8_t v : values) {
        uint8_t top = chk >> 25;
        chk = ((chk & 0x1ffffff) << 5) ^ v;
        for (int i = 0; i < 5; ++i) if ((top >> i) & 1) chk ^= gen[i];
    }
    return chk;
}

std::vector<uint8_t> hrpExpand(const std::string& hrp) {
    std::vector<uint8_t> out;
    for (char c : hrp) out.push_back(uint8_t(c) >> 5);
    out.push_back(0);
    for (char c : hrp) out.push_back(uint8_t(c) & 31);
    return out;
}

bool convertBits(const uint8_t* in, size_t len, int from, int to, bool pad, std::vector<uint8_t>& out) {
    uint32_t acc = 0;
    int bits = 0;
    uint32_t maxv = (1u << to) - 1;
    for (size_t i = 0; i < len; ++i) {
        acc = (acc << from) | in[i];
        bits += from;
        while (bits >= to) {
            bits -= to;
            out.push_back(uint8_t((acc >> bits) & maxv));
        }
    }
    if (pad) {
        if (bits) out.push_back(uint8_t((acc << (to - bits)) & maxv));
    } else if (bits >= from || ((acc << (to - bits)) & maxv)) {
        return false;
    }
    return true;
}

std::string bech32Encode(const std::string& hrp, const uint8_t data32[32]) {
    std::vector<uint8_t> data;
    convertBits(data32, 32, 8, 5, true, data);
    std::vector<uint8_t> values = hrpExpand(hrp);
    values.insert(values.end(), data.begin(), data.end());
    values.insert(values.end(), 6, 0);
    uint32_t mod = polymod(values) ^ 1;
    std::string out = hrp + "1";
    for (uint8_t d : data) out += kCharset[d];
    for (int i = 0; i < 6; ++i) out += kCharset[(mod >> (5 * (5 - i))) & 31];
    return out;
}

bool bech32Decode(const std::string& text, std::string& hrp, uint8_t out32[32]) {
    size_t sep = text.rfind('1');
    if (sep == std::string::npos || sep == 0 || sep + 7 > text.size()) return false;
    hrp = text.substr(0, sep);
    std::vector<uint8_t> data;
    for (size_t i = sep + 1; i < text.size(); ++i) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        const char* p = strchr(kCharset, c);
        if (!p || c == '\0') return false;
        data.push_back(uint8_t(p - kCharset));
    }
    for (char& c : hrp) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    std::vector<uint8_t> values = hrpExpand(hrp);
    values.insert(values.end(), data.begin(), data.end());
    if (polymod(values) != 1) return false;
    data.resize(data.size() - 6);
    std::vector<uint8_t> bytes;
    if (!convertBits(data.data(), data.size(), 5, 8, false, bytes) || bytes.size() != 32) return false;
    memcpy(out32, bytes.data(), 32);
    return true;
}

int copyX(unsigned char* output, const unsigned char* x32, const unsigned char*, void*) {
    memcpy(output, x32, 32);
    return 1;
}

std::string tagsJson(const std::vector<std::vector<std::string>>& tags) {
    std::string out = "[";
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i) out += ',';
        out += '[';
        for (size_t j = 0; j < tags[i].size(); ++j) {
            if (j) out += ',';
            out += Json::quote(tags[i][j]);
        }
        out += ']';
    }
    return out + "]";
}

int64_t randomizedPast(int64_t now) {
    // NIP-59: tweak seal/wrap timestamps so they don't reveal when a message was sent.
    uint8_t r[4];
    Crypto::randomBytes(r, sizeof(r));
    uint32_t v = uint32_t(r[0]) | uint32_t(r[1]) << 8 | uint32_t(r[2]) << 16 | uint32_t(r[3]) << 24;
    return now - int64_t(v % (2 * 24 * 3600));
}

} // namespace

// ---- keys ----

std::string Keys::pubHex() const { return Crypto::toHex(pub, 32); }

bool keysFromSecret(const uint8_t secret[32], Keys& out) {
    secp256k1_keypair kp;
    if (!secp256k1_keypair_create(ctx(), &kp, secret)) return false;
    secp256k1_xonly_pubkey xonly;
    if (!secp256k1_keypair_xonly_pub(ctx(), &xonly, nullptr, &kp)) return false;
    secp256k1_xonly_pubkey_serialize(ctx(), out.pub, &xonly);
    memcpy(out.secret, secret, 32);
    return true;
}

bool generateKeys(Keys& out) {
    uint8_t secret[32];
    for (int attempt = 0; attempt < 8; ++attempt) {
        Crypto::randomBytes(secret, sizeof(secret));
        if (secp256k1_ec_seckey_verify(ctx(), secret) && keysFromSecret(secret, out)) return true;
    }
    return false;
}

std::string npub(const uint8_t pub[32]) { return bech32Encode("npub", pub); }
std::string nsec(const uint8_t secret[32]) { return bech32Encode("nsec", secret); }

bool decodeKey(const std::string& text, std::string& hrp, uint8_t out[32]) {
    if (text.size() == 64 && Crypto::fromHex(text, out, 32)) {
        hrp = "hex";
        return true;
    }
    return bech32Decode(text, hrp, out);
}

// ---- events ----

std::string serializeForId(const Event& e) {
    return "[0," + Json::quote(e.pubkey) + "," + std::to_string(e.createdAt) + "," +
           std::to_string(e.kind) + "," + tagsJson(e.tags) + "," + Json::quote(e.content) + "]";
}

void computeId(Event& e) {
    std::string s = serializeForId(e);
    uint8_t h[32];
    Crypto::sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size(), h);
    e.id = Crypto::toHex(h, 32);
}

bool sign(Event& e, const Keys& keys) {
    e.pubkey = keys.pubHex();
    computeId(e);
    uint8_t id[32], aux[32], sig[64];
    Crypto::fromHex(e.id, id, 32);
    Crypto::randomBytes(aux, sizeof(aux));
    secp256k1_keypair kp;
    if (!secp256k1_keypair_create(ctx(), &kp, keys.secret)) return false;
    if (!secp256k1_schnorrsig_sign32(ctx(), sig, id, &kp, aux)) return false;
    e.sig = Crypto::toHex(sig, 64);
    return true;
}

bool verify(const Event& e) {
    Event copy = e;
    computeId(copy);
    if (copy.id != e.id) return false;
    uint8_t id[32], sig[64], pub[32];
    if (!Crypto::fromHex(e.id, id, 32) || !Crypto::fromHex(e.sig, sig, 64) ||
        !Crypto::fromHex(e.pubkey, pub, 32)) return false;
    secp256k1_xonly_pubkey xonly;
    if (!secp256k1_xonly_pubkey_parse(ctx(), &xonly, pub)) return false;
    return secp256k1_schnorrsig_verify(ctx(), sig, id, 32, &xonly) == 1;
}

std::string toJson(const Event& e) {
    std::string out = "{";
    if (!e.id.empty()) out += "\"id\":" + Json::quote(e.id) + ",";
    out += "\"pubkey\":" + Json::quote(e.pubkey) + ",\"created_at\":" + std::to_string(e.createdAt) +
           ",\"kind\":" + std::to_string(e.kind) + ",\"tags\":" + tagsJson(e.tags) +
           ",\"content\":" + Json::quote(e.content);
    if (!e.sig.empty()) out += ",\"sig\":" + Json::quote(e.sig);
    return out + "}";
}

bool fromJson(const Json::Value& v, Event& e) {
    e = Event();
    if (!v.isObject()) return false;
    const Json::Value* pubkey = v.get("pubkey");
    const Json::Value* created = v.get("created_at");
    const Json::Value* kind = v.get("kind");
    const Json::Value* tags = v.get("tags");
    const Json::Value* content = v.get("content");
    if (!pubkey || !pubkey->isString() || !created || !created->isNumber() || !kind ||
        !kind->isNumber() || !content || !content->isString()) return false;
    e.pubkey = pubkey->str;
    e.createdAt = created->asInt();
    e.kind = int(kind->asInt());
    e.content = content->str;
    if (const Json::Value* id = v.get("id")) if (id->isString()) e.id = id->str;
    if (const Json::Value* sig = v.get("sig")) if (sig->isString()) e.sig = sig->str;
    if (tags && tags->isArray()) {
        for (const auto& t : tags->items) {
            if (!t.isArray()) return false;
            std::vector<std::string> tag;
            for (const auto& part : t.items) {
                if (!part.isString()) return false;
                tag.push_back(part.str);
            }
            e.tags.push_back(tag);
        }
    }
    return true;
}

bool fromJson(const std::string& text, Event& e) {
    Json::Value v;
    return Json::parse(text, v) && fromJson(v, e);
}

// ---- NIP-44 v2 ----

bool conversationKey(const uint8_t mySecret[32], const uint8_t theirPub[32], uint8_t out[32]) {
    uint8_t compressed[33];
    compressed[0] = 0x02;  // x-only keys always mean the even-y point
    memcpy(compressed + 1, theirPub, 32);
    secp256k1_pubkey pub;
    if (!secp256k1_ec_pubkey_parse(ctx(), &pub, compressed, sizeof(compressed))) return false;
    uint8_t shared[32];
    if (!secp256k1_ecdh(ctx(), shared, &pub, mySecret, copyX, nullptr)) return false;
    static const char salt[] = "nip44-v2";
    Crypto::hkdfExtract(reinterpret_cast<const uint8_t*>(salt), sizeof(salt) - 1, shared, 32, out);
    return true;
}

size_t paddedLength(size_t len) {
    if (len <= 32) return 32;
    size_t nextPower = 1;
    while (nextPower < len) nextPower <<= 1;  // == 1 << (floor(log2(len - 1)) + 1)
    size_t chunk = nextPower <= 256 ? 32 : nextPower / 8;
    return chunk * ((len - 1) / chunk + 1);
}

namespace {
void messageKeys(const uint8_t convKey[32], const uint8_t nonce[32], uint8_t chachaKey[32],
                 uint8_t chachaNonce[12], uint8_t hmacKey[32]) {
    uint8_t keys[76];
    Crypto::hkdfExpand(convKey, nonce, 32, keys, sizeof(keys));
    memcpy(chachaKey, keys, 32);
    memcpy(chachaNonce, keys + 32, 12);
    memcpy(hmacKey, keys + 44, 32);
}
} // namespace

bool encrypt(const std::string& plaintext, const uint8_t convKey[32], const uint8_t nonce[32],
             std::string& payload) {
    size_t len = plaintext.size();
    if (len < 1 || len > 65535) return false;
    uint8_t chachaKey[32], chachaNonce[12], hmacKey[32];
    messageKeys(convKey, nonce, chachaKey, chachaNonce, hmacKey);

    Crypto::Bytes padded(2 + paddedLength(len), 0);
    padded[0] = uint8_t(len >> 8);
    padded[1] = uint8_t(len);
    memcpy(padded.data() + 2, plaintext.data(), len);
    Crypto::chacha20(chachaKey, chachaNonce, 0, padded.data(), padded.size());

    uint8_t mac[32];
    Crypto::hmacSha256(hmacKey, 32, nonce, 32, padded.data(), padded.size(), mac);

    Crypto::Bytes out;
    out.reserve(1 + 32 + padded.size() + 32);
    out.push_back(2);
    out.insert(out.end(), nonce, nonce + 32);
    out.insert(out.end(), padded.begin(), padded.end());
    out.insert(out.end(), mac, mac + 32);
    payload = Crypto::base64Encode(out.data(), out.size());
    return true;
}

bool encrypt(const std::string& plaintext, const uint8_t convKey[32], std::string& payload) {
    uint8_t nonce[32];
    Crypto::randomBytes(nonce, sizeof(nonce));
    return encrypt(plaintext, convKey, nonce, payload);
}

bool decrypt(const std::string& payload, const uint8_t convKey[32], std::string& plaintext) {
    if (payload.empty() || payload[0] == '#') return false;
    if (payload.size() < 132 || payload.size() > 87472) return false;
    Crypto::Bytes d;
    if (!Crypto::base64Decode(payload, d)) return false;
    if (d.size() < 99 || d.size() > 65603 || d[0] != 2) return false;

    const uint8_t* nonce = d.data() + 1;
    const uint8_t* mac = d.data() + d.size() - 32;
    size_t ctLen = d.size() - 1 - 32 - 32;
    Crypto::Bytes ct(d.begin() + 33, d.begin() + 33 + ctLen);

    uint8_t chachaKey[32], chachaNonce[12], hmacKey[32];
    messageKeys(convKey, nonce, chachaKey, chachaNonce, hmacKey);
    uint8_t expected[32];
    Crypto::hmacSha256(hmacKey, 32, nonce, 32, ct.data(), ct.size(), expected);
    if (!Crypto::equalConstTime(expected, mac, 32)) return false;

    Crypto::chacha20(chachaKey, chachaNonce, 0, ct.data(), ct.size());
    if (ct.size() < 2) return false;
    size_t len = size_t(ct[0]) << 8 | ct[1];
    if (len < 1 || ct.size() != 2 + paddedLength(len)) return false;
    plaintext.assign(reinterpret_cast<const char*>(ct.data() + 2), len);
    return true;
}

// ---- NIP-17 / NIP-59 ----

bool giftWrap(const Keys& sender, const uint8_t recipientPub[32], const std::string& text,
              int64_t now, Event& wrap) {
    std::string recipientHex = Crypto::toHex(recipientPub, 32);

    Event rumor;  // unsigned on purpose: deniable if leaked
    rumor.pubkey = sender.pubHex();
    rumor.createdAt = now;
    rumor.kind = kKindChat;
    rumor.tags = {{"p", recipientHex}};
    rumor.content = text;
    computeId(rumor);

    uint8_t key[32];
    Event seal;
    seal.kind = kKindSeal;
    seal.createdAt = randomizedPast(now);
    if (!conversationKey(sender.secret, recipientPub, key) ||
        !encrypt(toJson(rumor), key, seal.content) || !sign(seal, sender)) return false;

    Keys ephemeral;
    if (!generateKeys(ephemeral)) return false;
    wrap = Event();
    wrap.kind = kKindGiftWrap;
    wrap.createdAt = randomizedPast(now);
    wrap.tags = {{"p", recipientHex}};
    if (!conversationKey(ephemeral.secret, recipientPub, key) ||
        !encrypt(toJson(seal), key, wrap.content) || !sign(wrap, ephemeral)) return false;
    return true;
}

bool unwrap(const Keys& me, const Event& wrap, DirectMessage& out) {
    if (wrap.kind != kKindGiftWrap || !verify(wrap)) return false;

    uint8_t wrapPub[32], key[32];
    std::string sealJson;
    if (!Crypto::fromHex(wrap.pubkey, wrapPub, 32) || !conversationKey(me.secret, wrapPub, key) ||
        !decrypt(wrap.content, key, sealJson)) return false;
    Event seal;
    if (!fromJson(sealJson, seal) || seal.kind != kKindSeal || !verify(seal)) return false;

    uint8_t sealPub[32];
    std::string rumorJson;
    if (!Crypto::fromHex(seal.pubkey, sealPub, 32) || !conversationKey(me.secret, sealPub, key) ||
        !decrypt(seal.content, key, rumorJson)) return false;
    Event rumor;
    if (!fromJson(rumorJson, rumor) || rumor.kind != kKindChat) return false;
    // The seal is signed by the sender; the rumor must claim the same author.
    if (rumor.pubkey != seal.pubkey) return false;

    out.fromHex = rumor.pubkey;
    out.text = rumor.content;
    out.createdAt = rumor.createdAt;
    out.rumorId = rumor.id;
    return true;
}

} // namespace Nostr
