#pragma once

// Drawing helpers and small widgets shared by all screens. Everything draws
// into Board::canvas(); main.cpp pushes the frame when a screen is dirty.

#include <M5GFX.h>
#include <string>

#include "board.h"
#include "theme.h"

namespace Color {
inline uint32_t BG()           { return activeTheme().bg; }
inline uint32_t TEXT()         { return activeTheme().text; }
inline uint32_t DIM()          { return activeTheme().dim; }
inline uint32_t HEADER_BG()    { return activeTheme().headerBg; }
inline uint32_t HEADER_TEXT()  { return activeTheme().headerText; }
inline uint32_t ACCENT()       { return activeTheme().accent; }
inline uint32_t SELECTED_BG()  { return activeTheme().selectedBg; }
inline uint32_t INPUT_BG()     { return activeTheme().inputBg; }
inline uint32_t MSG_MINE()     { return activeTheme().msgMine; }
inline uint32_t MSG_OTHER()    { return activeTheme().msgOther; }
inline uint32_t CONNECTED()    { return activeTheme().connected; }
inline uint32_t SCANNING()     { return activeTheme().scanning; }
inline uint32_t ERROR_CLR()    { return activeTheme().errorClr; }
inline uint32_t DISCONNECTED() { return activeTheme().disconnected; }
inline uint32_t SEPARATOR()    { return activeTheme().separator; }
}

// Tuned for the 240x135 panel and the 6x8 built-in font.
namespace Layout {
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;
constexpr int HEADER_H = 16;
constexpr int FOOTER_H = 12;
constexpr int INPUT_H = 13;
constexpr int CONTENT_Y = HEADER_H;
constexpr int CONTENT_H = SCREEN_H - HEADER_H - FOOTER_H;
constexpr int LINE_H = 13;
constexpr int ROWS = CONTENT_H / LINE_H;
constexpr int MARGIN = 4;
constexpr int CHAR_W = 6;
constexpr int CHAR_H = 8;
constexpr int COLS = (SCREEN_W - 2 * MARGIN) / CHAR_W;
constexpr int INDICATOR_W = 3;
}

namespace UI {

constexpr uint32_t kToastMs = 4000;

void header(M5Canvas& c, const std::string& title);
// Shows the latest chat toast instead of the hint while it is fresh.
void footer(M5Canvas& c, const std::string& hint);
void clearContent(M5Canvas& c);
void row(M5Canvas& c, int visibleIndex, const std::string& text, bool selected,
         const std::string& right = "");
void centered(M5Canvas& c, const std::string& text, uint32_t color, int dy = 0);
// "Label      value" line at content row r.
void field(M5Canvas& c, int r, const std::string& label, const std::string& value,
           uint32_t valueColor = 0);
uint32_t linkColor();
bool toastVisible();

} // namespace UI

// Cursor + scroll window over a list of `count` rows.
struct ListView {
    int sel = 0;
    int top = 0;
    // Handles up/down; returns true if the selection moved.
    bool move(const KeyInput& k, int count, int rows = Layout::ROWS);
    void clamp(int count, int rows = Layout::ROWS);
    bool visible(int i, int rows = Layout::ROWS) const { return i >= top && i < top + rows; }
};

// Single-line text input with a UTF-8 byte budget.
struct LineEdit {
    std::string text;
    size_t maxBytes = 200;
    bool digitsOnly = false;
    // Applies typed characters and Del. Returns true if the text changed.
    // Del on an empty field returns false so callers can treat it as "back".
    bool handle(const KeyInput& k);
};
