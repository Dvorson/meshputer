#include "core/text_util.h"

#include <cstdint>
#include <cstring>

#include "core/emoji.h"

namespace TextUtil {

namespace {

// Base letters for U+00C0..U+00FF.
const char kLatin1Fold[] =
    "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"
    "aaaaaaaceeeeiiiidnooooo/ouuuuyty";

// Decodes one code point at s[i]; returns bytes consumed (0 = invalid lead).
size_t decodeUtf8(const std::string& s, size_t i, uint32_t& cp) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t len;
    if (c < 0x80) { cp = c; return 1; }
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
    else return 0;
    if (i + len > s.size()) return 0;
    for (size_t k = 1; k < len; ++k) {
        unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (cc & 0x3F);
    }
    return len;
}

bool isInvisible(uint32_t cp) {
    return cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D || cp == 0x200B ||
           (cp >= 0x1F3FB && cp <= 0x1F3FF);
}

// Returns the length of a known non-ASCII emoji at s[i], writing its code.
size_t matchEmoji(const std::string& s, size_t i, const char*& code) {
    for (int e = 0; e < kEmojiCount; ++e) {
        const char* u = kEmojis[e].utf8;
        if (static_cast<unsigned char>(u[0]) < 0x80) continue;
        size_t n = strlen(u);
        if (s.compare(i, n, u) == 0) {
            code = kEmojis[e].code;
            return n;
        }
    }
    return 0;
}

void appendFolded(std::string& out, uint32_t cp) {
    if (cp == '\n') { out += '\n'; return; }
    if (cp == '\t' || cp == 0xA0) { out += ' '; return; }
    if (cp < 0x20 || cp == 0x7F) return;
    if (cp < 0x80) { out += static_cast<char>(cp); return; }
    if (cp >= 0xC0 && cp <= 0xFF) { out += kLatin1Fold[cp - 0xC0]; return; }
    switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: out += '\''; return;
    case 0x201C: case 0x201D: case 0x201E: out += '"'; return;
    case 0x2013: case 0x2014: case 0x2212: out += '-'; return;
    case 0x2026: out += "..."; return;
    case 0x2022: out += '*'; return;
    default: break;
    }
    if (isInvisible(cp)) return;
    out += '?';
}

} // namespace

std::string toDisplay(const std::string& utf8) {
    std::string out;
    out.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        const char* code = nullptr;
        size_t n = matchEmoji(utf8, i, code);
        if (n > 0) {
            out += code;
            i += n;
            continue;
        }
        uint32_t cp = 0;
        n = decodeUtf8(utf8, i, cp);
        if (n == 0) {
            out += '?';
            i += 1;
            continue;
        }
        appendFolded(out, cp);
        i += n;
    }
    return out;
}

std::string truncateUtf8(const std::string& utf8, size_t maxBytes) {
    if (utf8.size() <= maxBytes) return utf8;
    size_t cut = maxBytes;
    // Back up over continuation bytes so the cut lands on a code point boundary.
    while (cut > 0 && (static_cast<unsigned char>(utf8[cut]) & 0xC0) == 0x80) --cut;
    return utf8.substr(0, cut);
}

std::vector<std::string> wrap(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    if (width == 0) return lines;

    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);

        std::string line;
        size_t pos = 0;
        while (pos < para.size()) {
            while (pos < para.size() && para[pos] == ' ') ++pos;
            if (pos >= para.size()) break;
            size_t end = para.find(' ', pos);
            if (end == std::string::npos) end = para.size();
            std::string word = para.substr(pos, end - pos);
            pos = end;

            while (word.size() > width) {
                if (!line.empty()) { lines.push_back(line); line.clear(); }
                lines.push_back(word.substr(0, width));
                word = word.substr(width);
            }
            if (word.empty()) continue;
            if (line.empty()) line = word;
            else if (line.size() + 1 + word.size() <= width) line += ' ' + word;
            else { lines.push_back(line); line = word; }
        }
        lines.push_back(line);

        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

std::string ellipsize(const std::string& text, size_t width) {
    if (text.size() <= width) return text;
    if (width < 2) return text.substr(0, width);
    return text.substr(0, width - 2) + "..";
}

} // namespace TextUtil
