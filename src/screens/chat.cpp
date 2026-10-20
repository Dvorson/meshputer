#include "screens/screens.h"

#include <Arduino.h>

#include "chat_service.h"
#include "core/emoji.h"
#include "core/mesh_protocol.h"
#include "core/text_util.h"
#include "meshtastic_client.h"
#include "settings.h"

ChatScreen gChatScreen;
CannedScreen gCannedScreen;
EmojiScreen gEmojiScreen;

namespace {

constexpr uint32_t kNoticeMs = 3000;
constexpr int kTextLineH = 10;
constexpr int kBubbleIndent = 18;
constexpr int kMaxLinesPerMessage = 12;

int messagesBottom() { return Layout::SCREEN_H - Layout::FOOTER_H - Layout::INPUT_H; }

} // namespace

void ChatScreen::open(int convIdx) {
    _conv = convIdx;
    if (const Conversation* c = Chat::store().at(convIdx)) _key = c->key;
    _input.text.clear();
    _input.maxBytes = MeshProto::kMaxTextBytes;
    _scroll = 0;
    _notice.clear();
}

void ChatScreen::show() {
    resolve();
    Chat::setActive(_conv);
    _seenChat = Chat::store().revision();
}

void ChatScreen::hide() {
    Chat::setActive(-1);
}

bool ChatScreen::resolve() {
    _conv = Chat::store().find(_key);
    return _conv >= 0;
}

void ChatScreen::tick() {
    if (Chat::store().revision() != _seenChat) {
        if (!resolve()) { Nav::pop(); return; }
        Chat::setActive(_conv);  // anything that arrived while on screen counts as read
        _seenChat = Chat::store().revision();
        invalidate();
    }
    if (!_notice.empty() && millis() - _noticeAt > kNoticeMs) {
        _notice.clear();
        invalidate();
    }
}

void ChatScreen::insert(const std::string& utf8) {
    if (_input.text.size() + utf8.size() <= _input.maxBytes) _input.text += utf8;
    invalidate();
}

void ChatScreen::sendNow(const std::string& utf8) {
    std::string err = Chat::send(_conv, utf8);
    if (err.empty()) {
        _scroll = 0;
    } else {
        _notice = err;
        _noticeAt = millis();
    }
    invalidate();
}

void ChatScreen::key(const KeyInput& k) {
    if (!resolve()) { Nav::pop(); return; }
    const Conversation* conv = Chat::store().at(_conv);
    if (!conv) { Nav::pop(); return; }

    if (k.fn) {
        int count = static_cast<int>(conv->messages.size());
        if (k.up() && _scroll < count - 1) { ++_scroll; invalidate(); }
        if (k.down() && _scroll > 0) { --_scroll; invalidate(); }
        if (k.has('r')) Nav::push(&gCannedScreen);
        else if (k.has('e')) Nav::push(&gEmojiScreen);
        else if (k.has('i') && !conv->key.isChannel) {
            gNodeInfoScreen.open(conv->key.peer);
            Nav::push(&gNodeInfoScreen);
        } else if (k.left() || k.esc()) Nav::pop();
        return;
    }
    if (k.esc()) { Nav::pop(); return; }
    if (k.enter) {
        if (!_input.text.empty()) {
            std::string text = _input.text;
            std::string err = Chat::send(_conv, text);
            if (err.empty()) {
                _input.text.clear();
                _scroll = 0;
            } else {
                _notice = err;
                _noticeAt = millis();
            }
            invalidate();
        }
        return;
    }
    if (k.del && _input.text.empty()) { Nav::pop(); return; }
    if (_input.handle(k)) invalidate();
}

