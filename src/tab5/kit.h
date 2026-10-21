#pragma once

// Look and building blocks of the Tab5 UI: colours from the app theme
// (theme.h), a type scale for a 5" 720p panel, and the few widgets every
// page is made of. Touch targets are at least 88 px (~7.5 mm).

#include <lvgl.h>

#include <cstdint>
#include <string>

namespace Kit {

struct Palette {
    lv_color_t bg;        // page
    lv_color_t surface;   // cards, bars
    lv_color_t raised;    // pressed rows, inputs
    lv_color_t line;      // separators
    lv_color_t text;
    lv_color_t text2;     // secondary text
    lv_color_t dim;       // hints, disabled
    lv_color_t accent;
    lv_color_t onAccent;  // text on accent
    lv_color_t tint;      // selected row: accent over the page
    lv_color_t mine;      // our chat bubbles
    lv_color_t ok, warn, err;
    bool dark;
};

const Palette& pal();
// Reads the theme setting; call before (re)building the UI.
void loadTheme(lv_display_t* display);

const lv_font_t* caption();   // 20
const lv_font_t* small();     // 22
const lv_font_t* body();      // 26
const lv_font_t* strong();    // 28
const lv_font_t* title();     // 36
const lv_font_t* display();   // 48

constexpr int kGap = 16;
constexpr int kPad = 24;
constexpr int kTouch = 88;
constexpr int kRowH = 112;

enum class Btn : uint8_t { Primary, Secondary, Danger, Ghost };

// Bare layout containers: no background, not clickable, not scrollable.
lv_obj_t* box(lv_obj_t* parent);
lv_obj_t* column(lv_obj_t* parent, int gap = kGap);  // full width, content height
lv_obj_t* hrow(lv_obj_t* parent, int gap = kGap);    // full width, content height, centred
// A vertically scrolling column that fills its parent.
lv_obj_t* scroller(lv_obj_t* parent, int gap = 0);

lv_obj_t* text(lv_obj_t* parent, const std::string& s, const lv_font_t* font, lv_color_t color);
// Clips a label to one line ending in "...".
lv_obj_t* oneLine(lv_obj_t* label);
// Full-width wrapping paragraph.
lv_obj_t* para(lv_obj_t* parent, const std::string& s, const lv_font_t* font, lv_color_t color);

lv_obj_t* button(lv_obj_t* parent, const std::string& label, Btn kind, lv_event_cb_t cb, void* user = nullptr);
lv_obj_t* iconButton(lv_obj_t* parent, const char* symbol, lv_event_cb_t cb, void* user = nullptr);

lv_obj_t* card(lv_obj_t* parent);
// Tappable full-width list row (flex row, items centred).
lv_obj_t* listRow(lv_obj_t* parent, bool selected, lv_event_cb_t cb, void* user);
lv_obj_t* avatar(lv_obj_t* parent, const std::string& label, uint32_t seed, int size);
lv_obj_t* dot(lv_obj_t* parent, lv_color_t color, int size);
lv_obj_t* pill(lv_obj_t* parent, const std::string& label, lv_color_t bg, lv_color_t fg);
lv_obj_t* sectionLabel(lv_obj_t* parent, const std::string& label);
void emptyState(lv_obj_t* parent, const char* symbol, const std::string& title, const std::string& body);

// Read-only "label ........ value" line.
lv_obj_t* field(lv_obj_t* parent, const std::string& label, const std::string& value);
// Labelled single-line text input; tapping it opens the keyboard.
lv_obj_t* input(lv_obj_t* parent, const std::string& label, const std::string& value, uint32_t maxLen,
                const char* placeholder = "", bool digits = false, bool password = false);
// Row with a label (and optional hint) and a switch; returns the switch.
lv_obj_t* toggle(lv_obj_t* parent, const std::string& label, const std::string& hint, bool on,
                 lv_event_cb_t cb, void* user = nullptr);
// One-of-n choice. `map` must outlive the widget (static), "" terminated.
lv_obj_t* segmented(lv_obj_t* parent, const char* const* map, int selected, lv_event_cb_t cb, void* user = nullptr);
lv_obj_t* slider(lv_obj_t* parent, int min, int max, int value, lv_event_cb_t cb, void* user = nullptr);

lv_color_t avatarColor(uint32_t seed);
lv_color_t linkColor();  // colour of the radio link state
const char* linkText();  // "Connected", "Connecting", "Not connected", ...

} // namespace Kit
