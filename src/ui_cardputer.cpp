// Cardputer UI: keyboard-driven screens on the 240x135 canvas (src/screens).
#if !defined(MESHPUTER_BOARD_TAB5)

#include "app_ui.h"

#include <Arduino.h>
#include <cstdlib>
#include <deque>

#include "board.h"
#include "chat_service.h"
#include "core/mesh_protocol.h"
#include "meshtastic_client.h"
#include "power.h"
#include "screens/screens.h"

namespace {

constexpr uint32_t kHeaderRefreshMs = 30000;  // battery level in the header

uint32_t sSeenMesh = 0;
uint32_t sSeenChat = 0;
bool sToastShown = false;
uint32_t sLastFrameMs = 0;
std::deque<KeyInput> sInjected;  // keys typed from the serial console

bool popInjected(KeyInput& out) {
    if (sInjected.empty()) return false;
    out = sInjected.front();
    sInjected.pop_front();
    return true;
}

bool parseNode(const std::string& s, uint32_t& out) {
    std::string hex = (!s.empty() && s[0] == '!') ? s.substr(1) : s;
    if (hex.empty() || hex.size() > 8) return false;
    char* end = nullptr;
    out = static_cast<uint32_t>(strtoul(hex.c_str(), &end, 16));
    return end && *end == '\0';
}

} // namespace

namespace AppUi {

void begin() {
    Nav::reset(&gHomeScreen);
}

void loop() {
    // The pairing prompt appears over whatever is on screen.
    if (gMesh.state() == LinkState::Pairing && Nav::current() != &gPinScreen) {
        Power::onKey();
        Nav::push(&gPinScreen);
    }

    KeyInput keys;
    bool injected = popInjected(keys);
    if (injected || Board::pollKeys(keys)) {
        bool wokeScreen = Power::onKey();
        if (injected || !wokeScreen) Nav::current()->key(keys);
    }

    Nav::current()->tick();
    Screen* screen = Nav::current();  // tick() may have navigated

    // Redraw on any change the current screen might show.
    bool toast = UI::toastVisible();
    uint32_t now = millis();
    if (gMesh.revision() != sSeenMesh || Chat::store().revision() != sSeenChat ||
        toast != sToastShown || now - sLastFrameMs > kHeaderRefreshMs) {
        sSeenMesh = gMesh.revision();
        sSeenChat = Chat::store().revision();
        sToastShown = toast;
        screen->invalidate();
    }
    if (Power::screenOn() && screen->takeDirty()) {
        screen->draw(Board::canvas());
        Board::scanKeyboard();  // a redraw takes tens of ms; don't miss quick taps
        Board::present();
        Board::scanKeyboard();
        sLastFrameMs = now;
    }
}

const char* screenName() { return Nav::current() ? Nav::current()->name() : "-"; }

void home() { Nav::reset(&gHomeScreen); }

std::string open(const std::string& what, const std::string& rest) {
    if (what == "home" || what == "chats") Nav::reset(&gHomeScreen);
    else if (what == "nodes") Nav::push(&gNodesScreen);
    else if (what == "channels") Nav::push(&gChannelsScreen);
    else if (what == "settings") Nav::push(&gSettingsScreen);
    else if (what == "radio") Nav::push(&gRadioScreen);
    else if (what == "chat") {
        int idx = atoi(rest.c_str());
        if (!Chat::store().at(idx)) return "no such conversation";
        openChat(idx);
    } else if (what == "node") {
        uint32_t num;
        if (!parseNode(rest, num)) return "bad node id";
        gNodeInfoScreen.open(num);
        Nav::push(&gNodeInfoScreen);
    } else {
        return "unknown screen";
    }
    return "";
}

std::string key(const std::string& spec) {
    KeyInput k;
    k.any = true;
    if (spec == "enter") k.enter = true;
    else if (spec == "del") k.del = true;
    else if (spec == "esc") k.chars.push_back('`');
    else if (spec == "up") k.chars.push_back(';');
    else if (spec == "down") k.chars.push_back('.');
    else if (spec == "left") k.chars.push_back(',');
    else if (spec == "right") k.chars.push_back('/');
    else if (spec.size() == 4 && spec.compare(0, 3, "fn+") == 0) { k.fn = true; k.chars.push_back(spec[3]); }
    else {
        // Literal text: one key event per character, like typing.
        for (char ch : spec) {
            KeyInput t;
            t.any = true;
            t.chars.push_back(ch);
            sInjected.push_back(t);
        }
        return "";
    }
    sInjected.push_back(k);
    return "";
}

std::string tap(int, int) { return "no touch screen"; }
std::string swipe(int, int, int, int) { return "no touch screen"; }
std::string rotate(int) { return "fixed orientation"; }

void dumpFrame(int step) {
    if (step < 1) step = 1;
    M5Canvas& c = Board::canvas();
    const uint8_t* buf = static_cast<const uint8_t*>(c.getBuffer());
    int w = c.width(), h = c.height();
    Serial.printf("SHOT %d %d rgb565be\n", (w + step - 1) / step, (h + step - 1) / step);
    static char line[240 * 4 + 2];
    static const char* kHex = "0123456789abcdef";
    for (int y = 0; y < h; y += step) {
        const uint8_t* row = buf + static_cast<size_t>(y) * w * 2;  // canvas rows are big-endian RGB565
        int n = 0;
        for (int x = 0; x < w; x += step) {
            for (int b = 0; b < 2; ++b) {
                line[n++] = kHex[row[x * 2 + b] >> 4];
                line[n++] = kHex[row[x * 2 + b] & 0xF];
            }
        }
        line[n] = '\0';
        Serial.println(line);
    }
    Serial.println("SHOT END");
}

} // namespace AppUi

#endif
