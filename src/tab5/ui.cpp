// Tab5 UI shell: the layout for the current orientation, navigation, the
// docked keyboard, the pairing prompt, toasts, and the AppUi entry points.
#if defined(MESHPUTER_BOARD_TAB5)

#include "app_ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdlib>
#include <cstring>

#include "board.h"
#include "chat_service.h"
#include "kit.h"
#include "meshtastic_client.h"
#include "nostr_service.h"
#include "port.h"
#include "power.h"
#include "settings.h"
#include "views.h"

// LVGL layout and drawing recurse deeper than the 8 KB default loop stack.
SET_LOOP_TASK_STACK_SIZE(24 * 1024);

namespace Tab5 {

namespace {

constexpr int kRailW = 136;
constexpr int kListW = 470;
constexpr int kHeaderH = 112;
constexpr int kNavH = 124;
constexpr int kKeyboardPortraitH = 470;
constexpr int kKeyboardLandscapeH = 330;
constexpr uint32_t kToastMs = 3500;
constexpr uint32_t kBatteryRefreshMs = 30000;
constexpr size_t kPinDigits = 6;

struct Shell {
    lv_obj_t* root = nullptr;
    lv_obj_t* nav = nullptr;
    lv_obj_t* navItems[kTabCount] = {};
    lv_obj_t* badge = nullptr;
    lv_obj_t* pageHeader = nullptr;  // portrait
    lv_obj_t* back = nullptr;        // portrait
    Pane master;                     // portrait: the page
    Pane detail;                     // landscape only
    lv_obj_t* link = nullptr;
    lv_obj_t* net = nullptr;
    lv_obj_t* battery = nullptr;
    lv_obj_t* keyboard = nullptr;
    lv_obj_t* toast = nullptr;
    lv_obj_t* pin = nullptr;
    lv_obj_t* pinDigits = nullptr;
};

Shell S;
Route sRoute;
uint8_t sPending = kAll;
View* sShown = nullptr;  // the view whose widgets are on screen
bool sCloseKeyboard = false;
std::string sPin;
uint32_t sToastUntil = 0;
uint32_t sSeenToastAt = 0;
int sActiveConv = -1;
uint32_t sActiveRevision = 0;

LinkState sShownLink = LinkState::Off;
bool sShownSaved = false;
int sShownNet = -2;
int sShownUnread = -1;
uint32_t sBatteryAt = 0;

View& viewFor(Tab t) {
    switch (t) {
    case Tab::Chats: return chatsView();
    case Tab::Nodes: return nodesView();
    case Tab::Radio: return radioView();
    case Tab::Settings: break;
    }
    return settingsView();
}

bool isInside(lv_obj_t* obj, lv_obj_t* container) {
    for (; obj; obj = lv_obj_get_parent(obj)) {
        if (obj == container) return true;
    }
    return false;
}

lv_obj_t* editing() { return S.keyboard ? lv_keyboard_get_textarea(S.keyboard) : nullptr; }

bool keyboardShown() { return S.keyboard && !lv_obj_has_flag(S.keyboard, LV_OBJ_FLAG_HIDDEN); }

// Portrait page without the bottom bar.
bool fullScreenPage() { return !landscape() && sRoute.detail && viewFor(sRoute.tab).fullScreenDetail(); }

void showNav(bool show) {
    if (!S.nav || landscape()) return;
    if (show) lv_obj_remove_flag(S.nav, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(S.nav, LV_OBJ_FLAG_HIDDEN);
}

void closeKeyboardNow() {
    if (!S.keyboard) return;
    if (lv_obj_t* ta = editing()) lv_obj_remove_state(ta, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(S.keyboard, nullptr);
    lv_obj_add_flag(S.keyboard, LV_OBJ_FLAG_HIDDEN);
    showNav(!fullScreenPage());
}

void highlightNav() {
    const Kit::Palette& p = Kit::pal();
    for (int i = 0; i < kTabCount; ++i) {
        lv_obj_t* item = S.navItems[i];
        if (!item) continue;
        bool on = static_cast<int>(sRoute.tab) == i;
        lv_obj_set_style_bg_opa(item, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(item, p.tint, 0);
        for (uint32_t c = 0; c < lv_obj_get_child_count(item); ++c) {
            lv_obj_t* child = lv_obj_get_child(item, c);
            if (child != S.badge) lv_obj_set_style_text_color(child, on ? p.accent : p.text2, 0);
        }
    }
}

// ---- events -------------------------------------------------------------------

void onNav(lv_event_t* e) {
    showTab(static_cast<Tab>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));
}

void onBack(lv_event_t*) { back(); }

void onKeyboard(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta = editing();
    if (code == LV_EVENT_CANCEL) sCloseKeyboard = true;
    // The keyboard passes READY on to the field after this handler returns,
    // so the field must stay attached until then.
    if (code == LV_EVENT_READY && !(ta && lv_obj_has_flag(ta, LV_OBJ_FLAG_USER_1))) sCloseKeyboard = true;
}

void submitPin() {
    if (sPin.empty()) return;
    gMesh.submitPin(static_cast<uint32_t>(strtoul(sPin.c_str(), nullptr, 10)));
}

void showPinDigits() {
    std::string shown;
    for (size_t i = 0; i < kPinDigits; ++i) {
        if (i) shown += ' ';
        shown += i < sPin.size() ? sPin[i] : '_';
    }
    lv_label_set_text(S.pinDigits, shown.c_str());
}

void onPinKey(lv_event_t* e) {
    lv_obj_t* m = lv_event_get_target_obj(e);
    const char* key = lv_buttonmatrix_get_button_text(m, lv_buttonmatrix_get_selected_button(m));
    if (!key) return;
    if (strcmp(key, LV_SYMBOL_BACKSPACE) == 0) {
        if (!sPin.empty()) sPin.pop_back();
    } else if (strcmp(key, LV_SYMBOL_OK) == 0) {
        submitPin();
        return;
    } else if (sPin.size() < kPinDigits) {
        sPin += key;
    }
    showPinDigits();
    if (sPin.size() == kPinDigits) submitPin();
}

void onPinCancel(lv_event_t*) { gMesh.cancelPin(); }

// ---- building -----------------------------------------------------------------

void buildStatus(lv_obj_t* parent, bool vertical) {
    lv_obj_t* box = Kit::box(parent);
    lv_obj_set_flex_flow(box, vertical ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(box, vertical ? 12 : 18, 0);
    const Kit::Palette& p = Kit::pal();
    S.net = Kit::text(box, LV_SYMBOL_WIFI, Kit::strong(), p.dim);
    S.link = Kit::text(box, LV_SYMBOL_BLUETOOTH, Kit::strong(), p.dim);
    S.battery = Kit::text(box, "", Kit::small(), p.text2);
}

lv_obj_t* buildPane(lv_obj_t* parent, Pane& pane, bool page) {
    const Kit::Palette& p = Kit::pal();
    lv_obj_t* box = Kit::box(parent);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(box, lv_pct(100));

    lv_obj_t* header = Kit::hrow(box, 8);
    lv_obj_set_height(header, kHeaderH);
    lv_obj_set_style_pad_hor(header, Kit::kPad, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, p.line, 0);
    if (page) {
        S.pageHeader = header;
        S.back = Kit::iconButton(header, LV_SYMBOL_LEFT, onBack);
        lv_obj_add_flag(S.back, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t* titles = Kit::column(header, 0);
    lv_obj_set_width(titles, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(titles, 1);
    pane.title = Kit::text(titles, "", Kit::title(), p.text);
    lv_obj_set_width(pane.title, lv_pct(100));
    Kit::oneLine(pane.title);
    pane.subtitle = Kit::text(titles, "", Kit::small(), p.text2);
    lv_obj_set_width(pane.subtitle, lv_pct(100));
    Kit::oneLine(pane.subtitle);
    lv_obj_add_flag(pane.subtitle, LV_OBJ_FLAG_HIDDEN);
    pane.actions = Kit::box(header);
    lv_obj_set_flex_flow(pane.actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pane.actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pane.actions, 4, 0);
    if (page) buildStatus(header, false);

    pane.body = Kit::box(box);
    lv_obj_set_width(pane.body, lv_pct(100));
    lv_obj_set_flex_grow(pane.body, 1);
    return box;
}

void buildNav(lv_obj_t* parent, bool rail) {
    const Kit::Palette& p = Kit::pal();
    S.nav = Kit::box(parent);
    lv_obj_set_style_bg_opa(S.nav, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(S.nav, p.surface, 0);
    if (rail) {
        lv_obj_set_size(S.nav, kRailW, lv_pct(100));
        lv_obj_set_flex_flow(S.nav, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(S.nav, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_ver(S.nav, 20, 0);
        lv_obj_set_style_pad_row(S.nav, 8, 0);
    } else {
        lv_obj_set_size(S.nav, lv_pct(100), kNavH);
        lv_obj_set_flex_flow(S.nav, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_all(S.nav, 8, 0);
        lv_obj_set_style_pad_column(S.nav, 8, 0);
        lv_obj_set_style_border_side(S.nav, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(S.nav, 1, 0);
        lv_obj_set_style_border_color(S.nav, p.line, 0);
    }
    for (int i = 0; i < kTabCount; ++i) {
        View& v = viewFor(static_cast<Tab>(i));
        lv_obj_t* item = Kit::box(S.nav);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        if (rail) {
            lv_obj_set_size(item, kRailW - 20, 116);
        } else {
            lv_obj_set_height(item, lv_pct(100));
            lv_obj_set_flex_grow(item, 1);
        }
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(item, 6, 0);
        lv_obj_set_style_radius(item, 28, 0);
        lv_obj_set_style_bg_color(item, p.raised, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
        Kit::text(item, v.icon(), Kit::title(), p.text2);
        Kit::text(item, v.label(), Kit::caption(), p.text2);
        lv_obj_add_event_cb(item, onNav, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        S.navItems[i] = item;
    }
    S.badge = Kit::pill(S.navItems[0], "", p.accent, p.onAccent);
    lv_obj_add_flag(S.badge, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(S.badge, LV_ALIGN_TOP_RIGHT, rail ? -6 : -40, 4);
    lv_obj_add_flag(S.badge, LV_OBJ_FLAG_HIDDEN);
    if (rail) {
        lv_obj_t* spacer = Kit::box(S.nav);
        lv_obj_set_flex_grow(spacer, 1);
        buildStatus(S.nav, true);
    }
}

void buildKeyboard() {
    const Kit::Palette& p = Kit::pal();
    S.keyboard = lv_keyboard_create(S.root);
    lv_obj_set_size(S.keyboard, lv_pct(100), landscape() ? kKeyboardLandscapeH : kKeyboardPortraitH);
    lv_obj_set_style_bg_color(S.keyboard, p.surface, 0);
    lv_obj_set_style_border_width(S.keyboard, 0, 0);
    lv_obj_set_style_radius(S.keyboard, 0, 0);
    lv_obj_set_style_pad_all(S.keyboard, 10, 0);
    lv_obj_set_style_pad_gap(S.keyboard, 10, 0);
    lv_obj_set_style_text_font(S.keyboard, Kit::strong(), LV_PART_ITEMS);
    lv_obj_set_style_radius(S.keyboard, 16, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(S.keyboard, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(S.keyboard, onKeyboard, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(S.keyboard, onKeyboard, LV_EVENT_CANCEL, nullptr);
    lv_keyboard_set_textarea(S.keyboard, nullptr);
    lv_obj_add_flag(S.keyboard, LV_OBJ_FLAG_HIDDEN);
}

void buildShell() {
    if (sShown) sShown->cleared(kAll);
    sShown = nullptr;
    lv_obj_clean(lv_layer_top());
    lv_obj_t* screen = lv_screen_active();
    lv_obj_clean(screen);
    S = Shell();

    Kit::loadTheme(lv_display_get_default());
    const Kit::Palette& p = Kit::pal();
    lv_obj_set_style_bg_color(screen, p.bg, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    S.root = Kit::box(screen);
    lv_obj_set_size(S.root, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(S.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_t* body = Kit::box(S.root);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);

    if (landscape()) {
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
        buildNav(body, true);
        lv_obj_t* list = buildPane(body, S.master, false);
        lv_obj_set_width(list, kListW);
        lv_obj_set_style_border_side(list, LV_BORDER_SIDE_RIGHT, 0);
        lv_obj_set_style_border_width(list, 1, 0);
        lv_obj_set_style_border_color(list, p.line, 0);
        lv_obj_t* detail = buildPane(body, S.detail, false);
        lv_obj_set_flex_grow(detail, 1);
    } else {
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_t* page = buildPane(body, S.master, true);
        lv_obj_set_width(page, lv_pct(100));
        lv_obj_set_flex_grow(page, 1);
        buildNav(body, false);
    }
    buildKeyboard();
    highlightNav();

    sShownNet = -2;
    sShownUnread = -1;
    sBatteryAt = 0;
    sShownLink = gMesh.state();
    sShownSaved = !gMesh.hasSavedRadio();  // forces the first status update
}

void clearPane(Pane& pane, View& v, uint8_t parts) {
    lv_obj_t* ta = editing();
    if (ta && (isInside(ta, pane.body) || isInside(ta, pane.actions))) closeKeyboardNow();
    v.cleared(parts);
    pane.setTitle("");
    pane.setSubtitle("");
    lv_obj_clean(pane.actions);
    lv_obj_clean(pane.body);
}

void fillParts(uint8_t parts) {
    View& v = viewFor(sRoute.tab);
    if (sShown != &v) {
        if (sShown) sShown->cleared(kAll);
        sShown = &v;
        parts |= kList | kDetail;
    }
    if (landscape()) {
        if (parts & kList) {
            clearPane(S.master, v, kList);
            v.buildList(S.master);
        }
        if (parts & kDetail) {
            clearPane(S.detail, v, kDetail);
            if (v.hasDetail()) v.buildDetail(S.detail);
            else v.buildPlaceholder(S.detail);
        }
        return;
    }
    bool detail = sRoute.detail && v.hasDetail();
    if (!(parts & (detail ? kDetail : kList))) return;  // the other page is not on screen
    sRoute.detail = detail;
    clearPane(S.master, v, kList | kDetail);
    if (detail) {
        lv_obj_remove_flag(S.back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_pad_left(S.pageHeader, 4, 0);
        showNav(!v.fullScreenDetail() && !keyboardShown());
        v.buildDetail(S.master);
    } else {
        lv_obj_add_flag(S.back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_pad_left(S.pageHeader, Kit::kPad, 0);
        showNav(!keyboardShown());
        v.buildList(S.master);
    }
}

// ---- live state ---------------------------------------------------------------

void updateStatus() {
    if (!S.link) return;
    const Kit::Palette& p = Kit::pal();
    LinkState link = gMesh.state();
    bool saved = gMesh.hasSavedRadio();
    if (link != sShownLink || saved != sShownSaved) {
        sShownLink = link;
        sShownSaved = saved;
        lv_obj_set_style_text_color(S.link, Kit::linkColor(), 0);
    }
    int net = Settings::get().internetEnabled ? static_cast<int>(NostrService::state()) : -1;
    if (net != sShownNet) {
        sShownNet = net;
        if (net < 0) lv_obj_add_flag(S.net, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(S.net, LV_OBJ_FLAG_HIDDEN);
        lv_color_t c = net == static_cast<int>(NostrService::State::Online) ? p.ok
                     : net == static_cast<int>(NostrService::State::Connecting) ? p.warn : p.dim;
        lv_obj_set_style_text_color(S.net, c, 0);
    }
    uint32_t now = millis();
    if (sBatteryAt == 0 || now - sBatteryAt > kBatteryRefreshMs) {
        sBatteryAt = now;
        int b = Board::batteryPercent();
        if (b < 0) {
            lv_obj_add_flag(S.battery, LV_OBJ_FLAG_HIDDEN);
        } else {
            const char* icon = b > 80 ? LV_SYMBOL_BATTERY_FULL : b > 55 ? LV_SYMBOL_BATTERY_3
                             : b > 30 ? LV_SYMBOL_BATTERY_2 : b > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
            lv_label_set_text_fmt(S.battery, "%s %d%%", icon, b);
            lv_obj_remove_flag(S.battery, LV_OBJ_FLAG_HIDDEN);
        }
    }
    int unread = Chat::store().unreadCount();
    if (unread != sShownUnread) {
        sShownUnread = unread;
        if (unread == 0) {
            lv_obj_add_flag(S.badge, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text_fmt(lv_obj_get_child(S.badge, 0), "%d", unread);
            lv_obj_remove_flag(S.badge, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

int visibleConversation() {
    if (sRoute.tab != Tab::Chats || !sRoute.hasConv || sRoute.newChat) return -1;
    if (!landscape() && !sRoute.detail) return -1;
    return Chat::store().find(sRoute.conv);
}

// Messages in the conversation on screen are read as they arrive.
void syncActiveConversation() {
    int idx = visibleConversation();
    uint32_t rev = Chat::store().revision();
    if (idx == sActiveConv && rev == sActiveRevision) return;
    sActiveConv = idx;
    Chat::setActive(idx);
    sActiveRevision = Chat::store().revision();  // marking read bumps it
}

void syncPinDialog() {
    bool want = gMesh.state() == LinkState::Pairing;
    if (want == (S.pin != nullptr)) return;
    if (!want) {
        lv_obj_delete(S.pin);
        S.pin = S.pinDigits = nullptr;
        return;
    }
    Power::onKey();
    closeKeyboardNow();
    sPin.clear();
    const Kit::Palette& p = Kit::pal();
    S.pin = Kit::box(lv_layer_top());
    lv_obj_set_size(S.pin, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(S.pin, LV_OBJ_FLAG_CLICKABLE);  // nothing behind it reacts
    lv_obj_set_style_bg_color(S.pin, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(S.pin, LV_OPA_60, 0);

    lv_obj_t* card = Kit::card(S.pin);
    lv_obj_set_width(card, LV_MIN(640, lv_display_get_horizontal_resolution(nullptr) - 48));
    lv_obj_center(card);
    lv_obj_set_style_pad_row(card, 20, 0);
    Kit::text(card, "Pair with your radio", Kit::title(), p.text);
    std::string name = gMesh.radio().name.empty() ? "the radio" : gMesh.radio().name;
    Kit::para(card, "Type the PIN shown on " + name + "'s screen. Meshtastic's default is 123456.",
              Kit::body(), p.text2);
    S.pinDigits = Kit::text(card, "", Kit::display(), p.accent);
    lv_obj_set_width(S.pinDigits, lv_pct(100));
    lv_obj_set_style_text_align(S.pinDigits, LV_TEXT_ALIGN_CENTER, 0);
    showPinDigits();

    static const char* const kKeypad[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                                          LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, ""};
    lv_obj_t* pad = lv_buttonmatrix_create(card);
    lv_buttonmatrix_set_map(pad, kKeypad);
    lv_obj_set_size(pad, lv_pct(100), landscape() ? 4 * 72 : 4 * 100);
    lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad, 0, 0);
    lv_obj_set_style_pad_all(pad, 0, 0);
    lv_obj_set_style_pad_gap(pad, 12, 0);
    lv_obj_set_style_text_font(pad, Kit::title(), LV_PART_ITEMS);
    lv_obj_set_style_radius(pad, 24, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(pad, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(pad, p.raised, LV_PART_ITEMS);
    lv_obj_set_style_text_color(pad, p.text, LV_PART_ITEMS);
    lv_obj_add_event_cb(pad, onPinKey, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* actions = Kit::hrow(card, 16);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    Kit::button(actions, "Cancel", Kit::Btn::Secondary, onPinCancel);
}

void syncToasts() {
    if (S.toast && static_cast<int32_t>(millis() - sToastUntil) >= 0) {
        lv_obj_delete(S.toast);
        S.toast = nullptr;
    }
    if (Chat::toastAtMs() == sSeenToastAt) return;
    sSeenToastAt = Chat::toastAtMs();
    // A message for the conversation on screen needs no toast.
    if (!Chat::toast().empty() && visibleConversation() < 0) toast(Chat::toast());
}

bool parseNode(const std::string& s, uint32_t& out) {
    std::string hex = (!s.empty() && s[0] == '!') ? s.substr(1) : s;
    if (hex.empty() || hex.size() > 8) return false;
    char* end = nullptr;
    out = static_cast<uint32_t>(strtoul(hex.c_str(), &end, 16));
    return end && *end == '\0';
}

} // namespace

// ---- shared API ---------------------------------------------------------------

Route& route() { return sRoute; }
bool landscape() { return Port::landscape(); }
void invalidate(uint8_t parts) { sPending |= parts; }

void Pane::setTitle(const std::string& text) {
    if (title) lv_label_set_text(title, text.c_str());
}

void Pane::setSubtitle(const std::string& text) {
    if (!subtitle) return;
    lv_label_set_text(subtitle, text.c_str());
    if (text.empty()) lv_obj_add_flag(subtitle, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(subtitle, LV_OBJ_FLAG_HIDDEN);
}

void View::buildPlaceholder(Pane& pane) {
    Kit::emptyState(pane.body, icon(), "Nothing selected", "");
}

void showTab(Tab tab) {
    sCloseKeyboard = true;
    if (tab == sRoute.tab) {
        // The active tab again: back to its list.
        if (sRoute.detail) {
            sRoute.detail = false;
            invalidate(kList | kDetail);
        }
        return;
    }
    sRoute.tab = tab;
    sRoute.detail = false;
    highlightNav();
    invalidate(kList | kDetail);
}

void openConversation(int convIdx) {
    const Conversation* c = Chat::store().at(convIdx);
    if (!c) return;
    sRoute.tab = Tab::Chats;
    sRoute.hasConv = true;
    sRoute.conv = c->key;
    sRoute.newChat = false;
    sRoute.detail = true;
    highlightNav();
    invalidate(kList | kDetail);
}

void openNewChat() {
    sRoute.tab = Tab::Chats;
    sRoute.newChat = true;
    sRoute.detail = true;
    highlightNav();
    invalidate(kList | kDetail);
}

void openNode(uint32_t num) {
    sRoute.tab = Tab::Nodes;
    sRoute.node = num;
    sRoute.detail = true;
    highlightNav();
    invalidate(kList | kDetail);
}

void openSection(int section) {
    sRoute.tab = Tab::Settings;
    sRoute.section = section;
    sRoute.detail = true;
    highlightNav();
    invalidate(kList | kDetail);
}

void back() {
    sCloseKeyboard = true;
    if (sRoute.newChat) {
        sRoute.newChat = false;
        if (landscape()) {
            invalidate(kDetail);
            return;
        }
    }
    if (!landscape() && sRoute.detail) {
        sRoute.detail = false;
        invalidate(kList | kDetail);
    }
}

void editText(lv_obj_t* textarea) {
    if (!S.keyboard || !textarea) return;
    sCloseKeyboard = false;
    if (lv_obj_t* old = editing()) lv_obj_remove_state(old, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(S.keyboard, textarea);
    const char* accepted = lv_textarea_get_accepted_chars(textarea);
    bool digits = accepted && strcmp(accepted, "0123456789") == 0;
    lv_keyboard_set_mode(S.keyboard, digits ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_remove_flag(S.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    showNav(false);
    lv_obj_update_layout(S.root);
    lv_obj_scroll_to_view_recursive(textarea, LV_ANIM_ON);
}

void closeKeyboard() { sCloseKeyboard = true; }

bool touching() {
    lv_indev_t* touch = lv_indev_get_next(nullptr);
    return touch && lv_indev_get_state(touch) == LV_INDEV_STATE_PRESSED;
}

void toast(const std::string& text) {
    if (S.toast) lv_obj_delete(S.toast);
    const Kit::Palette& p = Kit::pal();
    S.toast = Kit::box(lv_layer_top());
    lv_obj_set_width(S.toast, LV_MIN(640, lv_display_get_horizontal_resolution(nullptr) - 48));
    lv_obj_set_style_bg_opa(S.toast, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(S.toast, p.raised, 0);
    lv_obj_set_style_radius(S.toast, 28, 0);
    lv_obj_set_style_pad_hor(S.toast, 32, 0);
    lv_obj_set_style_pad_ver(S.toast, 22, 0);
    lv_obj_set_style_shadow_width(S.toast, 40, 0);
    lv_obj_set_style_shadow_opa(S.toast, LV_OPA_40, 0);
    lv_obj_t* l = Kit::para(S.toast, text, Kit::body(), p.text);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(S.toast, LV_ALIGN_TOP_MID, 0, landscape() ? 24 : kHeaderH + 12);
    sToastUntil = millis() + kToastMs;
}

std::string ago(uint32_t epochSeconds) {
    uint32_t now = gMesh.epochNow();
    if (epochSeconds == 0 || now == 0) return "";
    uint32_t age = now > epochSeconds ? now - epochSeconds : 0;
    if (age < 90) return "just now";
    if (age < 3600) return std::to_string(age / 60) + " min";
    if (age < 86400) return std::to_string(age / 3600) + " h";
    return std::to_string(age / 86400) + " d";
}

} // namespace Tab5

// ---- AppUi --------------------------------------------------------------------

using namespace Tab5;

namespace AppUi {

void begin() {
    Port::begin();
    sSeenToastAt = Chat::toastAtMs();
    sPending = kAll;
}

void loop() {
    if (Port::updateRotation()) sPending = kAll;
    if (sCloseKeyboard) {
        sCloseKeyboard = false;
        closeKeyboardNow();
    }
    if (sShown && !(sPending & kShell) && !touching()) sShown->update();
    if (sPending & kShell) buildShell();
    uint8_t parts = sPending;
    sPending = 0;
    if (parts) fillParts(parts);
    syncPinDialog();
    updateStatus();
    syncToasts();
    syncActiveConversation();
    Port::tick();
}

const char* screenName() {
    static std::string name;
    View& v = viewFor(sRoute.tab);
    bool detail = landscape() ? v.hasDetail() : sRoute.detail;
    name = v.name();
    if (detail) name += std::string("/") + v.detailName();
    if (S.pin) name += "+pin";
    if (keyboardShown()) name += "+keyboard";
    return name.c_str();
}

void home() {
    sRoute = Route();
    sCloseKeyboard = true;
    sPending = kAll;
}

std::string open(const std::string& what, const std::string& rest) {
    if (what == "home" || what == "chats" || what == "channels") {
        showTab(Tab::Chats);
        sRoute.detail = false;
    } else if (what == "nodes") {
        showTab(Tab::Nodes);
        sRoute.detail = false;
    } else if (what == "radio") {
        showTab(Tab::Radio);
    } else if (what == "settings") {
        if (rest.empty()) {
            showTab(Tab::Settings);
            sRoute.detail = false;
        } else {
            openSection(atoi(rest.c_str()));
        }
    } else if (what == "chat") {
        int idx = atoi(rest.c_str());
        if (!Chat::store().at(idx)) return "no such conversation";
        openConversation(idx);
    } else if (what == "node") {
        uint32_t num;
        if (!parseNode(rest, num)) return "bad node id";
        openNode(num);
    } else {
        return "unknown screen";
    }
    invalidate(kList | kDetail);
    return "";
}

std::string key(const std::string& spec) {
    if (spec == "esc" || spec == "back") {
        back();
        return "";
    }
    lv_obj_t* ta = editing();
    if (!ta) return "no text field is being edited (tap one first)";
    if (spec == "enter") {
        // What the keyboard's OK key does.
        lv_obj_send_event(S.keyboard, LV_EVENT_READY, nullptr);
        lv_obj_send_event(ta, LV_EVENT_READY, nullptr);
    } else if (spec == "del") {
        lv_textarea_delete_char(ta);
    } else {
        lv_textarea_add_text(ta, spec.c_str());
    }
    return "";
}

std::string tap(int x, int y) {
    Port::injectTap(x, y);
    return "";
}

std::string swipe(int x1, int y1, int x2, int y2) {
    Port::injectSwipe(x1, y1, x2, y2);
    return "";
}

std::string rotate(int rotation) {
    Port::forceRotation(rotation);
    return "";
}

void dumpFrame(int step) {
    lv_refr_now(nullptr);
    Port::dumpFrame(step);
}

} // namespace AppUi

#endif
