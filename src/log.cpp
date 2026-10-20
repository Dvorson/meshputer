#include "log.h"

#include <Arduino.h>
#include <cstdarg>

namespace {
bool sEnabled = true;
}

namespace Log {

bool enabled() { return sEnabled; }
void setEnabled(bool on) { sEnabled = on; }

void write(const char* tag, const char* fmt, ...) {
    if (!sEnabled) return;
    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    Serial.printf("[%s] %lu %s\n", tag, static_cast<unsigned long>(millis()), line);
}

} // namespace Log
