#include "kit.h"

#include "meshtastic_client.h"
#include "settings.h"
#include "theme.h"
#include "views.h"

namespace Kit {

namespace {

Palette sPal;

lv_color_t hex(uint32_t rgb) { return lv_color_hex(rgb); }

// `amount` of a over b, 0..255.
lv_color_t over(lv_color_t a, lv_color_t b, uint8_t amount) { return lv_color_mix(a, b, amount); }

void bare(lv_obj_t* o) {
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

void onInputClicked(lv_event_t* e) { Tab5::editText(lv_event_get_target_obj(e)); }

} // namespace

const Palette& pal() { return sPal; }

void loadTheme(lv_display_t* display) {
    const ColorTheme& t = *kThemes[Settings::get().themeIndex % kThemeCount];
    Palette& p = sPal;
    p.bg = hex(t.bg);
    p.text = hex(t.text);
    p.accent = hex(t.accent);
    p.dark = lv_color_brightness(p.bg) < 128;
    p.surface = over(p.text, p.bg, p.dark ? 16 : 10);
    p.raised = over(p.text, p.bg, p.dark ? 30 : 22);
    p.line = over(p.text, p.bg, p.dark ? 36 : 40);
    p.text2 = over(p.text, p.bg, 165);
    p.dim = over(p.text, p.bg, 105);
    p.onAccent = p.dark ? hex(0x111111) : hex(0xFFFFFF);
    p.tint = over(p.accent, p.bg, p.dark ? 44 : 36);
    p.mine = over(p.accent, p.bg, p.dark ? 62 : 48);
    p.ok = hex(t.connected);
    p.warn = hex(t.scanning);
    p.err = hex(t.errorClr);
    lv_theme_t* theme = lv_theme_default_init(display, p.accent, p.accent, p.dark, body());
    lv_display_set_theme(display, theme);
}

const lv_font_t* caption() { return &lv_font_montserrat_20; }
const lv_font_t* small() { return &lv_font_montserrat_22; }
const lv_font_t* body() { return &lv_font_montserrat_26; }
const lv_font_t* strong() { return &lv_font_montserrat_28; }
const lv_font_t* title() { return &lv_font_montserrat_36; }
const lv_font_t* display() { return &lv_font_montserrat_48; }

lv_obj_t* box(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    bare(o);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    return o;
}

lv_obj_t* column(lv_obj_t* parent, int gap) {
    lv_obj_t* o = box(parent);
    lv_obj_set_width(o, lv_pct(100));
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(o, gap, 0);
    return o;
}

lv_obj_t* hrow(lv_obj_t* parent, int gap) {
    lv_obj_t* o = box(parent);
    lv_obj_set_width(o, lv_pct(100));
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o, gap, 0);
    return o;
}

lv_obj_t* scroller(lv_obj_t* parent, int gap) {
    lv_obj_t* o = box(parent);
    lv_obj_set_size(o, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(o, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(o, gap, 0);
    return o;
}

lv_obj_t* text(lv_obj_t* parent, const std::string& s, const lv_font_t* font, lv_color_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, s.c_str());
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

lv_obj_t* oneLine(lv_obj_t* label) {
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(label, lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)));
    return label;
}

lv_obj_t* para(lv_obj_t* parent, const std::string& s, const lv_font_t* font, lv_color_t color) {
    lv_obj_t* l = text(parent, s, font, color);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    return l;
}

lv_obj_t* button(lv_obj_t* parent, const std::string& label, Btn kind, lv_event_cb_t cb, void* user) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_height(b, kTouch);
    lv_obj_set_style_min_width(b, 160, 0);
    lv_obj_set_style_pad_hor(b, 32, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_color_t bg = sPal.accent, fg = sPal.onAccent;
    lv_opa_t opa = LV_OPA_COVER;
    switch (kind) {
    case Btn::Primary: break;
    case Btn::Secondary: bg = sPal.raised; fg = sPal.text; break;
    case Btn::Danger: bg = over(sPal.err, sPal.bg, 60); fg = sPal.err; break;
    case Btn::Ghost: opa = LV_OPA_TRANSP; fg = sPal.accent; break;
    }
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, opa, 0);
    lv_obj_set_style_text_color(b, fg, 0);
    lv_obj_set_style_bg_color(b, over(sPal.text, bg, 40), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, sPal.surface, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(b, sPal.dim, LV_STATE_DISABLED);
    lv_obj_t* l = lv_label_create(b);  // inherits the button's text colour, per state
    lv_label_set_text(l, label.c_str());
    lv_obj_set_style_text_font(l, strong(), 0);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

lv_obj_t* iconButton(lv_obj_t* parent, const char* symbol, lv_event_cb_t cb, void* user) {
    lv_obj_t* b = button(parent, symbol, Btn::Ghost, cb, user);
    lv_obj_set_size(b, kTouch, kTouch);
    lv_obj_set_style_min_width(b, kTouch, 0);
    lv_obj_set_style_pad_hor(b, 0, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), title(), 0);
    return b;
}

lv_obj_t* card(lv_obj_t* parent) {
    lv_obj_t* c = column(parent, kGap);
    lv_obj_set_style_bg_color(c, sPal.surface, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 28, 0);
    lv_obj_set_style_pad_all(c, 28, 0);
    return c;
}

lv_obj_t* listRow(lv_obj_t* parent, bool selected, lv_event_cb_t cb, void* user) {
    lv_obj_t* r = hrow(parent, 20);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_min_height(r, kRowH, 0);
    lv_obj_set_style_pad_hor(r, kPad, 0);
    lv_obj_set_style_pad_ver(r, 14, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, selected ? sPal.tint : sPal.bg, 0);
    lv_obj_set_style_bg_color(r, sPal.raised, LV_STATE_PRESSED);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, sPal.line, 0);
    if (cb) lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, user);
    return r;
}

