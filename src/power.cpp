#include "power.h"

#include <Arduino.h>

#include "board.h"
#include "log.h"
#include "settings.h"

namespace {

enum class Screen : uint8_t { Bright, Dim, Off };

constexpr uint32_t kCpuMhz = 80;  // enough for BLE + UI; far less current than 240 MHz
constexpr uint32_t kLoopDelayAwakeMs = 15;
constexpr uint32_t kLoopDelayAsleepMs = 40;

Screen sScreen = Screen::Bright;
uint32_t sLastActivity = 0;

uint8_t dimLevel() {
    uint8_t d = Settings::get().brightness / 5;
    return d < 8 ? 8 : d;
}

void setScreen(Screen s) {
    if (s == sScreen) return;
    switch (s) {
    case Screen::Bright:
        Board::displayWake();
        Board::setBacklight(Settings::get().brightness);
        break;
    case Screen::Dim:
        Board::displayWake();
        Board::setBacklight(dimLevel());
        break;
    case Screen::Off:
        Board::displaySleep();
        break;
    }
    sScreen = s;
}

} // namespace

namespace Power {

void begin() {
    setCpuFrequencyMhz(kCpuMhz);
    Board::setBacklight(Settings::get().brightness);
    sLastActivity = millis();
    LOG("PWR", "cpu %lu MHz", static_cast<unsigned long>(getCpuFrequencyMhz()));
}

void loop() {
    const Settings& s = Settings::get();
    uint32_t idle = millis() - sLastActivity;
    if (s.screenOffSec != 0 && idle > s.screenOffSec * 1000UL) {
        setScreen(Screen::Off);
    } else if (s.dimAfterSec != 0 && idle > s.dimAfterSec * 1000UL && sScreen == Screen::Bright) {
        setScreen(Screen::Dim);
    }
}

bool onKey() {
    bool wasOff = sScreen == Screen::Off;
    sLastActivity = millis();
    setScreen(Screen::Bright);
    return wasOff;
}

void wakeForMessage() {
    if (sScreen != Screen::Off) return;  // already visible; don't disturb the timers
    // Show the message dimmed. Backdate activity to the dim point only when that
    // still leaves time before screen-off, so the message stays readable.
    const Settings& s = Settings::get();
    sLastActivity = millis();
    if (s.dimAfterSec != 0 && (s.screenOffSec == 0 || s.dimAfterSec < s.screenOffSec)) {
        sLastActivity -= s.dimAfterSec * 1000UL;
    }
    setScreen(Screen::Dim);
}

bool screenOn() { return sScreen != Screen::Off; }

uint32_t loopDelayMs() {
    return sScreen == Screen::Off ? kLoopDelayAsleepMs : kLoopDelayAwakeMs;
}

} // namespace Power
