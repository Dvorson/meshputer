#pragma once

// Notification tones. Board::beep powers the amplifier only while a tone plays.

#include "board.h"
#include "settings.h"

namespace Sound {

inline uint8_t vol(uint8_t cap = 255) {
    uint8_t v = Settings::get().volume;
    return v > cap ? cap : v;
}

inline bool on() { return Settings::get().soundEnabled; }

inline void message() {
    if (!on()) return;
    Board::beep(1200, 70, vol());
    Board::beep(1600, 70, vol(), true);
}

inline void sent() {
    if (on()) Board::beep(1000, 35, vol(80));
}

inline void error() {
    if (!on()) return;
    Board::beep(300, 150, vol(100));
    Board::beep(250, 150, vol(100), true);
}

inline void connected() {
    if (!on()) return;
    Board::beep(800, 80, vol());
    Board::beep(1200, 80, vol(), true);
    Board::beep(1600, 120, vol(), true);
}

inline void disconnected() {
    if (on()) Board::beep(700, 150, vol(100));
}

} // namespace Sound
