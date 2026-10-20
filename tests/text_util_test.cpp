#include "check.h"

#include "core/emoji.h"
#include "core/text_util.h"

using namespace TextUtil;

TEST(display_keeps_plain_ascii) {
    CHECK_EQ(toDisplay("Hello, mesh! 123"), std::string("Hello, mesh! 123"));
}

TEST(display_maps_known_emoji_to_codes) {
    CHECK_EQ(toDisplay("ok \xF0\x9F\x91\x8D"), std::string("ok :thumbsup:"));
    // Heart with variation selector, as phones send it.
    CHECK_EQ(toDisplay("\xE2\x9D\xA4\xEF\xB8\x8F"), std::string(":heart:"));
    // Thumbs up with a skin tone modifier.
    CHECK_EQ(toDisplay("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD"), std::string(":thumbsup:"));
}

TEST(display_folds_latin1_and_punctuation) {
    CHECK_EQ(toDisplay("caf\xC3\xA9 M\xC3\xBCnchen"), std::string("cafe Munchen"));
    CHECK_EQ(toDisplay("\xE2\x80\x9Cquote\xE2\x80\x9D \xE2\x80\x94 it\xE2\x80\x99s\xE2\x80\xA6"),
             std::string("\"quote\" - it's..."));
}

TEST(display_replaces_unknown_and_invalid) {
    CHECK_EQ(toDisplay("\xD0\x9F\xD1\x80\xD0\xB8"), std::string("???"));  // Cyrillic
    CHECK_EQ(toDisplay("a\xFF" "b"), std::string("a?b"));
    CHECK_EQ(toDisplay("trunc\xF0\x9F"), std::string("trunc??"));
    CHECK_EQ(toDisplay("tab\there\x01"), std::string("tab here"));
}

TEST(every_picker_emoji_round_trips_to_its_code) {
    for (int i = 0; i < kEmojiCount; ++i) {
        std::string shown = toDisplay(kEmojis[i].utf8);
        bool asciiEmoji = static_cast<unsigned char>(kEmojis[i].utf8[0]) < 0x80;
        if (asciiEmoji) CHECK_EQ(shown, std::string(kEmojis[i].utf8));
        else CHECK_EQ(shown, std::string(kEmojis[i].code));
    }
}

TEST(truncate_never_splits_a_code_point) {
    std::string s = "ab\xF0\x9F\x91\x8D";  // 2 + 4 bytes
    CHECK_EQ(truncateUtf8(s, 10), s);
    CHECK_EQ(truncateUtf8(s, 6), s);
    CHECK_EQ(truncateUtf8(s, 5), std::string("ab"));
    CHECK_EQ(truncateUtf8(s, 3), std::string("ab"));
    CHECK_EQ(truncateUtf8(s, 2), std::string("ab"));
    CHECK_EQ(truncateUtf8(s, 0), std::string(""));
}

TEST(wrap_breaks_on_words) {
    auto lines = wrap("the quick brown fox jumps", 10);
    CHECK_EQ(lines.size(), (size_t)3);
    CHECK_EQ(lines[0], std::string("the quick"));
    CHECK_EQ(lines[1], std::string("brown fox"));
    CHECK_EQ(lines[2], std::string("jumps"));
}

TEST(wrap_hard_splits_long_words_and_honours_newlines) {
    auto lines = wrap("abcdefghijkl x\nend", 5);
    CHECK_EQ(lines.size(), (size_t)4);
    CHECK_EQ(lines[0], std::string("abcde"));
    CHECK_EQ(lines[1], std::string("fghij"));
    CHECK_EQ(lines[2], std::string("kl x"));
    CHECK_EQ(lines[3], std::string("end"));
}

TEST(wrap_empty_text_is_one_empty_line) {
    auto lines = wrap("", 10);
    CHECK_EQ(lines.size(), (size_t)1);
    CHECK_EQ(lines[0], std::string(""));
}

TEST(ellipsize_cuts_with_dots) {
    CHECK_EQ(ellipsize("short", 10), std::string("short"));
    CHECK_EQ(ellipsize("a long node name", 8), std::string("a long.."));
}
