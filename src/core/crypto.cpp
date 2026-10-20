#include "core/crypto.h"

#include <cstring>

namespace Crypto {

namespace {

const uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

inline uint32_t load32le(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

void defaultRandom(uint8_t* out, size_t len) {
    // Never used on the device (setRandom is called at boot); tests install their own.
    for (size_t i = 0; i < len; ++i) out[i] = 0;
}

RandomFn sRandom = defaultRandom;

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

// ---- SHA-256 ---------------------------------------------------------------

Sha256::Sha256() {
    static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(_h, init, sizeof(_h));
}

void Sha256::block(const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) |
               (uint32_t(p[4 * i + 2]) << 8) | uint32_t(p[4 * i + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = _h[0], b = _h[1], c = _h[2], d = _h[3], e = _h[4], f = _h[5], g = _h[6], h = _h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + kK[i] + w[i];
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    _h[0] += a; _h[1] += b; _h[2] += c; _h[3] += d; _h[4] += e; _h[5] += f; _h[6] += g; _h[7] += h;
}

void Sha256::update(const uint8_t* data, size_t len) {
    _total += len;
    while (len > 0) {
        size_t n = 64 - _bufLen;
        if (n > len) n = len;
        memcpy(_buf + _bufLen, data, n);
        _bufLen += n;
        data += n;
        len -= n;
        if (_bufLen == 64) {
            block(_buf);
            _bufLen = 0;
        }
    }
}

void Sha256::final(uint8_t out[32]) {
    uint64_t bits = _total * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (_bufLen != 56) update(&zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; ++i) len[i] = uint8_t(bits >> (56 - 8 * i));
    update(len, 8);
    for (int i = 0; i < 8; ++i) {
        out[4 * i] = uint8_t(_h[i] >> 24);
        out[4 * i + 1] = uint8_t(_h[i] >> 16);
        out[4 * i + 2] = uint8_t(_h[i] >> 8);
        out[4 * i + 3] = uint8_t(_h[i]);
    }
}

void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    Sha256 s;
    s.update(data, len);
    s.final(out);
}

// ---- HMAC / HKDF -------------------------------------------------------------

void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* a, size_t aLen,
                const uint8_t* b, size_t bLen, uint8_t out[32]) {
    uint8_t k[64] = {0};
    if (keyLen > 64) sha256(key, keyLen, k);
    else memcpy(k, key, keyLen);
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    uint8_t inner[32];
    Sha256 s1;
    s1.update(ipad, 64);
    if (aLen) s1.update(a, aLen);
    if (bLen) s1.update(b, bLen);
    s1.final(inner);
    Sha256 s2;
    s2.update(opad, 64);
    s2.update(inner, 32);
    s2.final(out);
}

void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen, uint8_t out[32]) {
    hmacSha256(key, keyLen, msg, msgLen, nullptr, 0, out);
}

void hkdfExtract(const uint8_t* salt, size_t saltLen, const uint8_t* ikm, size_t ikmLen, uint8_t prk[32]) {
    hmacSha256(salt, saltLen, ikm, ikmLen, prk);
}

void hkdfExpand(const uint8_t prk[32], const uint8_t* info, size_t infoLen, uint8_t* out, size_t outLen) {
    uint8_t t[32];
    size_t tLen = 0;
    uint8_t counter = 1;
    size_t done = 0;
    Bytes msg;
    while (done < outLen) {
        msg.assign(t, t + tLen);
        msg.insert(msg.end(), info, info + infoLen);
        msg.push_back(counter++);
        hmacSha256(prk, 32, msg.data(), msg.size(), t);
        tLen = 32;
        size_t n = outLen - done < 32 ? outLen - done : 32;
        memcpy(out + done, t, n);
        done += n;
    }
}

// ---- ChaCha20 --------------------------------------------------------------------

void chacha20(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t* data, size_t len) {
    uint32_t state[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
    for (int i = 0; i < 8; ++i) state[4 + i] = load32le(key + 4 * i);
    state[12] = counter;
    for (int i = 0; i < 3; ++i) state[13 + i] = load32le(nonce + 4 * i);

    uint8_t stream[64];
    while (len > 0) {
        uint32_t x[16];
        memcpy(x, state, sizeof(x));
        for (int i = 0; i < 10; ++i) {
#define QR(a, b, c, d)                       \
            x[a] += x[b]; x[d] = rotl(x[d] ^ x[a], 16); \
            x[c] += x[d]; x[b] = rotl(x[b] ^ x[c], 12); \
            x[a] += x[b]; x[d] = rotl(x[d] ^ x[a], 8);  \
            x[c] += x[d]; x[b] = rotl(x[b] ^ x[c], 7);
            QR(0, 4, 8, 12) QR(1, 5, 9, 13) QR(2, 6, 10, 14) QR(3, 7, 11, 15)
            QR(0, 5, 10, 15) QR(1, 6, 11, 12) QR(2, 7, 8, 13) QR(3, 4, 9, 14)
#undef QR
        }
        for (int i = 0; i < 16; ++i) {
            uint32_t v = x[i] + state[i];
            stream[4 * i] = uint8_t(v);
            stream[4 * i + 1] = uint8_t(v >> 8);
            stream[4 * i + 2] = uint8_t(v >> 16);
            stream[4 * i + 3] = uint8_t(v >> 24);
        }
        size_t n = len < 64 ? len : 64;
        for (size_t i = 0; i < n; ++i) data[i] ^= stream[i];
        data += n;
        len -= n;
        ++state[12];
    }
}

// ---- helpers ------------------------------------------------------------------------

bool equalConstTime(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}

std::string toHex(const uint8_t* data, size_t len) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += kHex[data[i] >> 4];
        out += kHex[data[i] & 0xF];
    }
    return out;
}

bool fromHex(const std::string& hex, uint8_t* out, size_t outLen) {
    if (hex.size() != outLen * 2) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < outLen; ++i) {
        int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = uint8_t(hi << 4 | lo);
    }
    return true;
}

std::string base64Encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = uint32_t(data[i]) << 16;
        if (i + 1 < len) v |= uint32_t(data[i + 1]) << 8;
        if (i + 2 < len) v |= data[i + 2];
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < len ? kB64[(v >> 6) & 63] : '=';
        out += i + 2 < len ? kB64[v & 63] : '=';
    }
    return out;
}

bool base64Decode(const std::string& in, Bytes& out) {
    out.clear();
    if (in.size() % 4 != 0) return false;
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    for (size_t i = 0; i < in.size(); i += 4) {
        int a = val(in[i]), b = val(in[i + 1]);
        if (a < 0 || b < 0) return false;
        bool last = i + 4 == in.size();
        int c = in[i + 2] == '=' && last ? -2 : val(in[i + 2]);
        int d = in[i + 3] == '=' && last ? -2 : val(in[i + 3]);
        if (c == -1 || d == -1 || (c == -2 && d != -2)) return false;
        uint32_t v = uint32_t(a) << 18 | uint32_t(b) << 12 | uint32_t(c < 0 ? 0 : c) << 6 | uint32_t(d < 0 ? 0 : d);
        out.push_back(uint8_t(v >> 16));
        if (c >= 0) out.push_back(uint8_t(v >> 8));
        if (d >= 0) out.push_back(uint8_t(v));
    }
    return true;
}

void setRandom(RandomFn fn) { sRandom = fn ? fn : defaultRandom; }
void randomBytes(uint8_t* out, size_t len) { sRandom(out, len); }

} // namespace Crypto
