#pragma once

// Tagged, timestamped log lines on the USB serial port, e.g.
//   [MESH] 12345 state syncing -> ready
// Toggle at runtime with the "logs on|off" console command.

namespace Log {
bool enabled();
void setEnabled(bool on);
void write(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
}

#define LOG(tag, ...) Log::write(tag, __VA_ARGS__)
