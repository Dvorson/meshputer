// Settings: who we are on the mesh, internet chats (Nostr over Wi-Fi), the
// display, sound, quick replies, and device facts.
#if defined(MESHPUTER_BOARD_TAB5)

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "board.h"
#include "core/mesh_protocol.h"
#include "core/text_util.h"
#include "kit.h"
#include "meshtastic_client.h"
#include "nostr_service.h"
#include "settings.h"
#include "sound.h"
#include "theme.h"
#include "views.h"

#ifndef MESHPUTER_VERSION
#define MESHPUTER_VERSION "dev"
#endif

namespace Tab5 {

namespace {

enum Section : int { kProfile, kInternet, kDisplay, kSound, kReplies, kAbout, kSectionCount };

struct SectionInfo {
    const char* name;   // console
    const char* title;
    const char* icon;
};

const SectionInfo kSections[kSectionCount] = {
    {"profile", "Profile", LV_SYMBOL_EDIT},
    {"internet", "Internet", LV_SYMBOL_WIFI},
    {"display", "Display", LV_SYMBOL_IMAGE},
    {"sound", "Sound", LV_SYMBOL_VOLUME_MAX},
    {"replies", "Quick replies", LV_SYMBOL_LIST},
    {"about", "About", LV_SYMBOL_HOME},
};

constexpr uint16_t kDimValues[] = {10, 20, 30, 60, 0};
const char* const kDimMap[] = {"10 s", "20 s", "30 s", "1 min", "Never", ""};
constexpr uint16_t kOffValues[] = {30, 60, 120, 300, 0};
const char* const kOffMap[] = {"30 s", "1 min", "2 min", "5 min", "Never", ""};
const char* const kThemeMap[] = {"Dark", "Light", "OLED", ""};
const char* const kOrientationMap[] = {"Auto", "Portrait", "Landscape", ""};
constexpr uint32_t kRefreshMs = 1000;
constexpr size_t kReplyMaxChars = 60;

template <size_t N>
int indexOf(const uint16_t (&values)[N], uint16_t v) {
    for (size_t i = 0; i < N; ++i) if (values[i] == v) return static_cast<int>(i);
    return -1;
}

std::string myLongName() {
    const MeshNode* me = gMesh.node(gMesh.myNodeNum());
    return me && me->hasUser ? me->user.longName : "";
}

std::string myShortName() {
    const MeshNode* me = gMesh.node(gMesh.myNodeNum());
    return me && me->hasUser ? me->user.shortName : "";
}

std::string summary(int section) {
    const Settings& s = Settings::get();
    switch (section) {
    case kProfile: {
        std::string n = myLongName();
        return n.empty() ? "Connect a radio to set your name" : TextUtil::toDisplay(n);
    }
    case kInternet: return s.internetEnabled ? TextUtil::toDisplay(NostrService::status()) : "Off";
    case kDisplay:
        return std::string(kThemes[s.themeIndex % kThemeCount]->name) + ", " + kOrientationMap[s.orientation % 3] +
               (s.orientation == 0 ? " rotation" : "");
    case kSound: return s.soundEnabled ? "On, volume " + std::to_string(s.volume * 100 / 255) + "%" : "Off";
    case kReplies: {
        int n = 0;
        for (const auto& c : s.canned) if (!c.empty()) ++n;
        return std::to_string(n) + " replies";
    }
    case kAbout: return std::string("Meshputer ") + MESHPUTER_VERSION;
    }
    return "";
}

std::string memory() {
    char buf[64];
    snprintf(buf, sizeof(buf), "%u KB RAM, %u KB PSRAM free",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    return buf;
}

class SettingsView : public View {
public:
    const char* name() const override { return "settings"; }
    const char* detailName() const override {
        int s = route().section;
        return s >= 0 && s < kSectionCount ? kSections[s].name : "section";
    }
    const char* label() const override { return "Settings"; }
    const char* icon() const override { return LV_SYMBOL_SETTINGS; }

    bool hasDetail() const override { return route().section >= 0 && route().section < kSectionCount; }
    void buildList(Pane& pane) override;
    void buildDetail(Pane& pane) override;
    void update() override;
    void cleared(uint8_t parts) override {
        if (parts & kList) _list = nullptr;
        if (parts & kDetail) {
            _ownerStatus = _netStatus = _long = _short = _ssid = _pass = nullptr;
            for (auto& r : _replies) r = nullptr;
        }
    }

private:
    void buildRows();
    void buildProfile(lv_obj_t* page);
    void buildInternet(lv_obj_t* page);
    void buildDisplay(lv_obj_t* page);
    void buildSound(lv_obj_t* page);
    void buildReplies(lv_obj_t* page);
    void buildAbout(lv_obj_t* page);

    static void onRow(lv_event_t* e);
    static void onSaveProfile(lv_event_t*);
    static void onInternet(lv_event_t* e);
    static void onSaveWifi(lv_event_t*);
    static void onBrightness(lv_event_t* e);
    static void onDim(lv_event_t* e);
    static void onScreenOff(lv_event_t* e);
    static void onTheme(lv_event_t* e);
    static void onOrientation(lv_event_t* e);
    static void onSound(lv_event_t* e);
    static void onVolume(lv_event_t* e);
    static void onSaveReplies(lv_event_t*);

    lv_obj_t* _list = nullptr;
    lv_obj_t* _ownerStatus = nullptr;
    lv_obj_t* _netStatus = nullptr;
    lv_obj_t* _long = nullptr;
    lv_obj_t* _short = nullptr;
    lv_obj_t* _ssid = nullptr;
    lv_obj_t* _pass = nullptr;
    lv_obj_t* _replies[kMaxCannedResponses] = {};
    bool _profileReady = false;  // the profile page was built with the radio connected
    std::string _rowsSig;
    std::string _ownerShown;
    std::string _netShown;
    uint32_t _checkedAt = 0;
};

SettingsView sView;

// ---- section list -----------------------------------------------------------------

void SettingsView::buildList(Pane& pane) {
    pane.setTitle("Settings");
    if (landscape() && route().section < 0) route().section = kProfile;
    _list = Kit::scroller(pane.body);
    buildRows();
}

void SettingsView::buildRows() {
    const Kit::Palette& p = Kit::pal();
    lv_obj_clean(_list);
    _rowsSig.clear();
    for (int i = 0; i < kSectionCount; ++i) {
        std::string sum = summary(i);
        _rowsSig += sum + "|";
        bool selected = landscape() && route().section == i;
        lv_obj_t* row = Kit::listRow(_list, selected, onRow, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_t* badge = Kit::box(row);
        lv_obj_set_size(badge, 64, 64);
        lv_obj_set_style_radius(badge, 20, 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(badge, p.tint, 0);
        lv_obj_center(Kit::text(badge, kSections[i].icon, Kit::strong(), p.accent));
        lv_obj_t* words = Kit::column(row, 4);
        lv_obj_set_width(words, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(words, 1);
        Kit::text(words, kSections[i].title, Kit::strong(), p.text);
        lv_obj_t* s = Kit::text(words, sum, Kit::small(), p.text2);
        lv_obj_set_width(s, lv_pct(100));
        Kit::oneLine(s);
        if (!landscape()) Kit::text(row, LV_SYMBOL_RIGHT, Kit::body(), p.dim);
    }
}

void SettingsView::onRow(lv_event_t* e) {
    openSection(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));
}

// ---- sections -------------------------------------------------------------------

void SettingsView::buildDetail(Pane& pane) {
    int section = route().section;
    pane.setTitle(kSections[section].title);
    lv_obj_t* page = Kit::scroller(pane.body, 20);
    lv_obj_set_style_pad_all(page, Kit::kPad, 0);
    lv_obj_set_style_pad_bottom(page, 48, 0);
    switch (section) {
    case kProfile: pane.setSubtitle("How others see you on the mesh"); buildProfile(page); break;
    case kInternet: pane.setSubtitle("Private messages over Nostr"); buildInternet(page); break;
    case kDisplay: buildDisplay(page); break;
    case kSound: buildSound(page); break;
    case kReplies: pane.setSubtitle("One tap to send from any chat"); buildReplies(page); break;
    case kAbout: buildAbout(page); break;
    }
}

void SettingsView::buildProfile(lv_obj_t* page) {
    const Kit::Palette& p = Kit::pal();
    bool ready = gMesh.ready();
    _profileReady = ready;
    lv_obj_t* card = Kit::card(page);
    _long = Kit::input(card, "Name", myLongName(), MeshProto::kMaxLongNameBytes, "e.g. Anna on the hill");
    _short = Kit::input(card, "Short name (up to 4 characters)", myShortName(), MeshProto::kMaxShortNameBytes,
                        "ANNA");
    lv_obj_add_event_cb(_long, onSaveProfile, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(_short, onSaveProfile, LV_EVENT_READY, nullptr);
    lv_obj_t* actions = Kit::hrow(card, 16);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _ownerStatus = Kit::text(actions, "", Kit::small(), p.text2);
    lv_obj_set_flex_grow(_ownerStatus, 1);
    lv_obj_t* save = Kit::button(actions, "Save", Kit::Btn::Primary, onSaveProfile);
    if (!ready) {
        lv_obj_add_state(save, LV_STATE_DISABLED);
        lv_obj_add_state(_long, LV_STATE_DISABLED);
        lv_obj_add_state(_short, LV_STATE_DISABLED);
        Kit::para(page, "Your name is stored on your radio. Connect it in Radio to change it.", Kit::body(), p.warn);
    } else {
        Kit::para(page, "Saved to your radio " + TextUtil::toDisplay(gMesh.radio().name) + " (" +
                            MeshProto::nodeIdString(gMesh.myNodeNum()) +
                            "). Other nodes learn the new name the next time it announces itself.",
                  Kit::small(), p.text2);
    }
    _ownerShown.clear();
}

void SettingsView::onSaveProfile(lv_event_t*) {
    if (!sView._long || !sView._short) return;
    closeKeyboard();
    if (!gMesh.ready()) {
        toast("Connect a radio first");
        return;
    }
    std::string longName = lv_textarea_get_text(sView._long);
    std::string shortName = lv_textarea_get_text(sView._short);
    if (longName.empty() && shortName.empty()) {
        toast("The name cannot be empty");
        return;
    }
    if (longName.empty()) longName = shortName;
    if (shortName.empty()) shortName = TextUtil::truncateUtf8(longName, MeshProto::kMaxShortNameBytes);
    if (!gMesh.setOwner(longName, shortName)) toast(gMesh.ownerMessage());
}

void SettingsView::buildInternet(lv_obj_t* page) {
    const Kit::Palette& p = Kit::pal();
    const Settings& s = Settings::get();
    lv_obj_t* card = Kit::card(page);
    Kit::toggle(card, "Internet chats", "Powers Wi-Fi to reach Nostr relays. Off, Wi-Fi stays unpowered.",
                s.internetEnabled, onInternet);
    _netStatus = Kit::para(card, "", Kit::small(), p.text2);
    _netShown.clear();

    Kit::sectionLabel(page, "WI-FI");
    lv_obj_t* wifi = Kit::card(page);
    _ssid = Kit::input(wifi, "Network", s.wifiSsid, 32, "Network name");
    _pass = Kit::input(wifi, "Password", s.wifiPassword, 63, "Password", false, true);
    lv_obj_add_event_cb(_pass, onSaveWifi, LV_EVENT_READY, nullptr);
    lv_obj_t* actions = Kit::hrow(wifi, 16);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    Kit::button(actions, "Save", Kit::Btn::Primary, onSaveWifi);

    Kit::sectionLabel(page, "THIS DEVICE ON NOSTR");
    lv_obj_t* id = Kit::card(page);
    std::string npub = NostrService::npub();
    if (npub.empty()) {
        Kit::para(id, "Turn internet chats on once to create this device's identity.", Kit::body(), p.text2);
        return;
    }
    lv_obj_t* row = Kit::hrow(id, 28);
    lv_obj_set_flex_flow(row, landscape() ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_t* frame = Kit::box(row);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(frame, lv_color_white(), 0);
    lv_obj_set_style_pad_all(frame, 20, 0);
    lv_obj_set_style_radius(frame, 20, 0);
    lv_obj_t* qr = lv_qrcode_create(frame);
    lv_qrcode_set_size(qr, 280);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    std::string uri = "nostr:" + npub;
    lv_qrcode_update(qr, uri.c_str(), uri.size());
    lv_obj_t* words = Kit::column(row, 12);
    if (landscape()) {
        lv_obj_set_width(words, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(words, 1);
    }
    Kit::para(words, "Scan with a Nostr app (Amethyst, 0xchat, Damus...) to message this device.", Kit::body(),
              p.text);
    Kit::para(words, npub, Kit::small(), p.text2);
}

void SettingsView::onInternet(lv_event_t* e) {
    NostrService::setEnabled(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
    invalidate(kDetail);  // the identity may have just been created
}

void SettingsView::onSaveWifi(lv_event_t*) {
    if (!sView._ssid || !sView._pass) return;
    closeKeyboard();
    Settings& s = Settings::get();
    s.wifiSsid = lv_textarea_get_text(sView._ssid);
    s.wifiPassword = lv_textarea_get_text(sView._pass);
    s.save();
    if (s.internetEnabled) {  // rejoin with the new credentials
        NostrService::setEnabled(false);
        NostrService::setEnabled(true);
    }
    toast("Wi-Fi saved");
}

void SettingsView::buildDisplay(lv_obj_t* page) {
    const Kit::Palette& p = Kit::pal();
    const Settings& s = Settings::get();
    lv_obj_t* card = Kit::card(page);
    Kit::text(card, "Brightness", Kit::body(), p.text);
    Kit::slider(card, 8, 255, s.brightness, onBrightness);

    Kit::text(card, "Screen layout", Kit::body(), p.text);
    Kit::segmented(card, kOrientationMap, s.orientation % 3, onOrientation);
    Kit::para(card, "Auto turns the screen with the device: side by side in landscape, one page at a time in "
                    "portrait.", Kit::small(), p.text2);

    Kit::text(card, "Theme", Kit::body(), p.text);
    Kit::segmented(card, kThemeMap, s.themeIndex % kThemeCount, onTheme);

    lv_obj_t* power = Kit::card(page);
    Kit::text(power, "Dim after", Kit::body(), p.text);
    Kit::segmented(power, kDimMap, indexOf(kDimValues, s.dimAfterSec), onDim);
    Kit::text(power, "Screen off after", Kit::body(), p.text);
    Kit::segmented(power, kOffMap, indexOf(kOffValues, s.screenOffSec), onScreenOff);
    Kit::para(power, "A touch wakes the screen; new messages light it up dimmed.", Kit::small(), p.text2);
}

void SettingsView::onBrightness(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) return;
    Settings& s = Settings::get();
    s.brightness = static_cast<uint8_t>(lv_slider_get_value(lv_event_get_target_obj(e)));
    Board::setBacklight(s.brightness);
    if (code == LV_EVENT_RELEASED) s.save();
}

void SettingsView::onDim(lv_event_t* e) {
    uint32_t i = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
    if (i >= sizeof(kDimValues) / sizeof(kDimValues[0])) return;
    Settings& s = Settings::get();
    s.dimAfterSec = kDimValues[i];
    s.save();
}

void SettingsView::onScreenOff(lv_event_t* e) {
    uint32_t i = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
    if (i >= sizeof(kOffValues) / sizeof(kOffValues[0])) return;
    Settings& s = Settings::get();
    s.screenOffSec = kOffValues[i];
    s.save();
}

void SettingsView::onTheme(lv_event_t* e) {
    uint32_t i = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
    if (i >= static_cast<uint32_t>(kThemeCount)) return;
    Settings& s = Settings::get();
    s.themeIndex = static_cast<uint8_t>(i);
    s.save();
    invalidate(kAll);  // every widget takes its colours from the theme
}

void SettingsView::onOrientation(lv_event_t* e) {
    uint32_t i = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
    if (i > 2) return;
    Settings& s = Settings::get();
    s.orientation = static_cast<uint8_t>(i);
    s.save();
}

void SettingsView::buildSound(lv_obj_t* page) {
    const Kit::Palette& p = Kit::pal();
    const Settings& s = Settings::get();
    lv_obj_t* card = Kit::card(page);
    Kit::toggle(card, "Sounds", "New messages, sent, connected", s.soundEnabled, onSound);
    Kit::text(card, "Volume", Kit::body(), p.text);
    Kit::slider(card, 16, 255, s.volume, onVolume);
}

void SettingsView::onSound(lv_event_t* e) {
    Settings& s = Settings::get();
    s.soundEnabled = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    s.save();
    Sound::message();
}

void SettingsView::onVolume(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
    Settings& s = Settings::get();
    s.volume = static_cast<uint8_t>(lv_slider_get_value(lv_event_get_target_obj(e)));
    s.save();
    Sound::message();
}

void SettingsView::buildReplies(lv_obj_t* page) {
    const Settings& s = Settings::get();
    lv_obj_t* card = Kit::card(page);
    for (int i = 0; i < kMaxCannedResponses; ++i) {
        _replies[i] = Kit::input(card, "", TextUtil::toDisplay(s.canned[i]), kReplyMaxChars, "(empty)");
        lv_obj_add_event_cb(_replies[i], onSaveReplies, LV_EVENT_READY, nullptr);
    }
    lv_obj_t* actions = Kit::hrow(page, 16);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    Kit::button(actions, "Save", Kit::Btn::Primary, onSaveReplies);
}

void SettingsView::onSaveReplies(lv_event_t*) {
    Settings& s = Settings::get();
    for (int i = 0; i < kMaxCannedResponses; ++i) {
        if (sView._replies[i]) s.canned[i] = lv_textarea_get_text(sView._replies[i]);
    }
    s.save();
    closeKeyboard();
    toast("Quick replies saved");
}

void SettingsView::buildAbout(lv_obj_t* page) {
    const Kit::Palette& p = Kit::pal();
    lv_obj_t* card = Kit::card(page);
    lv_obj_set_style_pad_row(card, 4, 0);
    Kit::field(card, "Version", MESHPUTER_VERSION);
    Kit::field(card, "Device", "M5Stack Tab5");
    int battery = Board::batteryPercent();
    Kit::field(card, "Battery", battery < 0 ? "not fitted" : std::to_string(battery) + "%");
    Kit::field(card, "History", Board::sdReady() ? "stored on the device" : "not stored");
    Kit::field(card, "Memory", memory());
    Kit::field(card, "Up for", std::to_string(millis() / 60000) + " min");
    Kit::para(page, "Meshputer is free software (GPL-3.0): github.com/Dvorson/meshputer", Kit::small(), p.text2);
}

// ---- refresh --------------------------------------------------------------------

void SettingsView::update() {
    if (millis() - _checkedAt < kRefreshMs) return;
    _checkedAt = millis();
    if (_list) {
        std::string sig;
        for (int i = 0; i < kSectionCount; ++i) sig += summary(i) + "|";
        if (sig != _rowsSig) buildRows();
    }
    if (_ownerStatus && gMesh.ready() != _profileReady) {
        invalidate(kDetail);  // the name fields depend on the radio being connected
        return;
    }
    if (_ownerStatus) {
        std::string msg = gMesh.ownerUpdate() == OwnerUpdate::Idle ? "" : TextUtil::toDisplay(gMesh.ownerMessage());
        if (msg != _ownerShown) {
            _ownerShown = msg;
            lv_label_set_text(_ownerStatus, msg.c_str());
        }
    }
    if (_netStatus) {
        std::string msg = Settings::get().internetEnabled
                              ? TextUtil::toDisplay(NostrService::status()) + ", " +
                                    std::to_string(NostrService::relaysOnline()) + " relay(s) online"
                              : "Off";
        if (msg != _netShown) {
            _netShown = msg;
            lv_label_set_text(_netStatus, msg.c_str());
        }
    }
}

} // namespace

View& settingsView() { return sView; }

} // namespace Tab5

#endif
