#pragma once

// LVGL on the Tab5: the MIPI-DSI panel as an LVGL display, the touch panel as
// its pointer, and the IMU deciding which way up the UI is drawn.

#include <string>

namespace Port {

void begin();
void tick();  // runs LVGL timers: input, animations, drawing

// Checks the orientation setting and, when it follows the device, gravity.
// Returns true when the rotation changed: the UI must rebuild its layout.
bool updateRotation();
bool landscape();
// Scripted tests: use this rotation instead of the setting and the IMU; -1 resumes.
void forceRotation(int rotation);

// Scripted touch input from the serial console.
void injectTap(int x, int y);
void injectSwipe(int x1, int y1, int x2, int y2);

// Prints the panel contents (see AppUi::dumpFrame).
void dumpFrame(int step);

} // namespace Port
