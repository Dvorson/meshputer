#pragma once

// Text helpers for a 6x8 ASCII font fed by UTF-8 from the mesh.

#include <cstddef>
#include <string>
#include <vector>

namespace TextUtil {

// UTF-8 -> printable ASCII for the built-in font.
// Known emoji become their picker code (":thumbsup:"), common Latin-1 letters
// and typographic punctuation are transliterated, anything else becomes '?'.
// Variation selectors, ZWJ and skin-tone modifiers are dropped.
std::string toDisplay(const std::string& utf8);

// Longest prefix of at most maxBytes that does not split a UTF-8 sequence.
std::string truncateUtf8(const std::string& utf8, size_t maxBytes);

// Greedy word wrap of display text into lines of at most width characters.
// Words longer than a line are hard-split. Newlines force a break.
std::vector<std::string> wrap(const std::string& text, size_t width);

// Fit text into width characters, ending with ".." when cut.
std::string ellipsize(const std::string& text, size_t width);

} // namespace TextUtil