lv_color_t avatarColor(uint32_t seed) {
    static const uint32_t kColors[] = {0x3B6EA8, 0x4E8F5A, 0x9B5BA5, 0xB7743A,
                                       0x3E8C8C, 0xA84B5B, 0x6A6FB8, 0x7D8A3A};
    seed ^= seed >> 16;
    seed *= 0x45d9f3bU;
    seed ^= seed >> 16;
    return over(hex(kColors[seed % 8]), sPal.bg, sPal.dark ? 200 : 230);
}

lv_obj_t* avatar(lv_obj_t* parent, const std::string& label, uint32_t seed, int size) {
    lv_obj_t* a = box(parent);
    lv_obj_set_size(a, size, size);
    lv_obj_set_style_radius(a, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(a, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(a, avatarColor(seed), 0);
    lv_obj_t* l = text(a, label, size >= 140 ? display() : (size >= 80 ? strong() : small()), hex(0xFFFFFF));
    lv_obj_center(l);
    return a;
}

lv_obj_t* dot(lv_obj_t* parent, lv_color_t color, int size) {
    lv_obj_t* d = box(parent);
    lv_obj_set_size(d, size, size);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, color, 0);
    return d;
}

lv_obj_t* pill(lv_obj_t* parent, const std::string& label, lv_color_t bg, lv_color_t fg) {
    lv_obj_t* p = box(parent);
    lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p, bg, 0);
    lv_obj_set_style_pad_hor(p, 14, 0);
    lv_obj_set_style_pad_ver(p, 4, 0);
    text(p, label, caption(), fg);
    return p;
}

lv_obj_t* sectionLabel(lv_obj_t* parent, const std::string& label) {
    lv_obj_t* l = text(parent, label, small(), sPal.text2);
    lv_obj_set_style_pad_top(l, 12, 0);
    return l;
}

void emptyState(lv_obj_t* parent, const char* symbol, const std::string& title, const std::string& body) {
    lv_obj_t* c = column(parent, 20);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(c, 48, 0);
    lv_obj_set_style_pad_top(c, 96, 0);
    text(c, symbol, display(), sPal.dim);
    lv_obj_t* t = text(c, title, strong(), sPal.text);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    if (!body.empty()) {
        lv_obj_t* b = para(c, body, Kit::body(), sPal.text2);
        lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
    }
}

