#pragma once

// Device hardware: display, input, beeper, battery, storage. Everything
// hardware-specific lives behind this header (board_cardputer.cpp,
// board_tab5.cpp); the Tab5's display and touch are driven by its LVGL UI.

#include <Arduino.h>
#include <M5GFX.h>
#include <string>
#include <vector>

struct KeyInput {
    bool any = false;
    bool enter = false;
    bool del = false;
    bool fn = false;
    bool tab = false;
    std::vector<char> chars;  // printable characters, already shifted

    bool has(char c) const {
        for (char k : chars) if (k == c) return true;
        return false;
    }
    // Cardputer arrow keys are ; . , / (printed in the corner of those keys).
    bool up() const { return has(';'); }
    bool down() const { return has('.'); }
    bool left() const { return has(','); }
    bool right() const { return has('/'); }
    // ESC is the ` key; in lists Del also goes back.
    bool esc() const { return has('`'); }
};

namespace Board {

void begin();
void update();  // call once per loop: scans input, idles the speaker

#if !defined(MESHPUTER_BOARD_TAB5)
// Off-screen frame buffer; screens draw here and call present().
M5Canvas& canvas();
void present();

// Key events are queued by every scan (update() and scanKeyboard()), so a
// short tap is not lost while the main loop is busy drawing.
bool pollKeys(KeyInput& out);
void scanKeyboard();  // extra keyboard-only scan, e.g. right after a redraw
#endif

void setBacklight(uint8_t level);  // 0 turns the panel off
void displaySleep();
void displayWake();

// Short beeps; the amplifier is powered only while sound is playing.
void beep(uint16_t freqHz, uint16_t ms, uint8_t volume, bool queue = false);

int batteryPercent();  // -1 if unknown

// SD card (FAT). Paths are absolute, e.g. "/meshputer/abc/c0.txt".
bool sdReady();
bool readFile(const char* path, std::string& out);
bool writeFile(const char* path, const std::string& data);
bool ensureDir(const char* path);
bool listDir(const char* path, std::vector<std::string>& names);

} // namespace Board
