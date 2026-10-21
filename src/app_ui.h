#pragma once

// The device's user interface. Each board brings its own: the Cardputer has
// keyboard-driven screens on a 240x135 panel (src/screens), the Tab5 a touch
// UI built with LVGL that follows the device's orientation (src/tab5).
//
// The services below the UI (gMesh, Chat, NostrService, Settings, Power) are
// shared; the UI only reads them and calls into them from the main loop.

#include <string>

namespace AppUi {

void begin();
void loop();  // input, navigation, drawing; called once per main loop

// --- serial console hooks (scripted tests and screenshots) ---

const char* screenName();
// Back to the start screen, e.g. after demo data replaced the conversations.
void home();
// Jumps to a screen: "home", "chats", "nodes", "channels", "radio",
// "settings", "chat <n>", "node <!id>". Returns "" or an error.
std::string open(const std::string& what, const std::string& rest);
// Injects a key ("enter", "esc", "up", "fn+x", or literal text to type).
std::string key(const std::string& spec);
// Touch input; "" or an error (e.g. on a board without a touch screen).
std::string tap(int x, int y);
std::string swipe(int x1, int y1, int x2, int y2);
// Draws the UI turned this way (0-3) instead of following the device; -1 resumes.
std::string rotate(int rotation);
// Prints the frame as "SHOT <w> <h> rgb565be", one hex line per row, every
// `step`-th pixel and row, then "SHOT END".
void dumpFrame(int step);

} // namespace AppUi
