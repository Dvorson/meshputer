#pragma once

// Power policy: 80 MHz CPU, backlight dims then the panel sleeps when idle,
// and the main loop sleeps between polls. Wi-Fi is never initialised (no code
// links the Wi-Fi stack). Bluetooth duty is owned by MeshtasticClient, which
// powers the controller down when no radio is needed.

#include <cstdint>

namespace Power {

void begin();
void loop();

// A key was pressed. Returns true if the screen was off: callers should then
// swallow that key so waking the device does not also act on it.
bool onKey();

// An incoming message: light the screen (dimmed) so it can be read.
void wakeForMessage();

bool screenOn();

// How long the main loop may sleep before polling again.
uint32_t loopDelayMs();

} // namespace Power
