#pragma once

#include <Arduino.h>

// ──────────────────────────────────────────────────────
// Meshputer Color Theme System
//
// Design direction: Teenage Engineering x Dieter Rams
// - High-contrast, geometric, confident (TE)
// - Restrained, honest surfaces, clear hierarchy (Rams)
// - Premium, calm, legible on 240x135 TFT
// ──────────────────────────────────────────────────────

struct ColorTheme {
    const char* name;
    uint32_t bg;
    uint32_t text;
    uint32_t dim;
    uint32_t headerBg;
    uint32_t headerText;
    uint32_t accent;
    uint32_t selectedBg;
    uint32_t inputBg;
    uint32_t msgMine;
    uint32_t msgOther;
    uint32_t connected;
    uint32_t scanning;
    uint32_t errorClr;
    uint32_t disconnected;
    uint32_t separator;
};

// Dark — default. Near-black ground, warm white type, coral-orange accent.
// Feels like a premium device dashboard.
static const ColorTheme kThemeDark = {
    "Dark",
    0x0C0C0C,   // bg: near-black, honest surface
    0xE8E4DF,   // text: warm off-white, easy on eyes
    0x5A5652,   // dim: warm mid-gray
    0x1A1918,   // headerBg: slightly lifted from bg
    0xD0CBC6,   // headerText: warm light gray
    0xFF6B35,   // accent: coral-orange (TE playfulness)
    0x1E1C1A,   // selectedBg: subtle lift from bg
    0x141312,   // inputBg: just above bg
    0x1C1510,   // msgMine: warm dark tint
    0x181818,   // msgOther: neutral dark
    0x2ECC71,   // connected: confident green
    0xE8A838,   // scanning: warm amber
    0xE74C3C,   // error: clear red
    0x3D3A37,   // disconnected: warm dark gray
    0x2A2725    // separator: barely visible line
};

// Light — warm paper ground, dark type, muted accent.
// Rams-inspired: clear, calm, nothing shouts.
static const ColorTheme kThemeLight = {
    "Light",
    0xF2EDE8,   // bg: warm paper white
    0x1A1816,   // text: near-black
    0x7A756F,   // dim: warm mid-gray
    0xE6E0DA,   // headerBg: slightly cooler than bg
    0x2A2624,   // headerText: near-black
    0xD4582A,   // accent: muted burnt orange
    0xDDD6CE,   // selectedBg: subtle warm tint
    0xE8E2DC,   // inputBg: very close to bg
    0xD8D0C8,   // msgMine: warm light tone
    0xE0DAD4,   // msgOther: lighter warm
    0x27A85B,   // connected: muted green
    0xC4922E,   // scanning: muted amber
    0xC94030,   // error: muted red
    0xA09A94,   // disconnected: warm gray
    0xD0C8C0    // separator: subtle warm line
};

// OLED Black — pure black for OLED screens. Maximum contrast.
// Sharp, technical, monochrome-leaning with orange signal.
static const ColorTheme kThemeOLED = {
    "OLED Black",
    0x000000,   // bg: true black
    0xDDDAD6,   // text: warm white (not harsh pure white)
    0x484440,   // dim: warm dark gray
    0x000000,   // headerBg: stays black
    0x908880,   // headerText: muted warm
    0xFF6B35,   // accent: same coral-orange
    0x141210,   // selectedBg: just barely visible
    0x0A0908,   // inputBg: near-black
    0x141008,   // msgMine: warm near-black
    0x121212,   // msgOther: neutral near-black
    0x2ECC71,   // connected: green
    0xE8A838,   // scanning: amber
    0xE74C3C,   // error: red
    0x302C28,   // disconnected: dark warm gray
    0x1E1C1A    // separator: barely-there line
};

static const ColorTheme* kThemes[] = { &kThemeDark, &kThemeLight, &kThemeOLED };
static constexpr int kThemeCount = 3;

// Get active theme based on settings
const ColorTheme& activeTheme();