void ChatScreen::drawMessages(M5Canvas& c) {
    const Conversation* conv = Chat::store().at(_conv);
    int top = Layout::CONTENT_Y;
    int bottom = messagesBottom();
    c.fillRect(0, top, Layout::SCREEN_W, bottom - top, Color::BG());
    if (!conv || conv->messages.empty()) {
        std::string hint = "No messages yet";
        if (conv && !conv->key.isChannel) hint = "Say hi to " + TextUtil::toDisplay(gMesh.nodeShortName(conv->key.peer));
        c.setTextColor(Color::DIM(), Color::BG());
        int w = static_cast<int>(hint.size()) * Layout::CHAR_W;
        c.drawString(hint.c_str(), (Layout::SCREEN_W - w) / 2, top + (bottom - top) / 2 - 4);
        return;
    }

    int bubbleW = Layout::SCREEN_W - 2 * Layout::MARGIN - kBubbleIndent;
    size_t cols = static_cast<size_t>((bubbleW - 8) / Layout::CHAR_W);

    c.setClipRect(0, top, Layout::SCREEN_W, bottom - top);
    int y = bottom;
    for (int i = static_cast<int>(conv->messages.size()) - 1 - _scroll; i >= 0 && y > top; --i) {
        const ChatMessage& m = conv->messages[i];
        std::vector<std::string> lines = TextUtil::wrap(TextUtil::toDisplay(m.text), cols);
        if (static_cast<int>(lines.size()) > kMaxLinesPerMessage) {
            lines.resize(kMaxLinesPerMessage);
            lines.back() = TextUtil::ellipsize(lines.back() + "...", cols);
        }
        int h = kTextLineH * (1 + static_cast<int>(lines.size())) + 4;
        int msgTop = y - h;
        int x = m.fromMe ? Layout::MARGIN + kBubbleIndent : Layout::MARGIN;
        uint32_t bg = m.fromMe ? Color::MSG_MINE() : Color::MSG_OTHER();
        c.fillRoundRect(x, msgTop, bubbleW, h - 2, 3, bg);
        if (m.fromMe) c.fillRect(x, msgTop, 2, h - 2, Color::ACCENT());

        // Sender line: who, and for our own messages how far it got.
        std::string who = Chat::senderLabel(m);
        std::string status;
        if (m.fromMe) status = deliveryLabel(m.delivery);
        else status = ageLabel(m.timestamp);
        c.setTextColor(m.fromMe ? Color::ACCENT() : Color::SCANNING(), bg);
        c.drawString(who.c_str(), x + 5, msgTop + 2);
        if (!status.empty()) {
            uint32_t sc = m.delivery == Delivery::Failed ? Color::ERROR_CLR()
                        : m.delivery == Delivery::Delivered ? Color::CONNECTED() : Color::DIM();
            c.setTextColor(sc, bg);
            int sw = static_cast<int>(status.size()) * Layout::CHAR_W;
            c.drawString(status.c_str(), x + bubbleW - sw - 4, msgTop + 2);
        }
        c.setTextColor(Color::TEXT(), bg);
        int ty = msgTop + 2 + kTextLineH;
        for (const auto& line : lines) {
            c.drawString(line.c_str(), x + 5, ty);
            ty += kTextLineH;
        }
        y = msgTop - 2;
    }
    c.clearClipRect();
}

void ChatScreen::drawInput(M5Canvas& c) {
    int y = messagesBottom();
    c.fillRect(0, y, Layout::SCREEN_W, Layout::INPUT_H, Color::INPUT_BG());
    c.drawFastHLine(0, y, Layout::SCREEN_W, Color::SEPARATOR());
    int ty = y + (Layout::INPUT_H - Layout::CHAR_H) / 2 + 1;
    c.setTextColor(Color::ACCENT(), Color::INPUT_BG());
    c.drawString(">", Layout::MARGIN, ty);

    std::string left = std::to_string(_input.maxBytes - _input.text.size());
    int cols = Layout::COLS - 3 - static_cast<int>(left.size());
    std::string shown = TextUtil::toDisplay(_input.text) + "_";
    if (static_cast<int>(shown.size()) > cols) shown = shown.substr(shown.size() - cols);
    c.setTextColor(Color::TEXT(), Color::INPUT_BG());
    c.drawString(shown.c_str(), Layout::MARGIN + 2 * Layout::CHAR_W, ty);
    c.setTextColor(Color::DIM(), Color::INPUT_BG());
    c.drawString(left.c_str(), Layout::SCREEN_W - Layout::MARGIN - static_cast<int>(left.size()) * Layout::CHAR_W, ty);
}

