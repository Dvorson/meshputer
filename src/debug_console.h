#pragma once

// Line-based command console on the USB serial port (115200 baud).
// Lets a host script drive and inspect the device: `make monitor`, then `help`.
// Every command's output ends with a line "## done" (or "## error <why>").

#include "board.h"

namespace DebugConsole {

void poll();

// Keystrokes injected with the `key` command, consumed like real key presses.
bool popKey(KeyInput& out);

} // namespace DebugConsole
