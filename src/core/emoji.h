#pragma once

#include <cstdint>
#include <cstring>

// const data already lives in flash on ESP32; PROGMEM is only a marker here.
#ifndef PROGMEM
#define PROGMEM
#endif

// 8x8 monochrome emoji bitmaps stored in PROGMEM
// Each emoji is 8 bytes (1 byte per row, MSB = leftmost pixel)

struct EmojiDef {
    const char* code;      // e.g. ":smile:" — how the emoji is shown on screen
    const char* utf8;      // what goes over the mesh, so phone apps render it
    const uint8_t* bitmap; // 8 bytes in PROGMEM
    const char* category;  // "Smileys", "Hands", etc.
};

// Emoji bitmaps (8x8, 1 bit per pixel, MSB first)
// 50+ emoji across categories

// === Smileys ===
static const uint8_t PROGMEM emo_smile[] =    {0x3C,0x42,0xA5,0x81,0xA5,0x99,0x42,0x3C};
static const uint8_t PROGMEM emo_grin[] =      {0x3C,0x42,0xA5,0x81,0xBD,0x99,0x42,0x3C};
static const uint8_t PROGMEM emo_joy[] =       {0x3C,0x42,0xE5,0xC1,0xBD,0x99,0x42,0x3C};
static const uint8_t PROGMEM emo_wink[] =      {0x3C,0x42,0x25,0x81,0xA5,0x99,0x42,0x3C};
static const uint8_t PROGMEM emo_cool[] =      {0x3C,0x42,0xFF,0x81,0xA5,0x99,0x42,0x3C};
static const uint8_t PROGMEM emo_think[] =     {0x3C,0x42,0xA5,0x81,0x81,0xB9,0x42,0x3C};
static const uint8_t PROGMEM emo_sad[] =       {0x3C,0x42,0xA5,0x81,0x99,0xA5,0x42,0x3C};
static const uint8_t PROGMEM emo_cry[] =       {0x3C,0x42,0xA5,0x91,0x99,0xA5,0x42,0x3C};
static const uint8_t PROGMEM emo_angry[] =     {0x3C,0x42,0xA5,0xDB,0x81,0x99,0x42,0x3C};
static const uint8_t PROGMEM emo_shock[] =     {0x3C,0x42,0xA5,0x81,0x81,0x99,0x42,0x3C};
static const uint8_t PROGMEM emo_sleep[] =     {0x3C,0x42,0x81,0x81,0x81,0x99,0x42,0x3C};
static const uint8_t PROGMEM emo_skull[] =     {0x3C,0x42,0xA5,0x81,0xFF,0xDB,0x42,0x3C};

// === Hands ===
static const uint8_t PROGMEM emo_thumbsup[] =  {0x0C,0x14,0x14,0x3C,0x7E,0x7E,0x7E,0x3C};
static const uint8_t PROGMEM emo_thumbsdown[] ={0x3C,0x7E,0x7E,0x7E,0x3C,0x14,0x14,0x0C};
static const uint8_t PROGMEM emo_wave[] =      {0x14,0x2A,0x2A,0x3E,0x1C,0x08,0x08,0x08};
static const uint8_t PROGMEM emo_peace[] =     {0x14,0x14,0x14,0x14,0x3C,0x7E,0x7E,0x3C};
static const uint8_t PROGMEM emo_ok[] =        {0x1C,0x22,0x22,0x1C,0x08,0x1C,0x08,0x08};
static const uint8_t PROGMEM emo_clap[] =      {0x18,0x24,0x18,0x3C,0x7E,0x7E,0x7E,0x3C};
static const uint8_t PROGMEM emo_pray[] =      {0x18,0x24,0x24,0x24,0x18,0x18,0x18,0x18};
static const uint8_t PROGMEM emo_muscle[] =    {0x06,0x09,0x09,0x1F,0x3E,0x60,0x60,0x3E};
static const uint8_t PROGMEM emo_point[] =     {0x04,0x04,0x04,0x04,0x3C,0x7E,0x7E,0x3C};
static const uint8_t PROGMEM emo_eyes[] =      {0x00,0x66,0x66,0x00,0x00,0x00,0x00,0x00};

