// Board layer for the M5Stack Tab5 (ESP32-P4, 720x1280 MIPI-DSI touch panel):
// power-up, backlight, speaker, battery and storage. The display and touch
// panel are driven by the LVGL UI (src/tab5).
#if defined(MESHPUTER_BOARD_TAB5)

#include "board.h"
#include "core/text_util.h"

#include <M5Unified.h>
#include <LittleFS.h>
#include <SD_MMC.h>

namespace {

constexpr uint32_t kSpeakerIdleOffMs = 300;

// SD card on SDMMC (4-bit): CLK, CMD, D0..D3.
constexpr int kSdClk = 43, kSdCmd = 44, kSdD0 = 39, kSdD1 = 40, kSdD2 = 41, kSdD3 = 42;

bool sSdReady = false;
fs::FS* sFs = nullptr;  // SD card if present, otherwise internal flash (LittleFS)
bool sDisplayAsleep = false;
uint32_t sLastSoundMs = 0;

} // namespace

namespace Board {

void begin() {
    auto cfg = M5.config();
    cfg.internal_mic = false;
    cfg.internal_imu = true;  // the UI turns with the device
    M5.begin(cfg);  // also powers the C6 radio and points esp-hosted at it
    M5.Display.fillScreen(0x000000u);
    M5.Speaker.end();

    SD_MMC.setPins(kSdClk, kSdCmd, kSdD0, kSdD1, kSdD2, kSdD3);
    if (SD_MMC.begin("/sdcard", false)) {
        sFs = &SD_MMC;
    } else if (LittleFS.begin(true)) {
        // No card: keep history in the 16 MB flash rather than losing it on reboot.
        sFs = &LittleFS;
    }
    sSdReady = sFs != nullptr;
}

void update() {
    M5.update();
    if (M5.Speaker.isRunning() && !M5.Speaker.isPlaying() &&
        static_cast<int32_t>(millis() - sLastSoundMs) > static_cast<int32_t>(kSpeakerIdleOffMs)) {
        M5.Speaker.end();
    }
}

void setBacklight(uint8_t level) { M5.Display.setBrightness(level); }

void displaySleep() {
    if (sDisplayAsleep) return;
    M5.Display.setBrightness(0);
    M5.Display.sleep();
    sDisplayAsleep = true;
}

void displayWake() {
    if (!sDisplayAsleep) return;
    M5.Display.wakeup();
    sDisplayAsleep = false;
}

void beep(uint16_t freqHz, uint16_t ms, uint8_t volume, bool queue) {
    if (!M5.Speaker.isRunning()) M5.Speaker.begin();
    M5.Speaker.setVolume(volume);
    M5.Speaker.tone(freqHz, ms, 0, !queue);
    uint32_t now = millis();
    bool pending = static_cast<int32_t>(sLastSoundMs - now) > 0;
    sLastSoundMs = (queue && pending ? sLastSoundMs : now) + ms;
}

int batteryPercent() {
    int level = M5.Power.getBatteryLevel();
    return (level < 0 || level > 100) ? -1 : level;
}

std::string screenText(const std::string& utf8) { return TextUtil::toUnicodeDisplay(utf8); }

bool sdReady() { return sSdReady; }

bool readFile(const char* path, std::string& out) {
    if (!sSdReady) return false;
    File f = sFs->open(path, FILE_READ);
    if (!f) return false;
    out.clear();
    out.reserve(f.size());
    char buf[256];
    while (f.available()) {
        int n = f.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf));
        if (n <= 0) break;
        out.append(buf, n);
    }
    f.close();
    return true;
}

bool writeFile(const char* path, const std::string& data) {
    if (!sSdReady) return false;
    std::string tmp = std::string(path) + ".tmp";
    File f = sFs->open(tmp.c_str(), FILE_WRITE);
    if (!f) return false;
    size_t written = f.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    f.close();
    if (written != data.size()) {
        sFs->remove(tmp.c_str());
        return false;
    }
    sFs->remove(path);
    return sFs->rename(tmp.c_str(), path);
}

bool ensureDir(const char* path) {
    if (!sSdReady) return false;
    std::string p(path);
    for (size_t i = 1; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '/') {
            std::string part = p.substr(0, i);
            if (!sFs->exists(part.c_str()) && !sFs->mkdir(part.c_str())) return false;
        }
    }
    return true;
}

bool listDir(const char* path, std::vector<std::string>& names) {
    if (!sSdReady) return false;
    File dir = sFs->open(path);
    if (!dir || !dir.isDirectory()) return false;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (!f.isDirectory()) names.emplace_back(f.name());
        f.close();
    }
    dir.close();
    return true;
}

} // namespace Board

#endif