lv_obj_t* field(lv_obj_t* parent, const std::string& label, const std::string& value) {
    lv_obj_t* r = hrow(parent, kGap);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_min_height(r, 56, 0);
    text(r, label, body(), sPal.text2);
    lv_obj_t* v = text(r, value, body(), sPal.text);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_flex_grow(v, 1);
    oneLine(v);
    return v;
}

lv_obj_t* input(lv_obj_t* parent, const std::string& label, const std::string& value, uint32_t maxLen,
                const char* placeholder, bool digits, bool password) {
    lv_obj_t* c = column(parent, 8);
    if (!label.empty()) text(c, label, small(), sPal.text2);
    lv_obj_t* ta = lv_textarea_create(c);
    lv_obj_set_width(ta, lv_pct(100));
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, maxLen);
    if (digits) lv_textarea_set_accepted_chars(ta, "0123456789");
    lv_textarea_set_password_mode(ta, password);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_text(ta, value.c_str());
    lv_obj_set_style_text_font(ta, strong(), 0);
    lv_obj_set_style_bg_color(ta, sPal.raised, 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_style_border_width(ta, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(ta, sPal.accent, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(ta, 20, 0);
    lv_obj_set_style_pad_all(ta, 22, 0);
    lv_obj_add_event_cb(ta, onInputClicked, LV_EVENT_CLICKED, nullptr);
    return ta;
}

lv_obj_t* toggle(lv_obj_t* parent, const std::string& label, const std::string& hint, bool on,
                 lv_event_cb_t cb, void* user) {
    lv_obj_t* r = hrow(parent, kGap);
    lv_obj_set_style_min_height(r, kTouch, 0);
    lv_obj_t* words = column(r, 4);
    lv_obj_set_width(words, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(words, 1);
    para(words, label, body(), sPal.text);
    if (!hint.empty()) para(words, hint, small(), sPal.text2);
    lv_obj_t* sw = lv_switch_create(r);
    lv_obj_set_size(sw, 104, 56);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    if (cb) lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, user);
    return sw;
}

lv_obj_t* segmented(lv_obj_t* parent, const char* const* map, int selected, lv_event_cb_t cb, void* user) {
    lv_obj_t* m = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(m, map);
    lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(m, true);
    if (selected >= 0) lv_buttonmatrix_set_button_ctrl(m, selected, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_size(m, lv_pct(100), kTouch);
    lv_obj_set_style_pad_all(m, 6, 0);
    lv_obj_set_style_pad_gap(m, 6, 0);
    lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(m, sPal.raised, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_font(m, small(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, sPal.text, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(m, sPal.accent, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(m, sPal.onAccent, LV_PART_ITEMS | LV_STATE_CHECKED);
    if (cb) lv_obj_add_event_cb(m, cb, LV_EVENT_VALUE_CHANGED, user);
    return m;
}

lv_obj_t* slider(lv_obj_t* parent, int min, int max, int value, lv_event_cb_t cb, void* user) {
    lv_obj_t* s = lv_slider_create(parent);
    lv_obj_set_width(s, lv_pct(100));
    lv_obj_set_height(s, 20);
    lv_obj_set_style_margin_ver(s, 24, 0);
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_set_style_pad_all(s, 14, LV_PART_KNOB);
    lv_obj_set_ext_click_area(s, 30);
    if (cb) lv_obj_add_event_cb(s, cb, LV_EVENT_ALL, user);
    return s;
}

lv_color_t linkColor() {
    switch (gMesh.state()) {
    case LinkState::Ready: return sPal.ok;
    case LinkState::Off:
    case LinkState::Idle: return gMesh.hasSavedRadio() ? sPal.err : sPal.dim;
    default: return sPal.warn;
    }
}

const char* linkText() {
    switch (gMesh.state()) {
    case LinkState::Ready: return "Connected";
    case LinkState::Scanning: return "Scanning";
    case LinkState::Connecting: return "Connecting";
    case LinkState::Pairing: return "Waiting for PIN";
    case LinkState::Syncing: return "Syncing";
    case LinkState::Off:
    case LinkState::Idle: break;
    }
    return gMesh.hasSavedRadio() ? "Not connected" : "Not paired";
}

} // namespace Kit
