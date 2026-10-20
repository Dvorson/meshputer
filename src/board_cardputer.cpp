// Board layer for the M5Stack Cardputer / Cardputer ADV (ESP32-S3).
#if !defined(MESHPUTER_BOARD_TAB5)

#include "board.h"

#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>

#include <deque>

namespace {

// SD card on the Cardputer's second SPI bus.
constexpr int kSdSck = 40;
constexpr int kSdMiso = 39;
constexpr int kSdMosi = 14;
constexpr int kSdCs = 12;

constexpr uint32_t kRepeatDelayMs = 450;
constexpr uint32_t kRepeatEveryMs = 70;
constexpr uint32_t kSpeakerIdleOffMs = 300;

M5Canvas sCanvas(&M5Cardputer.Display);
bool sSdReady = false;
bool sDisplayAsleep = false;

// Keyboard: we diff the pressed key positions ourselves. The library's
// isChange() only compares how many keys are down, so rolling from one key to
// the next between two scans (fast typing) was silently dropped.
std::vector<Point2D_t> sDownKeys;
std::deque<KeyInput> sKeyQueue;
constexpr size_t kKeyQueueMax = 16;
bool sHeld = false;
Point2D_t sHeldPos;
uint32_t sHeldSince = 0;
uint32_t sLastRepeat = 0;
KeyInput sHeldKeys;

uint32_t sLastSoundMs = 0;

bool contains(const std::vector<Point2D_t>& keys, const Point2D_t& p) {
    for (const auto& k : keys) if (k == p) return true;
    return false;
}

void pushKey(const KeyInput& k) {
    if (sKeyQueue.size() < kKeyQueueMax) sKeyQueue.push_back(k);
}

// Reads the matrix and queues an event for every key that went down since the
// previous scan; repeats a held character or Del.
void scanKeys() {
    auto& kb = M5Cardputer.Keyboard;
    const std::vector<Point2D_t>& keys = kb.keyList();
    uint32_t now = millis();

    KeyInput ev;
    ev.fn = kb.keysState().fn;
    Point2D_t lastNew;
    for (const auto& p : keys) {
        if (contains(sDownKeys, p)) continue;
        uint8_t code = kb.getKeyValue(p).value_first;
        if (code == KEY_ENTER) ev.enter = true;
        else if (code == KEY_BACKSPACE) ev.del = true;
        else if (code == KEY_TAB) ev.tab = true;
        else {
            uint8_t c = kb.getKey(p);  // applies shift / caps lock
            if (c < 32 || c > 126) continue;  // fn, opt, ctrl, alt
            ev.chars.push_back(static_cast<char>(c));
        }
        lastNew = p;
    }
    sDownKeys.assign(keys.begin(), keys.end());
    ev.any = ev.enter || ev.del || ev.tab || !ev.chars.empty();

    if (ev.any) {
        pushKey(ev);
        sHeld = true;
        sHeldPos = lastNew;
        sHeldSince = sLastRepeat = now;
        sHeldKeys = ev;
        sHeldKeys.enter = false;  // never auto-repeat Enter (would resend)
        sHeldKeys.tab = false;
        return;
    }
    if (!sHeld || !contains(keys, sHeldPos)) {
        sHeld = false;
        return;
    }
    if ((sHeldKeys.del || !sHeldKeys.chars.empty()) &&
        now - sHeldSince > kRepeatDelayMs && now - sLastRepeat > kRepeatEveryMs) {
        sLastRepeat = now;
        pushKey(sHeldKeys);
    }
}

} // namespace

namespace Board {

void begin() {
    auto cfg = M5.config();
    cfg.internal_mic = false;   // shares GPIO43 with the speaker; never used
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    cfg.led_brightness = 0;
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Speaker.end();  // amplifier off until we actually beep

    M5Cardputer.Display.setRotation(1);
    sCanvas.setColorDepth(16);
    sCanvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
    sCanvas.setFont(&fonts::Font0);
    sCanvas.setTextSize(1);
    sCanvas.setTextDatum(textdatum_t::top_left);

    SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    sSdReady = SD.begin(kSdCs, SPI, 25000000);
}

void update() {
    M5Cardputer.update();
    scanKeys();
    // sLastSoundMs is when the last tone ends; signed difference handles "in the future".
    if (M5Cardputer.Speaker.isRunning() && !M5Cardputer.Speaker.isPlaying() &&
        static_cast<int32_t>(millis() - sLastSoundMs) > static_cast<int32_t>(kSpeakerIdleOffMs)) {
        M5Cardputer.Speaker.end();
    }
}

M5Canvas& canvas() { return sCanvas; }

void present() {
    if (sDisplayAsleep) return;
    sCanvas.pushSprite(0, 0);
}

void scanKeyboard() {
    M5Cardputer.Keyboard.updateKeyList();
    M5Cardputer.Keyboard.updateKeysState();
    scanKeys();
}

bool pollKeys(KeyInput& out) {
    if (sKeyQueue.empty()) return false;
    out = sKeyQueue.front();
    sKeyQueue.pop_front();
    return true;
}

void setBacklight(uint8_t level) {
    M5Cardputer.Display.setBrightness(level);
}

void displaySleep() {
    if (sDisplayAsleep) return;
    M5Cardputer.Display.setBrightness(0);
    M5Cardputer.Display.sleep();
    sDisplayAsleep = true;
}

void displayWake() {
    if (!sDisplayAsleep) return;
    M5Cardputer.Display.wakeup();
    sDisplayAsleep = false;
    sCanvas.pushSprite(0, 0);
}

void beep(uint16_t freqHz, uint16_t ms, uint8_t volume, bool queue) {
    if (!M5Cardputer.Speaker.isRunning()) M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume(volume);
    M5Cardputer.Speaker.tone(freqHz, ms, 0, !queue);
    // A queued tone starts when the previous one ends.
    uint32_t now = millis();
    bool pending = static_cast<int32_t>(sLastSoundMs - now) > 0;
    sLastSoundMs = (queue && pending ? sLastSoundMs : now) + ms;
}

int batteryPercent() {
    int level = M5Cardputer.Power.getBatteryLevel();
    return (level < 0 || level > 100) ? -1 : level;
}

bool sdReady() { return sSdReady; }

bool readFile(const char* path, std::string& out) {
    if (!sSdReady) return false;
    File f = SD.open(path, FILE_READ);
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
    // Write-then-rename so a power cut never leaves a truncated history file.
    std::string tmp = std::string(path) + ".tmp";
    File f = SD.open(tmp.c_str(), FILE_WRITE);
    if (!f) return false;
    size_t written = f.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    f.close();
    if (written != data.size()) {
        SD.remove(tmp.c_str());
        return false;
    }
    SD.remove(path);
    return SD.rename(tmp.c_str(), path);
}

bool ensureDir(const char* path) {
    if (!sSdReady) return false;
    std::string p(path);
    for (size_t i = 1; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '/') {
            std::string part = p.substr(0, i);
            if (!SD.exists(part.c_str()) && !SD.mkdir(part.c_str())) return false;
        }
    }
    return true;
}

bool listDir(const char* path, std::vector<std::string>& names) {
    if (!sSdReady) return false;
    File dir = SD.open(path);
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