void ChatScreen::draw(M5Canvas& c) {
    const Conversation* conv = Chat::store().at(_conv);
    drawMessages(c);
    drawInput(c);
    UI::header(c, conv ? Chat::title(*conv) : "Chat");
    if (!_notice.empty()) {
        UI::footer(c, "");
        int y = Layout::SCREEN_H - Layout::FOOTER_H;
        c.setTextColor(Color::ERROR_CLR(), Color::HEADER_BG());
        c.drawString(_notice.c_str(), Layout::MARGIN, y + (Layout::FOOTER_H - Layout::CHAR_H) / 2 + 1);
    } else {
        bool dm = conv && !conv->key.isChannel;
        UI::footer(c, dm ? "Ent send fn+;/. scroll fn+r/e/i" : "Ent send fn+;/. scroll fn+r/e");
    }
}

// ---- canned replies ------------------------------------------------------------

void CannedScreen::key(const KeyInput& k) {
    if (k.del || k.esc()) { Nav::pop(); return; }
    if (_list.move(k, kMaxCannedResponses)) invalidate();
    if (k.enter) {
        const std::string& text = Settings::get().canned[_list.sel];
        if (text.empty()) return;
        Nav::pop();
        gChatScreen.sendNow(text);
    }
}

void CannedScreen::draw(M5Canvas& c) {
    UI::header(c, "Quick replies");
    UI::clearContent(c);
    const Settings& s = Settings::get();
    for (int i = _list.top; i < kMaxCannedResponses && _list.visible(i); ++i) {
        std::string label = std::to_string(i + 1) + ". " + (s.canned[i].empty() ? "(empty)" : s.canned[i]);
        UI::row(c, i - _list.top, label, i == _list.sel);
    }
    UI::footer(c, "Enter send  Del back");
}

// ---- emoji picker ------------------------------------------------------------------

namespace {
constexpr int kCols = 8;
constexpr int kCellW = 28;
constexpr int kCellH = 22;
constexpr int kGridRows = (Layout::CONTENT_H - Layout::LINE_H) / kCellH;

void drawEmoji(M5Canvas& c, int x, int y, const uint8_t* bitmap, uint32_t fg) {
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            if ((bitmap[row] >> (7 - col)) & 1) c.fillRect(x + col * 2, y + row * 2, 2, 2, fg);
        }
    }
}
} // namespace

void EmojiScreen::key(const KeyInput& k) {
    if (k.del || k.esc()) { Nav::pop(); return; }
    int before = _sel;
    if (k.up() && _sel >= kCols) _sel -= kCols;
    if (k.down() && _sel + kCols < kEmojiCount) _sel += kCols;
    if (k.left() && _sel > 0) --_sel;
    if (k.right() && _sel < kEmojiCount - 1) ++_sel;
    if (_sel != before) {
        int row = _sel / kCols;
        if (row < _topRow) _topRow = row;
        if (row >= _topRow + kGridRows) _topRow = row - kGridRows + 1;
        invalidate();
    }
    if (k.enter) {
        Nav::pop();
        gChatScreen.insert(kEmojis[_sel].utf8);
    }
}

void EmojiScreen::draw(M5Canvas& c) {
    UI::header(c, "Emoji");
    UI::clearContent(c);
    c.setTextColor(Color::DIM(), Color::BG());
    c.drawString(kEmojis[_sel].category, Layout::MARGIN, Layout::CONTENT_Y + 3);
    c.setTextColor(Color::ACCENT(), Color::BG());
    int cw = static_cast<int>(strlen(kEmojis[_sel].code)) * Layout::CHAR_W;
    c.drawString(kEmojis[_sel].code, Layout::SCREEN_W - cw - Layout::MARGIN, Layout::CONTENT_Y + 3);

    int startX = (Layout::SCREEN_W - kCols * kCellW) / 2;
    int gridY = Layout::CONTENT_Y + Layout::LINE_H;
    for (int r = 0; r < kGridRows; ++r) {
        for (int col = 0; col < kCols; ++col) {
            int idx = (_topRow + r) * kCols + col;
            if (idx >= kEmojiCount) break;
            int x = startX + col * kCellW + (kCellW - 16) / 2;
            int y = gridY + r * kCellH + (kCellH - 16) / 2;
            bool sel = idx == _sel;
            if (sel) c.fillRoundRect(x - 3, y - 3, 22, 22, 3, Color::SELECTED_BG());
            drawEmoji(c, x, y, kEmojis[idx].bitmap, sel ? Color::ACCENT() : Color::TEXT());
        }
    }
    UI::footer(c, ";./, move  Enter insert  Del back");
}
