#pragma once

// Nostr for a small device: keys (secp256k1 / BIP-340), NIP-19 npub/nsec,
// NIP-01 events, NIP-44 v2 encryption and NIP-17 private DMs (NIP-59 gift
// wraps). Portable C++ over libsecp256k1; unit-tested natively.

#include <cstdint>
#include <string>
#include <vector>

#include "core/json.h"

namespace Nostr {

struct Keys {
    uint8_t secret[32] = {0};
    uint8_t pub[32] = {0};  // x-only public key
    std::string pubHex() const;
};

bool keysFromSecret(const uint8_t secret[32], Keys& out);
bool generateKeys(Keys& out);  // uses Crypto::randomBytes

// NIP-19
std::string npub(const uint8_t pub[32]);
std::string nsec(const uint8_t secret[32]);
// Decodes an npub/nsec (hrp out) or 64-char hex (hrp = "hex") into 32 bytes.
bool decodeKey(const std::string& text, std::string& hrp, uint8_t out[32]);

// NIP-01
struct Event {
    std::string id;
    std::string pubkey;
    int64_t createdAt = 0;
    int kind = 0;
    std::vector<std::vector<std::string>> tags;
    std::string content;
    std::string sig;
};

std::string serializeForId(const Event& e);
void computeId(Event& e);
bool sign(Event& e, const Keys& keys);  // sets pubkey, id, sig
bool verify(const Event& e);            // checks id and signature
std::string toJson(const Event& e);
bool fromJson(const Json::Value& v, Event& e);
bool fromJson(const std::string& text, Event& e);

// NIP-44 v2
bool conversationKey(const uint8_t mySecret[32], const uint8_t theirPub[32], uint8_t out[32]);
size_t paddedLength(size_t len);
bool encrypt(const std::string& plaintext, const uint8_t convKey[32], const uint8_t nonce[32],
             std::string& payload);
bool encrypt(const std::string& plaintext, const uint8_t convKey[32], std::string& payload);
bool decrypt(const std::string& payload, const uint8_t convKey[32], std::string& plaintext);

// NIP-17 private direct messages (kind 14 rumor, kind 13 seal, kind 1059 gift wrap)
constexpr int kKindGiftWrap = 1059;
constexpr int kKindSeal = 13;
constexpr int kKindChat = 14;

struct DirectMessage {
    std::string fromHex;   // sender's public key
    std::string text;
    int64_t createdAt = 0;
    std::string rumorId;
};

bool giftWrap(const Keys& sender, const uint8_t recipientPub[32], const std::string& text,
              int64_t now, Event& wrap);
bool unwrap(const Keys& me, const Event& wrap, DirectMessage& out);

} // namespace Nostr
