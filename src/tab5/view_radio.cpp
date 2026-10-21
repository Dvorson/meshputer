// Radio: the Bluetooth link to the Meshtastic radio — its state, connect /
// disconnect / forget, pairing options, and a scan for radios nearby.
#if defined(MESHPUTER_BOARD_TAB5)

#include <cstdlib>
#include <vector>

#include "core/text_util.h"
#include "kit.h"
#include "meshtastic_client.h"
#include "settings.h"
#include "views.h"

namespace Tab5 {

namespace {

constexpr uint32_t kRefreshMs = 500;

bool busy(LinkState s) {
    return s == LinkState::Connecting || s == LinkState::Pairing || s == LinkState::Syncing;
}

class RadioView : public View {
public:
    const char* name() const override { return "radio"; }
    const char* detailName() const override { return "link"; }
    const char* label() const override { return "Radio"; }
    const char* icon() const override { return LV_SYMBOL_BLUETOOTH; }

    bool hasDetail() const override { return landscape(); }
    void buildList(Pane& pane) override;
    void buildDetail(Pane& pane) override;
    void update() override;
    void cleared(uint8_t parts) override {
        if (parts & kList) _nearby = _scanActions = nullptr;
        if ((parts & kDetail) || ((parts & kList) && !landscape())) _status = _pin = nullptr;
    }

private:
    std::string signature() const;
    void buildStatus();
    void buildConnection(lv_obj_t* parent);
    void buildNearby();
    void buildScanActions();

    static void onConnectSaved(lv_event_t*) { gMesh.reconnectNow(); }
    static void onDisconnect(lv_event_t*) { gMesh.disconnect(); }
    static void onForget(lv_event_t*) { gMesh.forgetRadio(); }
    static void onScan(lv_event_t*) { gMesh.startScan(); }
    static void onStopScan(lv_event_t*) { gMesh.stopScan(); }
    static void onResult(lv_event_t* e);
    static void onAutoConnect(lv_event_t* e);
    static void onPin(lv_event_t* e);

    lv_obj_t* _status = nullptr;
    lv_obj_t* _pin = nullptr;
    lv_obj_t* _nearby = nullptr;
    lv_obj_t* _scanActions = nullptr;
    std::vector<RadioInfo> _results;
    std::string _sig;
    uint32_t _builtAt = 0;
};

RadioView sView;

std::string RadioView::signature() const {
    std::string s = linkStateLabel(gMesh.state());
    s += "|" + gMesh.status() + "|" + std::to_string(gMesh.reconnectInMs() / 1000) + "|" + gMesh.radio().name;
    for (const auto& r : gMesh.scanResults()) s += "|" + r.addr + r.name + std::to_string(r.rssi / 6);
    return s;
}

// Landscape: the radios nearby on the left (a list), the link and its
// options on the right. Portrait: all of it on one page.
void RadioView::buildList(Pane& pane) {
    pane.setTitle("Radio");
    if (landscape()) {
        pane.setSubtitle("Nearby, over Bluetooth");
        _scanActions = Kit::box(pane.actions);
        lv_obj_t* page = Kit::scroller(pane.body, 0);
        _nearby = Kit::column(page, 0);
    } else {
        pane.setSubtitle("Meshtastic over Bluetooth");
        lv_obj_t* page = Kit::scroller(pane.body, 20);
        lv_obj_set_style_pad_all(page, Kit::kPad, 0);
        _status = Kit::card(page);
        buildConnection(page);
        lv_obj_t* head = Kit::hrow(page, 16);
        lv_obj_set_style_pad_top(head, 12, 0);
        lv_obj_t* l = Kit::sectionLabel(head, "NEARBY RADIOS");
        lv_obj_set_flex_grow(l, 1);
        _scanActions = Kit::box(head);
        _nearby = Kit::column(page, 0);
    }
    _sig.clear();
    _builtAt = 0;
    update();
}

void RadioView::buildDetail(Pane& pane) {
    pane.setTitle("Connection");
    pane.setSubtitle("Meshtastic over Bluetooth");
    lv_obj_t* page = Kit::scroller(pane.body, 20);
    lv_obj_set_style_pad_all(page, Kit::kPad, 0);
    _status = Kit::card(page);
    buildConnection(page);
    _sig.clear();
    _builtAt = 0;
    update();
}

void RadioView::buildStatus() {
    const Kit::Palette& p = Kit::pal();
    lv_obj_clean(_status);
    LinkState st = gMesh.state();
    bool saved = gMesh.hasSavedRadio();

    lv_obj_t* top = Kit::hrow(_status, 24);
    lv_obj_t* badge = Kit::box(top);
    lv_obj_set_size(badge, 112, 112);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, lv_color_mix(Kit::linkColor(), p.surface, 70), 0);
    lv_obj_center(Kit::text(badge, LV_SYMBOL_BLUETOOTH, Kit::display(), Kit::linkColor()));

