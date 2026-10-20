#include "screens/screens.h"

#include <Arduino.h>

#include "board.h"
#include "core/mesh_protocol.h"
#include "core/text_util.h"
#include "meshtastic_client.h"
#include "settings.h"
#include "theme.h"

SettingsScreen gSettingsScreen;

namespace {

enum Row : int {
    kName, kShort, kSound, kVolume, kPin, kAutoConnect,
    kBrightness, kDimAfter, kScreenOff, kTheme, kRadio, kAbout, kRowCount
};

constexpr uint32_t kNoticeMs = 4000;
constexpr uint8_t kBrightnessSteps[] = {40, 80, 140, 200, 255};
constexpr uint16_t kDimSteps[] = {10, 20, 30, 60, 0};
constexpr uint16_t kOffSteps[] = {30, 60, 120, 300, 0};
#ifndef MESHPUTER_VERSION
#define MESHPUTER_VERSION "dev"
#endif
const char* const kAppVersion = MESHPUTER_VERSION;

template <typename T, size_t N>
T nextStep(const T (&steps)[N], T current) {
    for (size_t i = 0; i < N; ++i) {
        if (steps[i] == current) return steps[(i + 1) % N];
    }
    return steps[0];
}

std::string seconds(uint16_t s) { return s == 0 ? "never" : std::to_string(s) + "s"; }
std::string onOff(bool v) { return v ? "on" : "off"; }

std::string myLongName() {
    const MeshNode* me = gMesh.node(gMesh.myNodeNum());
    return me && me->hasUser ? me->user.longName : "";
}

std::string myShortName() {
    const MeshNode* me = gMesh.node(gMesh.myNodeNum());
    return me && me->hasUser ? me->user.shortName : "";
}

} // namespace

void SettingsScreen::show() {
    _editing = -1;
    _notice.clear();
}

void SettingsScreen::tick() {
    if (gMesh.revision() != _seenMesh) {
        _seenMesh = gMesh.revision();
        invalidate();
    }
    if (!_notice.empty() && millis() - _noticeAt > kNoticeMs) {
        _notice.clear();
        invalidate();
    }
}

void SettingsScreen::activate(int row) {
    Settings& s = Settings::get();
    switch (row) {
    case kName:
    case kShort:
        if (!gMesh.ready()) {
            _notice = "Connect a radio to change identity";
            _noticeAt = millis();
            return;
        }
        _editing = row;
        _edit.digitsOnly = false;
        _edit.maxBytes = row == kName ? MeshProto::kMaxLongNameBytes : MeshProto::kMaxShortNameBytes;
        _edit.text = row == kName ? myLongName() : myShortName();
        return;
    case kPin:
        _editing = row;
        _edit.digitsOnly = true;
        _edit.maxBytes = 6;
        _edit.text = s.blePin;
        return;
    case kSound:       s.soundEnabled = !s.soundEnabled; break;
    case kVolume:      s.volume = s.volume >= 224 ? 32 : s.volume + 32; break;
    case kAutoConnect: s.autoConnect = !s.autoConnect; break;
    case kBrightness:
        s.brightness = nextStep(kBrightnessSteps, s.brightness);
        Board::setBacklight(s.brightness);
        break;
    case kDimAfter:    s.dimAfterSec = nextStep(kDimSteps, s.dimAfterSec); break;
    case kScreenOff:   s.screenOffSec = nextStep(kOffSteps, s.screenOffSec); break;
    case kTheme:       s.themeIndex = (s.themeIndex + 1) % kThemeCount; break;
    case kRadio:       Nav::push(&gRadioScreen); return;
    default:           return;
    }
    s.save();
}

void SettingsScreen::commitEdit() {
    Settings& s = Settings::get();
    std::string text = _edit.text;
    int row = _editing;
    _editing = -1;
    if (row == kPin) {
        if (text.empty()) return;
        while (text.size() < 6) text = "0" + text;
        s.blePin = text;
        s.save();
        return;
    }
    if (text.empty()) {
        _notice = "Name cannot be empty";
        _noticeAt = millis();
        return;
    }
    std::string longName = row == kName ? text : myLongName();
    std::string shortName = row == kShort ? text : myShortName();
    if (longName.empty()) longName = shortName;
    if (shortName.empty()) shortName = TextUtil::truncateUtf8(longName, MeshProto::kMaxShortNameBytes);
    gMesh.setOwner(longName, shortName);
}

void SettingsScreen::key(const KeyInput& k) {
    if (_editing >= 0) {
        if (k.esc()) { _editing = -1; invalidate(); return; }
        if (k.enter) { commitEdit(); invalidate(); return; }
        if (_edit.handle(k)) invalidate();
        return;
    }
    if (k.del || k.esc()) { Nav::pop(); return; }
    if (_list.move(k, kRowCount)) invalidate();
    if (k.enter) {
        activate(_list.sel);
        invalidate();
    }
}

void SettingsScreen::draw(M5Canvas& c) {
    UI::header(c, "Settings");
    UI::clearContent(c);
    const Settings& s = Settings::get();

    for (int i = _list.top; i < kRowCount && _list.visible(i); ++i) {
        const char* label = "";
        std::string value;
        switch (i) {
        case kName:
            label = "Name";
            value = gMesh.ready() ? TextUtil::toDisplay(myLongName()) : "(connect radio)";
            break;
        case kShort:
            label = "Short name";
            value = gMesh.ready() ? TextUtil::toDisplay(myShortName()) : "";
            break;
        case kSound:       label = "Sound"; value = onOff(s.soundEnabled); break;
        case kVolume:      label = "Volume"; value = std::to_string(s.volume); break;
        case kPin:         label = "Radio PIN"; value = s.blePin; break;
        case kAutoConnect: label = "Auto-connect"; value = onOff(s.autoConnect); break;
        case kBrightness:  label = "Brightness"; value = std::to_string(s.brightness); break;
        case kDimAfter:    label = "Dim after"; value = seconds(s.dimAfterSec); break;
        case kScreenOff:   label = "Screen off"; value = seconds(s.screenOffSec); break;
        case kTheme:       label = "Theme"; value = kThemes[s.themeIndex % kThemeCount]->name; break;
        case kRadio:
            label = "Radio";
            value = s.radioName.empty() ? (s.radioAddr.empty() ? "none" : s.radioAddr) : TextUtil::toDisplay(s.radioName);
            break;
        case kAbout:
            label = "About";
            value = std::string(kAppVersion) + "  " + std::to_string(ESP.getFreeHeap() / 1024) + "k free";
            break;
        }
        if (i == _editing) value = TextUtil::toDisplay(_edit.text) + "_";
        UI::row(c, i - _list.top, label, i == _list.sel, TextUtil::ellipsize(value, 20));
    }

    std::string footer = "Enter change  Del back";
    if (_editing >= 0) footer = "type  Enter save  ` cancel";
    else if (!_notice.empty()) footer = _notice;
    else if (gMesh.ownerUpdate() != OwnerUpdate::Idle) footer = "Identity: " + gMesh.ownerMessage();
    UI::footer(c, footer);
}
