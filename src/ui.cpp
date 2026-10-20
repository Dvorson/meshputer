#include "ui.h"

#include <Arduino.h>

#include "chat_service.h"
#include "core/text_util.h"
#include "meshtastic_client.h"
#include "nostr_service.h"
#include "settings.h"

const ColorTheme& activeTheme() {
    int idx = Settings::get().themeIndex;
    if (idx < 0 || idx >= kThemeCount) idx = 0;
    return *kThemes[idx];
}

namespace UI {

uint32_t linkColor() {
    switch (gMesh.state()) {
    case LinkState::Ready:      return Color::CONNECTED();
    case LinkState::Scanning:
    case LinkState::Connecting:
    case LinkState::Pairing:
    case LinkState::Syncing:    return Color::SCANNING();
    default:                    return gMesh.hasSavedRadio() ? Color::ERROR_CLR() : Color::DISCONNECTED();
    }
}

void header(M5Canvas& c, const std::string& title) {
    c.fillRect(0, 0, Layout::SCREEN_W, Layout::HEADER_H, Color::HEADER_BG());
    c.drawFastHLine(0, Layout::HEADER_H - 1, Layout::SCREEN_W, Color::SEPARATOR());
    c.fillRect(Layout::MARGIN + 1, (Layout::HEADER_H - 5) / 2, 5, 5, linkColor());

    std::string right;
    if (NostrService::state() == NostrService::State::Online) right += "@ ";  // internet link up
    int unread = Chat::store().unreadCount();
    if (unread > 0) right += "*" + std::to_string(unread) + " ";
    int bat = Board::batteryPercent();
    if (bat >= 0) right += std::to_string(bat) + "%";

    int rightW = static_cast<int>(right.size()) * Layout::CHAR_W;
    int titleCols = (Layout::SCREEN_W - Layout::MARGIN * 2 - 10 - rightW) / Layout::CHAR_W - 1;
    int ty = (Layout::HEADER_H - Layout::CHAR_H) / 2;
    c.setTextColor(Color::HEADER_TEXT(), Color::HEADER_BG());
    c.drawString(TextUtil::ellipsize(title, titleCols > 0 ? titleCols : 0).c_str(), Layout::MARGIN + 10, ty);
    if (!right.empty()) {
        c.setTextColor(unread > 0 ? Color::ACCENT() : Color::DIM(), Color::HEADER_BG());
        c.drawString(right.c_str(), Layout::SCREEN_W - rightW - Layout::MARGIN, ty);
    }
}

bool toastVisible() {
    return !Chat::toast().empty() && millis() - Chat::toastAtMs() < kToastMs;
}

void footer(M5Canvas& c, const std::string& hint) {
    int y = Layout::SCREEN_H - Layout::FOOTER_H;
    c.fillRect(0, y, Layout::SCREEN_W, Layout::FOOTER_H, Color::HEADER_BG());
    c.drawFastHLine(0, y, Layout::SCREEN_W, Color::SEPARATOR());
    bool toast = toastVisible();
    const std::string& text = toast ? Chat::toast() : hint;
    c.setTextColor(toast ? Color::ACCENT() : Color::DIM(), Color::HEADER_BG());
    c.drawString(TextUtil::ellipsize(text, Layout::COLS).c_str(), Layout::MARGIN,
                 y + (Layout::FOOTER_H - Layout::CHAR_H) / 2 + 1);
}

void clearContent(M5Canvas& c) {
    c.fillRect(0, Layout::CONTENT_Y, Layout::SCREEN_W, Layout::CONTENT_H, Color::BG());
}

void row(M5Canvas& c, int visibleIndex, const std::string& text, bool selected,
         const std::string& right) {
    int y = Layout::CONTENT_Y + visibleIndex * Layout::LINE_H;
    uint32_t bg = selected ? Color::SELECTED_BG() : Color::BG();
    c.fillRect(0, y, Layout::SCREEN_W, Layout::LINE_H, bg);
    if (selected) c.fillRect(0, y, Layout::INDICATOR_W, Layout::LINE_H, Color::ACCENT());

    int textX = Layout::MARGIN + Layout::INDICATOR_W + 2;
    int rightCols = static_cast<int>(right.size());
    int cols = (Layout::SCREEN_W - textX - Layout::MARGIN) / Layout::CHAR_W - (rightCols ? rightCols + 1 : 0);
    int ty = y + (Layout::LINE_H - Layout::CHAR_H) / 2;
    c.setTextColor(selected ? Color::ACCENT() : Color::TEXT(), bg);
    c.drawString(TextUtil::ellipsize(text, cols > 0 ? cols : 0).c_str(), textX, ty);
    if (rightCols) {
        c.setTextColor(Color::DIM(), bg);
        c.drawString(right.c_str(), Layout::SCREEN_W - rightCols * Layout::CHAR_W - Layout::MARGIN, ty);
    }
}

void centered(M5Canvas& c, const std::string& text, uint32_t color, int dy) {
    std::string t = TextUtil::ellipsize(text, Layout::COLS);
    int w = static_cast<int>(t.size()) * Layout::CHAR_W;
    c.setTextColor(color, Color::BG());
    c.drawString(t.c_str(), (Layout::SCREEN_W - w) / 2,
                 Layout::CONTENT_Y + Layout::CONTENT_H / 2 - Layout::CHAR_H / 2 + dy);
}

void field(M5Canvas& c, int r, const std::string& label, const std::string& value, uint32_t valueColor) {
    int y = Layout::CONTENT_Y + r * Layout::LINE_H + (Layout::LINE_H - Layout::CHAR_H) / 2;
    constexpr int kLabelCols = 10;
    c.setTextColor(Color::ACCENT(), Color::BG());
    c.drawString(label.c_str(), Layout::MARGIN, y);
    c.setTextColor(valueColor ? valueColor : Color::TEXT(), Color::BG());
    c.drawString(TextUtil::ellipsize(value, Layout::COLS - kLabelCols).c_str(),
                 Layout::MARGIN + kLabelCols * Layout::CHAR_W, y);
}

} // namespace UI

bool ListView::move(const KeyInput& k, int count, int rows) {
    int before = sel;
    if (k.up() && sel > 0) --sel;
    if (k.down() && sel < count - 1) ++sel;
    clamp(count, rows);
    return sel != before;
}

bool ListView::tapped(const KeyInput& k, int count, int rows, int rowOffset) {
    int r = k.tapRow - rowOffset;
    if (k.tapRow < 0 || r < 0 || r >= rows) return false;
    int idx = top + r;
    if (idx >= count) return false;
    sel = idx;
    return true;
}

void ListView::clamp(int count, int rows) {
    if (count <= 0) { sel = 0; top = 0; return; }
    if (sel >= count) sel = count - 1;
    if (sel < 0) sel = 0;
    if (sel < top) top = sel;
    if (sel >= top + rows) top = sel - rows + 1;
    if (top > count - rows) top = count - rows > 0 ? count - rows : 0;
}

bool LineEdit::handle(const KeyInput& k) {
    bool changed = false;
    for (char ch : k.chars) {
        if (ch == '`') continue;  // ESC
        if (digitsOnly && (ch < '0' || ch > '9')) continue;
        if (text.size() + 1 > maxBytes) break;
        text += ch;
        changed = true;
    }
    if (k.del && !text.empty()) {
        // Remove one whole UTF-8 code point (emoji are multi-byte).
        size_t cut = text.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
        text.erase(cut);
        changed = true;
    }
    return changed;
}