// === Hearts ===
static const uint8_t PROGMEM emo_heart[] =     {0x00,0x66,0xFF,0xFF,0xFF,0x7E,0x3C,0x18};
static const uint8_t PROGMEM emo_broken[] =     {0x00,0x66,0xFB,0xF7,0xEF,0x7E,0x3C,0x18};
static const uint8_t PROGMEM emo_spark[] =     {0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00};

// === Objects ===
static const uint8_t PROGMEM emo_fire[] =      {0x10,0x28,0x54,0x54,0x7C,0x7C,0x38,0x10};
static const uint8_t PROGMEM emo_star[] =      {0x10,0x10,0x54,0x38,0xFE,0x38,0x54,0x10};
static const uint8_t PROGMEM emo_check[] =     {0x00,0x01,0x02,0x04,0x48,0x30,0x10,0x00};
static const uint8_t PROGMEM emo_cross[] =     {0x00,0x42,0x24,0x18,0x18,0x24,0x42,0x00};
static const uint8_t PROGMEM emo_warn[] =      {0x18,0x18,0x3C,0x3C,0x7E,0x7E,0xFF,0x00};
static const uint8_t PROGMEM emo_pin[] =       {0x18,0x3C,0x3C,0x3C,0x18,0x18,0x08,0x08};
static const uint8_t PROGMEM emo_bell[] =      {0x18,0x3C,0x3C,0x3C,0x7E,0x7E,0x18,0x00};
static const uint8_t PROGMEM emo_key[] =       {0x1C,0x22,0x22,0x1C,0x08,0x0E,0x08,0x08};
static const uint8_t PROGMEM emo_lock[] =      {0x18,0x24,0x24,0x7E,0x7E,0x7E,0x7E,0x3C};
static const uint8_t PROGMEM emo_radio[] =     {0x42,0x24,0x18,0x3C,0x42,0x18,0x18,0x18};
static const uint8_t PROGMEM emo_bulb[] =      {0x18,0x3C,0x66,0x66,0x3C,0x18,0x18,0x18};
static const uint8_t PROGMEM emo_msg[] =       {0xFF,0x81,0xC1,0xA1,0x91,0x81,0x81,0xFF};
static const uint8_t PROGMEM emo_phone[] =     {0x7E,0x42,0x42,0x42,0x42,0x42,0x7E,0x18};
static const uint8_t PROGMEM emo_house[] =     {0x18,0x3C,0x7E,0xFF,0x66,0x66,0x66,0x7E};

// === Nature ===
static const uint8_t PROGMEM emo_sun[] =       {0x24,0x18,0x7E,0xDB,0x7E,0x18,0x24,0x00};
static const uint8_t PROGMEM emo_moon[] =      {0x1C,0x3E,0x7E,0x7E,0x7E,0x3E,0x1C,0x00};
static const uint8_t PROGMEM emo_rain[] =      {0x3C,0x7E,0xFF,0xFF,0x00,0x24,0x24,0x00};
static const uint8_t PROGMEM emo_snow[] =      {0x24,0x18,0x7E,0x18,0x24,0x42,0x00,0x00};
static const uint8_t PROGMEM emo_tree[] =      {0x18,0x3C,0x7E,0x18,0x3C,0x7E,0x18,0x18};
static const uint8_t PROGMEM emo_mount[] =     {0x00,0x00,0x18,0x3C,0x7E,0xFF,0xFF,0x00};

// === Symbols ===
static const uint8_t PROGMEM emo_sos[] =       {0x7E,0x42,0x5A,0x5A,0x5A,0x5A,0x42,0x7E};
static const uint8_t PROGMEM emo_info[] =      {0x3C,0x42,0x5E,0x42,0x5E,0x5E,0x42,0x3C};
static const uint8_t PROGMEM emo_question[] =  {0x3C,0x42,0x42,0x0C,0x18,0x00,0x18,0x00};
static const uint8_t PROGMEM emo_hash[] =      {0x24,0x7E,0x24,0x24,0x7E,0x24,0x00,0x00};
static const uint8_t PROGMEM emo_at[] =        {0x3C,0x42,0x5E,0x52,0x5E,0x40,0x3C,0x00};
static const uint8_t PROGMEM emo_arrow_up[] =  {0x18,0x3C,0x7E,0x18,0x18,0x18,0x18,0x00};
static const uint8_t PROGMEM emo_arrow_dn[] =  {0x18,0x18,0x18,0x18,0x7E,0x3C,0x18,0x00};

