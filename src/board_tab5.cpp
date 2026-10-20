// Board layer for the M5Stack Tab5 (ESP32-P4, 720x1280 MIPI-DSI touch panel).
//
// The app UI is the Cardputer's 240x135 canvas drawn 3x larger at the top of the
// portrait screen; below it sits a touch keyboard that produces the same
// KeyInput events as the Cardputer keys, so every screen works unchanged.
#if defined(MESHPUTER_BOARD_TAB5)

#include "board.h"
#include "ui.h"

#include <M5Unified.h>
#include <LittleFS.h>
#include <SD_MMC.h>

#include <deque>

namespace {

constexpr int kZoom = 3;
constexpr int kAppW = 240 * kZoom;  // 720
constexpr int kAppH = 135 * kZoom;  // 405
constexpr int kKeysTop = kAppH + 24;
constexpr int kKeyGap = 8;
constexpr uint32_t kSpeakerIdleOffMs = 300;
constexpr size_t kKeyQueueMax = 16;

// SD card on SDMMC (4-bit): CLK, CMD, D0..D3.
constexpr int kSdClk = 43, kSdCmd = 44, kSdD0 = 39, kSdD1 = 40, kSdD2 = 41, kSdD3 = 42;

M5Canvas sCanvas(&M5.Display);
bool sSdReady = false;
fs::FS* sFs = nullptr;  // SD card if present, otherwise internal flash (LittleFS)
bool sDisplayAsleep = false;
uint32_t sLastSoundMs = 0;
std::deque<KeyInput> sKeyQueue;

enum class Special : uint8_t { None, Enter, Del, Esc, Shift, Symbols, Space, Up, Down, Left, Right, Fn };

struct Key {
    const char* label;
    char ch;          // printable, if any
    Special special;
    float width;      // in key units
};

// Two layers; every row adds up to 10 units.
const Key kLetters[5][11] = {
    {{"1",'1'},{"2",'2'},{"3",'3'},{"4",'4'},{"5",'5'},{"6",'6'},{"7",'7'},{"8",'8'},{"9",'9'},{"0",'0'},{nullptr}},
    {{"q",'q'},{"w",'w'},{"e",'e'},{"r",'r'},{"t",'t'},{"y",'y'},{"u",'u'},{"i",'i'},{"o",'o'},{"p",'p'},{nullptr}},
    {{"a",'a'},{"s",'s'},{"d",'d'},{"f",'f'},{"g",'g'},{"h",'h'},{"j",'j'},{"k",'k'},{"l",'l'},{"Del",0,Special::Del},{nullptr}},
    {{"Shift",0,Special::Shift},{"z",'z'},{"x",'x'},{"c",'c'},{"v",'v'},{"b",'b'},{"n",'n'},{"m",'m'},{",",','},{".",'.'},{nullptr}},
    {{"?123",0,Special::Symbols,1.3f},{"Esc",0,Special::Esc,1.3f},{"Up",0,Special::Up,1.2f},{"Down",0,Special::Down,1.4f},
     {"",' ',Special::Space,2.6f},{"Enter",0,Special::Enter,2.2f},{nullptr}},
};
const Key kSymbols[5][11] = {
    {{"!",'!'},{"@",'@'},{"#",'#'},{"$",'$'},{"%",'%'},{"&",'&'},{"*",'*'},{"(",'('},{")",')'},{"'",'\''},{nullptr}},
    {{"-",'-'},{"_",'_'},{"=",'='},{"+",'+'},{":",':'},{";",';'},{"/",'/'},{"?",'?'},{"\"",'"'},{"~",'~'},{nullptr}},
    {{"<",'<'},{">",'>'},{"[",'['},{"]",']'},{"{",'{'},{"}",'}'},{"\\",'\\'},{"|",'|'},{"^",'^'},{"Del",0,Special::Del},{nullptr}},
    {{"Up",0,Special::Up,2.5f},{"Down",0,Special::Down,2.5f},{"Left",0,Special::Left,2.5f},{"Right",0,Special::Right,2.5f},{nullptr}},
    {{"abc",0,Special::Symbols,1.5f},{"Esc",0,Special::Esc,1.5f},{"",' ',Special::Space,4.0f},{"Enter",0,Special::Enter,3.0f},{nullptr}},
};

bool sShift = false;
bool sSymbols = false;

int keyHeight() { return (M5.Display.height() - kKeysTop - kKeyGap) / 5 - kKeyGap; }

template <typename Fn>
void forEachKey(Fn fn) {
    const Key (*layout)[11] = sSymbols ? kSymbols : kLetters;
    int unit = (M5.Display.width() - kKeyGap) / 10;
    int h = keyHeight();
    for (int r = 0; r < 5; ++r) {
        float x = kKeyGap / 2.0f;
        int y = kKeysTop + r * (h + kKeyGap);
        for (int c = 0; layout[r][c].label; ++c) {
            const Key& k = layout[r][c];
            float w = (k.width > 0 ? k.width : 1.0f) * unit;
            fn(k, int(x) + kKeyGap / 2, y, int(w) - kKeyGap, h);
            x += w;
        }
    }
}

void drawKeyboard() {
    auto& d = M5.Display;
    d.fillRect(0, kAppH, d.width(), d.height() - kAppH, 0x0C0C0Cu);
    d.setFont(&fonts::FreeSansBold18pt7b);
    d.setTextDatum(textdatum_t::middle_center);
    forEachKey([&](const Key& k, int x, int y, int w, int h) {
        bool active = (k.special == Special::Shift && sShift);
        uint32_t bg = k.special == Special::Enter ? 0xFF6B35u : (active ? 0x5A5652u : 0x262422u);
        d.fillRoundRect(x, y, w, h, 12, bg);
        d.setTextColor(k.special == Special::Enter ? 0x111111u : 0xE8E4DFu, bg);
        std::string label = k.label;
        if (k.ch >= 'a' && k.ch <= 'z' && sShift) label[0] = char(label[0] - 'a' + 'A');
        if (!label.empty()) d.drawString(label.c_str(), x + w / 2, y + h / 2);
    });
    d.setTextDatum(textdatum_t::top_left);
    d.setFont(&fonts::Font0);
}

void pushKey(const KeyInput& k) {
    if (sKeyQueue.size() < kKeyQueueMax) sKeyQueue.push_back(k);
}

void onTap(int tx, int ty) {
    if (ty < kAppH) {
        // The app area: the title bar goes back, a content row opens that row.
        int vy = ty / kZoom;
        KeyInput in;
        in.any = true;
        if (vy < Layout::CONTENT_Y) in.chars.push_back('`');
        else if (vy < Layout::SCREEN_H - Layout::FOOTER_H) in.tapRow = (vy - Layout::CONTENT_Y) / Layout::LINE_H;
        else return;
        pushKey(in);
        return;
    }
    forEachKey([&](const Key& k, int x, int y, int w, int h) {
        if (tx < x || tx >= x + w || ty < y || ty >= y + h) return;
        KeyInput in;
        in.any = true;
        switch (k.special) {
        case Special::Shift: sShift = !sShift; drawKeyboard(); return;
        case Special::Symbols: sSymbols = !sSymbols; sShift = false; drawKeyboard(); return;
        case Special::Enter: in.enter = true; break;
        case Special::Del: in.del = true; break;
        case Special::Esc: in.chars.push_back('`'); break;
        // Arrows are fn+;/.,/ on the Cardputer: lists read the key, chat reads fn+key as scroll.
        case Special::Up: in.fn = true; in.chars.push_back(';'); break;
        case Special::Down: in.fn = true; in.chars.push_back('.'); break;
        case Special::Left: in.fn = true; in.chars.push_back(','); break;
        case Special::Right: in.fn = true; in.chars.push_back('/'); break;
        default: {
            char c = k.ch;
            if (sShift && c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
            in.chars.push_back(c);
            if (sShift) { sShift = false; drawKeyboard(); }
        }
        }
        pushKey(in);
    });
}

void scanTouch() {
    auto t = M5.Touch.getDetail();
    if (t.wasPressed()) onTap(t.x, t.y);
}

} // namespace

namespace Board {

void begin() {
    auto cfg = M5.config();
    cfg.internal_mic = false;
    cfg.internal_imu = false;
    M5.begin(cfg);  // also powers the C6 radio and points esp-hosted at it
    M5.Display.setRotation(0);  // portrait, 720x1280
    M5.Display.fillScreen(0x000000u);
    M5.Speaker.end();

    sCanvas.setPsram(true);
    sCanvas.setColorDepth(16);
    sCanvas.createSprite(240, 135);
    sCanvas.setFont(&fonts::Font0);
    sCanvas.setTextSize(1);
    sCanvas.setTextDatum(textdatum_t::top_left);
    drawKeyboard();

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
    scanTouch();
    if (M5.Speaker.isRunning() && !M5.Speaker.isPlaying() &&
        static_cast<int32_t>(millis() - sLastSoundMs) > static_cast<int32_t>(kSpeakerIdleOffMs)) {
        M5.Speaker.end();
    }
}

M5Canvas& canvas() { return sCanvas; }

void present() {
    if (sDisplayAsleep) return;
    sCanvas.pushRotateZoom(&M5.Display, kAppW / 2, kAppH / 2, 0, kZoom, kZoom);
}

bool pollKeys(KeyInput& out) {
    if (sKeyQueue.empty()) return false;
    out = sKeyQueue.front();
    sKeyQueue.pop_front();
    return true;
}

void scanKeyboard() {
    M5.update();
    scanTouch();
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
    drawKeyboard();
    present();
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
