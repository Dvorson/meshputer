#pragma once

// Small, portable crypto primitives used by Nostr (NIP-01 ids, NIP-44 v2):
// SHA-256, HMAC-SHA256, HKDF, ChaCha20 (RFC 8439), base64, hex.
// Verified against the official NIP-44 test vectors in tests/.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Crypto {

using Bytes = std::vector<uint8_t>;

struct Sha256 {
    Sha256();
    void update(const uint8_t* data, size_t len);
    void final(uint8_t out[32]);

private:
    void block(const uint8_t* p);
    uint32_t _h[8];
    uint8_t _buf[64];
    size_t _bufLen = 0;
    uint64_t _total = 0;
};

void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen, uint8_t out[32]);
// HMAC over two concatenated parts without copying them together.
void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* a, size_t aLen,
                const uint8_t* b, size_t bLen, uint8_t out[32]);
void hkdfExtract(const uint8_t* salt, size_t saltLen, const uint8_t* ikm, size_t ikmLen, uint8_t prk[32]);
void hkdfExpand(const uint8_t prk[32], const uint8_t* info, size_t infoLen, uint8_t* out, size_t outLen);

// XORs the ChaCha20 keystream (96-bit nonce, 32-bit counter) into data.
void chacha20(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t* data, size_t len);

bool equalConstTime(const uint8_t* a, const uint8_t* b, size_t len);

std::string toHex(const uint8_t* data, size_t len);
bool fromHex(const std::string& hex, uint8_t* out, size_t outLen);
std::string base64Encode(const uint8_t* data, size_t len);
bool base64Decode(const std::string& in, Bytes& out);

// Randomness comes from the platform (esp_random on the device).
using RandomFn = void (*)(uint8_t* out, size_t len);
void setRandom(RandomFn fn);
void randomBytes(uint8_t* out, size_t len);

} // namespace Crypto