    lv_obj_t* words = Kit::column(top, 6);
    lv_obj_set_width(words, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(words, 1);
    std::string name = gMesh.radio().name.empty() ? Settings::get().radioName : gMesh.radio().name;
    Kit::para(words, saved ? (name.empty() ? Settings::get().radioAddr : TextUtil::toDisplay(name)) : "No radio",
              Kit::title(), p.text);
    Kit::text(words, Kit::linkText(), Kit::strong(), Kit::linkColor());
    std::string detail = gMesh.status() == Kit::linkText() ? "" : gMesh.status();
    uint32_t retry = gMesh.reconnectInMs();
    if (retry > 0 && st != LinkState::Ready) detail += (detail.empty() ? "" : ", ") + std::string("retrying in ") +
                                                        std::to_string((retry + 999) / 1000) + " s";
    if (!detail.empty()) Kit::para(words, TextUtil::toDisplay(detail), Kit::small(), p.text2);

    lv_obj_t* actions = Kit::hrow(_status, 16);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(actions, 16, 0);
    if (st == LinkState::Ready) {
        Kit::button(actions, "Disconnect", Kit::Btn::Secondary, onDisconnect);
        Kit::button(actions, "Forget", Kit::Btn::Danger, onForget);
    } else if (busy(st)) {
        Kit::button(actions, "Cancel", Kit::Btn::Secondary, onDisconnect);
    } else if (saved) {
        Kit::button(actions, "Connect", Kit::Btn::Primary, onConnectSaved);
        Kit::button(actions, "Forget", Kit::Btn::Danger, onForget);
    } else {
        Kit::para(_status, "Scan, then tap your radio. Its PIN is shown on its screen (Meshtastic's default "
                           "is 123456). Chats and nodes come from the radio once it is connected.",
                  Kit::body(), p.text2);
        lv_obj_delete(actions);
    }
}

void RadioView::buildConnection(lv_obj_t* parent) {
    const Settings& s = Settings::get();
    lv_obj_t* card = Kit::card(parent);
    Kit::toggle(card, "Connect automatically", "Reconnect to the saved radio at start and after a dropout",
                s.autoConnect, onAutoConnect);
    _pin = Kit::input(card, "Pairing PIN", s.blePin, 6, "123456", true);
    lv_obj_add_event_cb(_pin, onPin, LV_EVENT_READY, nullptr);
}

void RadioView::buildScanActions() {
    lv_obj_clean(_scanActions);
    if (gMesh.state() == LinkState::Scanning) {
        lv_obj_t* row = Kit::hrow(_scanActions, 12);
        lv_obj_set_width(row, LV_SIZE_CONTENT);
        lv_obj_t* spin = lv_spinner_create(row);
        lv_obj_set_size(spin, 48, 48);
        lv_obj_set_style_arc_width(spin, 6, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spin, 6, LV_PART_INDICATOR);
        Kit::button(row, "Stop", Kit::Btn::Secondary, onStopScan);
    } else {
        lv_obj_t* b = Kit::button(_scanActions, LV_SYMBOL_REFRESH "  Scan", Kit::Btn::Primary, onScan);
        LinkState st = gMesh.state();
        if (st != LinkState::Idle && st != LinkState::Off) lv_obj_add_state(b, LV_STATE_DISABLED);
    }
}

void RadioView::buildNearby() {
    const Kit::Palette& p = Kit::pal();
    lv_obj_clean(_nearby);
    _results = gMesh.scanResults();
    if (_results.empty()) {
        std::string hint = gMesh.state() == LinkState::Scanning ? "Looking for radios..."
                         : gMesh.state() == LinkState::Ready ? "Connected. Scan to switch to another radio."
                                                             : "Tap Scan to look for radios nearby.";
        lv_obj_t* l = Kit::para(_nearby, hint, Kit::body(), p.text2);
        lv_obj_set_style_pad_all(l, Kit::kPad, 0);
        return;
    }
    const std::string& savedAddr = Settings::get().radioAddr;
    for (size_t i = 0; i < _results.size(); ++i) {
        const RadioInfo& r = _results[i];
        lv_obj_t* row = Kit::listRow(_nearby, false, onResult, reinterpret_cast<void*>(i));
        Kit::avatar(row, LV_SYMBOL_BLUETOOTH, std::strtoul(r.addr.substr(9).c_str(), nullptr, 16), 72);
        lv_obj_t* words = Kit::column(row, 4);
        lv_obj_set_width(words, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(words, 1);
        lv_obj_t* top = Kit::hrow(words, 12);
        lv_obj_t* t = Kit::text(top, r.name.empty() ? "Meshtastic radio" : TextUtil::toDisplay(r.name), Kit::strong(), p.text);
        lv_obj_set_flex_grow(t, 1);
        Kit::oneLine(t);
        if (r.addr == savedAddr) Kit::pill(top, "saved", p.tint, p.accent);
        int bars = r.rssi > -60 ? 4 : r.rssi > -70 ? 3 : r.rssi > -80 ? 2 : 1;
        Kit::text(words, r.addr + ", " + std::to_string(r.rssi) + " dBm  " + std::string(bars, '|'), Kit::small(),
                  p.text2);
    }
}

void RadioView::onResult(lv_event_t* e) {
    size_t i = reinterpret_cast<size_t>(lv_event_get_user_data(e));
    if (i >= sView._results.size()) return;
    if (gMesh.state() == LinkState::Scanning) gMesh.stopScan();
    gMesh.connect(sView._results[i]);
}

void RadioView::onAutoConnect(lv_event_t* e) {
    Settings& s = Settings::get();
    s.autoConnect = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    s.save();
}

void RadioView::onPin(lv_event_t*) {
    if (!sView._pin) return;
    std::string pin = lv_textarea_get_text(sView._pin);
    if (pin.empty()) return;
    while (pin.size() < 6) pin = "0" + pin;
    Settings& s = Settings::get();
    s.blePin = pin;
    s.save();
    lv_textarea_set_text(sView._pin, pin.c_str());
    toast("PIN saved");
}

void RadioView::update() {
    if (millis() - _builtAt < kRefreshMs) return;
    std::string sig = signature();
    if (sig == _sig) return;
    _sig = sig;
    _builtAt = millis();
    if (_status) buildStatus();
    if (_scanActions) buildScanActions();
    if (_nearby) buildNearby();
}

} // namespace

View& radioView() { return sView; }

} // namespace Tab5

#endif
