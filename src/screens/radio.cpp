#include "screens/screens.h"

#include <Arduino.h>
#include <cstdlib>

#include "core/text_util.h"
#include "meshtastic_client.h"
#include "settings.h"

RadioScreen gRadioScreen;
PinScreen gPinScreen;

namespace {

constexpr int kInfoRows = 2;
constexpr int kListRows = Layout::ROWS - kInfoRows;

bool linked() {
    LinkState s = gMesh.state();
    return s == LinkState::Connecting || s == LinkState::Pairing ||
           s == LinkState::Syncing || s == LinkState::Ready;
}

std::string radioLabel(const RadioInfo& r) {
    return TextUtil::toDisplay(r.name.empty() ? r.addr : r.name);
}

} // namespace

void RadioScreen::show() {
    _connectRequested = false;
    _list = ListView();
    if (gMesh.state() == LinkState::Idle || gMesh.state() == LinkState::Off) gMesh.startScan();
    rebuild();
}

void RadioScreen::rebuild() {
    _results = gMesh.scanResults();
    _items.clear();
    const Settings& s = Settings::get();
    std::string saved = s.radioName.empty() ? s.radioAddr : s.radioName;

    if (linked()) {
        _items.push_back({Action::Disconnect, "Disconnect", "", -1});
        _items.push_back({Action::Forget, "Forget this radio", "", -1});
    } else {
        bool scanning = gMesh.state() == LinkState::Scanning;
        _items.push_back({Action::Scan, scanning ? "Scanning..." : "Scan again", "", -1});
        for (int i = 0; i < static_cast<int>(_results.size()); ++i) {
            const RadioInfo& r = _results[i];
            std::string right = std::to_string(r.rssi) + "dB";
            if (r.addr == s.radioAddr) right = "* " + right;
            _items.push_back({Action::Connect, radioLabel(r), right, i});
        }
        if (!s.radioAddr.empty()) {
            _items.push_back({Action::Reconnect, "Reconnect " + TextUtil::toDisplay(saved), "", -1});
            _items.push_back({Action::Forget, "Forget " + TextUtil::toDisplay(saved), "", -1});
        }
    }
    _list.clamp(static_cast<int>(_items.size()), kListRows);
    _seenMesh = gMesh.revision();
}

void RadioScreen::tick() {
    uint32_t now = millis();
    bool scanning = gMesh.state() == LinkState::Scanning;
    if (gMesh.revision() != _seenMesh || (scanning && now - _lastPollMs > 400)) {
        _lastPollMs = now;
        rebuild();
        invalidate();
    }
    if (_connectRequested && gMesh.ready()) {
        _connectRequested = false;
        Nav::pop();
    }
}

void RadioScreen::key(const KeyInput& k) {
    if (k.del || k.esc()) { Nav::pop(); return; }
    if (_list.move(k, static_cast<int>(_items.size()), kListRows)) invalidate();
    bool open = k.enter;
    if (!open || _items.empty()) return;

    const Item& item = _items[_list.sel];
    switch (item.action) {
    case Action::Scan:
        if (gMesh.state() != LinkState::Scanning) gMesh.startScan();
        break;
    case Action::Connect:
        if (item.scanIdx >= 0 && item.scanIdx < static_cast<int>(_results.size())) {
            gMesh.connect(_results[item.scanIdx]);
            _connectRequested = true;
        }
        break;
    case Action::Reconnect:
        gMesh.reconnectNow();
        _connectRequested = true;
        break;
    case Action::Disconnect:
        gMesh.disconnect();
        break;
    case Action::Forget:
        gMesh.forgetRadio();
        break;
    }
    rebuild();
    invalidate();
}

void RadioScreen::draw(M5Canvas& c) {
    UI::header(c, "Radio");
    UI::clearContent(c);

    std::string name = gMesh.radio().addr.empty() ? "none" : radioLabel(gMesh.radio());
    UI::field(c, 0, "Radio", name);
    uint32_t color = gMesh.ready() ? Color::CONNECTED() : (linked() ? Color::SCANNING() : Color::DIM());
    UI::field(c, 1, "Status", TextUtil::toDisplay(gMesh.status()), color);

    for (int i = _list.top; i < static_cast<int>(_items.size()) && _list.visible(i, kListRows); ++i) {
        UI::row(c, i - _list.top + kInfoRows, _items[i].label, i == _list.sel, _items[i].right);
    }
    if (!linked() && _results.empty() && gMesh.state() != LinkState::Scanning) {
        UI::centered(c, "No radios found. Is BLE on?", Color::DIM(), Layout::LINE_H * 2);
    }
    UI::footer(c, "Enter select  Del back");
}

// ---- PIN -----------------------------------------------------------------------

void PinScreen::show() {
    _pin.digitsOnly = true;
    _pin.maxBytes = 6;
    _pin.text = Settings::get().blePin;
}

void PinScreen::tick() {
    if (gMesh.state() != LinkState::Pairing) Nav::pop();
}

void PinScreen::key(const KeyInput& k) {
    if (k.esc()) {
        gMesh.cancelPin();
        return;
    }
    if (k.enter && !_pin.text.empty()) {
        gMesh.submitPin(static_cast<uint32_t>(strtoul(_pin.text.c_str(), nullptr, 10)));
        return;
    }
    if (_pin.handle(k)) invalidate();
}

void PinScreen::draw(M5Canvas& c) {
    UI::header(c, "Pair radio");
    UI::clearContent(c);
    UI::centered(c, radioLabel(gMesh.radio()), Color::TEXT(), -30);
    UI::centered(c, "Type the PIN shown on the radio", Color::DIM(), -16);

    std::string shown = _pin.text;
    while (shown.size() < 6) shown += '_';
    c.setTextSize(2);
    c.setTextColor(Color::ACCENT(), Color::BG());
    int w = static_cast<int>(shown.size()) * Layout::CHAR_W * 2;
    c.drawString(shown.c_str(), (Layout::SCREEN_W - w) / 2, Layout::CONTENT_Y + Layout::CONTENT_H / 2);
    c.setTextSize(1);

    UI::footer(c, "Enter pair  ` cancel  default 123456");
}