// Master emoji table
static const EmojiDef kEmojis[] = {
    // Smileys
    {":smile:", "🙂", emo_smile,   "Smileys"},
    {":grin:", "😁", emo_grin,    "Smileys"},
    {":joy:", "😂", emo_joy,     "Smileys"},
    {":wink:", "😉", emo_wink,    "Smileys"},
    {":cool:", "😎", emo_cool,    "Smileys"},
    {":think:", "🤔", emo_think,   "Smileys"},
    {":sad:", "😞", emo_sad,     "Smileys"},
    {":cry:", "😢", emo_cry,     "Smileys"},
    {":angry:", "😠", emo_angry,   "Smileys"},
    {":shock:", "😮", emo_shock,   "Smileys"},
    {":sleep:", "😴", emo_sleep,   "Smileys"},
    {":skull:", "💀", emo_skull,   "Smileys"},
    // Hands
    {":thumbsup:", "👍", emo_thumbsup,  "Hands"},
    {":thumbsdown:", "👎", emo_thumbsdown,"Hands"},
    {":wave:", "👋", emo_wave,      "Hands"},
    {":peace:", "✌", emo_peace,     "Hands"},
    {":ok:", "👌", emo_ok,        "Hands"},
    {":clap:", "👏", emo_clap,      "Hands"},
    {":pray:", "🙏", emo_pray,      "Hands"},
    {":muscle:", "💪", emo_muscle,    "Hands"},
    {":point:", "👉", emo_point,     "Hands"},
    {":eyes:", "👀", emo_eyes,      "Hands"},
    // Hearts
    {":heart:", "❤", emo_heart,     "Hearts"},
    {":broken:", "💔", emo_broken,    "Hearts"},
    {":spark:", "✨", emo_spark,     "Hearts"},
    // Objects
    {":fire:", "🔥", emo_fire,      "Objects"},
    {":star:", "⭐", emo_star,      "Objects"},
    {":check:", "✅", emo_check,     "Objects"},
    {":cross:", "❌", emo_cross,     "Objects"},
    {":warn:", "⚠", emo_warn,      "Objects"},
    {":pin:", "📍", emo_pin,       "Objects"},
    {":bell:", "🔔", emo_bell,      "Objects"},
    {":key:", "🔑", emo_key,       "Objects"},
    {":lock:", "🔒", emo_lock,      "Objects"},
    {":radio:", "📻", emo_radio,     "Objects"},
    {":bulb:", "💡", emo_bulb,      "Objects"},
    {":msg:", "💬", emo_msg,       "Objects"},
    {":phone:", "📱", emo_phone,     "Objects"},
    {":house:", "🏠", emo_house,     "Objects"},
    // Nature
    {":sun:", "☀", emo_sun,       "Nature"},
    {":moon:", "🌙", emo_moon,      "Nature"},
    {":rain:", "🌧", emo_rain,      "Nature"},
    {":snow:", "❄", emo_snow,      "Nature"},
    {":tree:", "🌲", emo_tree,      "Nature"},
    {":mount:", "⛰", emo_mount,     "Nature"},
    // Symbols
    {":sos:", "🆘", emo_sos,       "Symbols"},
    {":info:", "ℹ", emo_info,      "Symbols"},
    {":question:", "❓", emo_question,  "Symbols"},
    {":hash:", "#", emo_hash,      "Symbols"},
    {":at:", "@", emo_at,        "Symbols"},
    {":up:", "⬆", emo_arrow_up,  "Symbols"},
    {":down:", "⬇", emo_arrow_dn,  "Symbols"},
};

static constexpr int kEmojiCount = sizeof(kEmojis) / sizeof(kEmojis[0]);

// Find emoji by code, returns index or -1
inline int findEmoji(const char* code) {
    for (int i = 0; i < kEmojiCount; i++) {
        if (strcmp(code, kEmojis[i].code) == 0) return i;
    }
    return -1;
}

// Get category names (unique, in order)
inline int getEmojiCategoryCount() {
    int count = 0;
    const char* last = "";
    for (int i = 0; i < kEmojiCount; i++) {
        if (strcmp(kEmojis[i].category, last) != 0) {
            last = kEmojis[i].category;
            count++;
        }
    }
    return count;
}
