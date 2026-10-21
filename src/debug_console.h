#pragma once

// Line-based command console on the USB serial port (115200 baud).
// Lets a host script drive and inspect the device: `make monitor`, then `help`.
// Every command's output ends with a line "## done" (or "## error <why>").

namespace DebugConsole {

void poll();

} // namespace DebugConsole
