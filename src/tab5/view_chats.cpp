// Chats: every conversation (mesh DMs, channels, internet DMs) and the
// conversation itself with a composer docked above the keyboard.
#if defined(MESHPUTER_BOARD_TAB5)

#include <algorithm>
#include <vector>

#include "board.h"
#include "chat_service.h"
#include "core/crypto.h"
#include "core/emoji.h"
#include "core/mesh_protocol.h"
#include "core/nostr.h"
#include "kit.h"
#include "meshtastic_client.h"
#include "nostr_service.h"
#include "settings.h"
#include "views.h"

namespace Tab5 {

namespace {

constexpr uint32_t kListRefreshMs = 400;

struct Entry {
    int conv = -1;        // conversation index, or -1 for a channel without one
    uint8_t channel = 0;
};

uint32_t seedFor(const ConversationKey& k) {
    if (k.isNostr()) {
        uint32_t h = 2166136261u;
        for (char c : k.nostr) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
        return h;
    }
    return k.isChannel ? 0xC0DE0000u + k.channel : k.peer;
}

std::string avatarLabel(const ConversationKey& k) {
    if (k.isNostr()) return "@";
    if (k.isChannel) return "#";
    return Board::screenText(gMesh.nodeShortName(k.peer));
}

// A horizontally scrolling row of chips.
lv_obj_t* quickRow(lv_obj_t* parent) {
    lv_obj_t* row = Kit::box(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_hor(row, 16, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    return row;
}

std::string deliveryText(Delivery d) {
    switch (d) {
    case Delivery::Queued: return "Sending";
    case Delivery::Relayed: return "Sent " LV_SYMBOL_OK;
    case Delivery::Delivered: return "Delivered " LV_SYMBOL_OK;
    case Delivery::Failed: return "Not delivered";
    default: return "";
    }
}

std::string describe(const ConversationKey& k) {
    if (k.isNostr()) {
        uint8_t pub[32];
        std::string hrp, npub = k.nostr;
        if (Nostr::decodeKey(k.nostr, hrp, pub)) npub = Nostr::npub(pub);
        return "Internet (Nostr), " + (npub.size() > 20 ? npub.substr(0, 14) + "..." + npub.substr(npub.size() - 6) : npub);
    }
    if (k.isChannel) return "Channel " + std::to_string(k.channel) + ", everyone on it";
    std::string s = MeshProto::nodeIdString(k.peer);
    if (const MeshNode* n = gMesh.node(k.peer)) {
        if (n->hasHopsAway) s += n->hopsAway == 0 ? ", direct" : ", " + std::to_string(n->hopsAway) + " hops";
        std::string heard = ago(n->lastHeard);
        if (!heard.empty()) s += ", heard " + heard;
    }
    return s;
}

class ChatsView : public View {
public:
    const char* name() const override { return "chats"; }
    const char* detailName() const override { return route().newChat ? "new" : "chat"; }
    const char* label() const override { return "Chats"; }
    const char* icon() const override { return LV_SYMBOL_ENVELOPE; }

    bool hasDetail() const override {
        const Route& r = route();
        return r.newChat || (r.hasConv && Chat::store().find(r.conv) >= 0);
    }

    bool fullScreenDetail() const override { return true; }  // room for the composer
    void buildList(Pane& pane) override;
    void buildDetail(Pane& pane) override;
    void buildPlaceholder(Pane& pane) override {
        Kit::emptyState(pane.body, LV_SYMBOL_ENVELOPE, "Pick a conversation",
                        "Or open a node in Nodes to message it.");
    }
    void update() override;
    void cleared(uint8_t parts) override {
        if (parts & kList) _list = nullptr, _pane = nullptr;
        if (parts & kDetail) _messages = _input = _quick = _npub = nullptr, _detail = nullptr;
    }

private:
    void rebuildEntries();
    std::string listSignature();
    void buildRows();
    void buildConversation(Pane& pane);
    void buildNewChat(Pane& pane);
    void buildMessages();
    void send(const std::string& text);

    static void onRow(lv_event_t* e);
    static void onNew(lv_event_t*) { openNewChat(); }
    static void onSend(lv_event_t* e);
    static void onQuickToggle(lv_event_t* e);
    static void onQuick(lv_event_t* e);
    static void onEmoji(lv_event_t* e);
    static void onNode(lv_event_t*);
    static void onStart(lv_event_t* e);
    static void onCancelNew(lv_event_t*) { back(); }

    std::vector<Entry> _entries;
    Pane* _pane = nullptr;    // the list's pane
    Pane* _detail = nullptr;  // the conversation's pane
    std::string _headShown;
    lv_obj_t* _list = nullptr;
    lv_obj_t* _messages = nullptr;
    lv_obj_t* _input = nullptr;
    lv_obj_t* _quick = nullptr;
    lv_obj_t* _npub = nullptr;
    int _bubbleTextW = 480;
    uint32_t _seenChat = 0;
    uint32_t _seenMesh = 0;
    uint32_t _listAt = 0;
    std::string _listSig;
    uint32_t _messagesRev = 0;
};

ChatsView sView;

// ---- list -----------------------------------------------------------------------

void ChatsView::rebuildEntries() {
    _entries.clear();
    const ConversationStore& store = Chat::store();
    for (int idx : store.homeOrder()) _entries.push_back({idx, 0});
    // Channels nobody has written on yet, so they can be opened.
    for (const auto& ch : gMesh.channels()) {
        if (ch.role == 0 || ch.index < 0) continue;
        uint8_t index = static_cast<uint8_t>(ch.index);
        bool listed = std::any_of(_entries.begin(), _entries.end(), [&](const Entry& e) {
            const Conversation* c = store.at(e.conv);
            return c && c->key.isChannel && c->key.channel == index;
        });
        if (!listed) _entries.push_back({-1, index});
    }
}

// Everything a row shows; the list is redrawn only when this changes.
std::string ChatsView::listSignature() {
    rebuildEntries();
    std::string sig = Kit::linkText();
    const ConversationStore& store = Chat::store();
    for (const Entry& e : _entries) {
        const Conversation* c = store.at(e.conv);
        if (!c) {
            sig += "|#" + gMesh.channelName(e.channel);
            continue;
        }
        sig += "|" + Chat::title(*c) + (c->unread ? "*" : "");
        if (!c->messages.empty()) sig += Chat::preview(*c, 120) + ago(c->messages.back().timestamp);
    }
    return sig;
}

void ChatsView::buildList(Pane& pane) {
    pane.setTitle("Chats");
    _pane = &pane;
    Kit::iconButton(pane.actions, LV_SYMBOL_PLUS, onNew);
    _list = Kit::scroller(pane.body);
    buildRows();
}

void ChatsView::buildRows() {
    const Kit::Palette& p = Kit::pal();
    int32_t scroll = lv_obj_get_scroll_y(_list);
    lv_obj_clean(_list);
    _listSig = listSignature();
    _seenChat = Chat::store().revision();
    _seenMesh = gMesh.revision();
    _listAt = millis();
    if (gMesh.ready()) _pane->setSubtitle("via " + Board::screenText(gMesh.radio().name));
    else if (gMesh.hasSavedRadio()) _pane->setSubtitle(std::string("Radio: ") + Kit::linkText());
    else _pane->setSubtitle("No radio connected");

    if (_entries.empty()) {
        if (!gMesh.hasSavedRadio()) {
            Kit::emptyState(_list, LV_SYMBOL_BLUETOOTH, "Connect your radio",
                            "Pair with a Meshtastic radio in Radio to chat over the mesh.");
            lv_obj_t* row = Kit::hrow(_list);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            Kit::button(row, "Open Radio", Kit::Btn::Primary, [](lv_event_t*) { showTab(Tab::Radio); });
        } else {
            Kit::emptyState(_list, LV_SYMBOL_ENVELOPE, "No messages yet",
                            "Channels appear once the radio has synced. Open a node in Nodes to message it.");
        }
        return;
    }

    const Route& r = route();
    const ConversationStore& store = Chat::store();
    for (size_t i = 0; i < _entries.size(); ++i) {
        const Entry& e = _entries[i];
        const Conversation* c = store.at(e.conv);
        ConversationKey key = c ? c->key : ConversationKey::onChannel(e.channel);
        bool selected = landscape() && r.hasConv && !r.newChat && r.conv == key;
        lv_obj_t* row = Kit::listRow(_list, selected, onRow, reinterpret_cast<void*>(i));
        Kit::avatar(row, avatarLabel(key), seedFor(key), 72);

        lv_obj_t* words = Kit::column(row, 4);
        lv_obj_set_width(words, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(words, 1);
        lv_obj_t* top = Kit::hrow(words, 12);
        std::string title = c ? Chat::title(*c) : "#" + Board::screenText(gMesh.channelName(e.channel));
        lv_obj_t* t = Kit::text(top, title, Kit::strong(), p.text);
        lv_obj_set_flex_grow(t, 1);
        Kit::oneLine(t);
        if (c && !c->messages.empty()) Kit::text(top, ago(c->messages.back().timestamp), Kit::caption(), p.text2);

        lv_obj_t* bottom = Kit::hrow(words, 12);
        std::string preview = c && !c->messages.empty() ? Chat::preview(*c, 120) : "No messages yet";
        lv_obj_t* pv = Kit::text(bottom, preview, Kit::small(), c && c->unread ? p.text : p.text2);
        lv_obj_set_flex_grow(pv, 1);
        Kit::oneLine(pv);
        if (c && c->unread) Kit::dot(bottom, p.accent, 18);
    }
    lv_obj_update_layout(_list);
    lv_obj_scroll_to_y(_list, scroll, LV_ANIM_OFF);
}

void ChatsView::onRow(lv_event_t* e) {
    size_t i = reinterpret_cast<size_t>(lv_event_get_user_data(e));
    if (i >= sView._entries.size()) return;
    const Entry& entry = sView._entries[i];
    int idx = entry.conv >= 0 ? entry.conv : Chat::openChannel(entry.channel);
    openConversation(idx);
}

// ---- conversation ---------------------------------------------------------------

void ChatsView::buildDetail(Pane& pane) {
    if (route().newChat) buildNewChat(pane);
    else buildConversation(pane);
}

void ChatsView::buildConversation(Pane& pane) {
    const Kit::Palette& p = Kit::pal();
    const ConversationKey key = route().conv;
    int idx = Chat::store().find(key);
    const Conversation* c = Chat::store().at(idx);
    if (!c) return;
    _detail = &pane;
    _headShown = Chat::title(*c) + "\n" + describe(key);
    pane.setTitle(Chat::title(*c));
    pane.setSubtitle(describe(key));
    if (!key.isChannel && !key.isNostr()) Kit::iconButton(pane.actions, LV_SYMBOL_EYE_OPEN, onNode);

    lv_obj_set_flex_flow(pane.body, LV_FLEX_FLOW_COLUMN);
    _messages = Kit::scroller(pane.body, 14);
    lv_obj_set_flex_grow(_messages, 1);
    lv_obj_set_style_pad_all(_messages, Kit::kPad, 0);

    // Quick replies (sent at once) and emoji (added to the message), above the composer.
    _quick = Kit::column(pane.body, 12);
    lv_obj_set_style_pad_bottom(_quick, 12, 0);
    lv_obj_t* replies = quickRow(_quick);
    const Settings& s = Settings::get();
    for (int i = 0; i < kMaxCannedResponses; ++i) {
        if (s.canned[i].empty()) continue;
        lv_obj_t* chip = Kit::button(replies, Board::screenText(s.canned[i]), Kit::Btn::Secondary, onQuick,
                                     reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_set_height(chip, 72);
        lv_obj_set_style_text_font(lv_obj_get_child(chip, 0), Kit::small(), 0);
    }
    lv_obj_t* emoji = quickRow(_quick);
    for (int i = 0; i < kEmojiCount; ++i) {
        if (static_cast<unsigned char>(kEmojis[i].utf8[0]) < 0x80) continue;  // text smileys like :)
        lv_obj_t* chip = Kit::button(emoji, kEmojis[i].utf8, Kit::Btn::Ghost, onEmoji,
                                     reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_set_size(chip, 72, 72);
        lv_obj_set_style_min_width(chip, 72, 0);
        lv_obj_set_style_pad_hor(chip, 0, 0);
        lv_obj_set_style_text_font(lv_obj_get_child(chip, 0), Kit::body(), 0);
    }
    lv_obj_add_flag(_quick, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* composer = Kit::hrow(pane.body, 12);
    lv_obj_set_style_pad_all(composer, 16, 0);
    lv_obj_set_style_bg_opa(composer, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(composer, p.surface, 0);
    Kit::iconButton(composer, LV_SYMBOL_LIST, onQuickToggle);
    _input = lv_textarea_create(composer);
    lv_obj_set_flex_grow(_input, 1);
    lv_obj_set_height(_input, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(_input, 200, 0);
    lv_textarea_set_placeholder_text(_input, "Message");
    lv_textarea_set_max_length(_input, MeshProto::kMaxTextBytes);
    lv_obj_set_style_text_font(_input, Kit::body(), 0);
    lv_obj_set_style_bg_color(_input, p.raised, 0);
    lv_obj_set_style_border_width(_input, 0, 0);
    lv_obj_set_style_radius(_input, 32, 0);
    lv_obj_set_style_pad_hor(_input, 24, 0);
    lv_obj_set_style_pad_ver(_input, 22, 0);
    lv_obj_add_flag(_input, LV_OBJ_FLAG_USER_1);  // keep the keyboard up after sending
    lv_obj_add_event_cb(_input, [](lv_event_t* e) { editText(lv_event_get_target_obj(e)); }, LV_EVENT_CLICKED,
                        nullptr);
    lv_obj_add_event_cb(_input, onSend, LV_EVENT_READY, nullptr);
    Kit::button(composer, "Send", Kit::Btn::Primary, onSend);

    // Keep the newest message in view when the keyboard opens or closes.
    lv_obj_add_event_cb(_messages, [](lv_event_t* e) {
        lv_obj_scroll_to_y(lv_event_get_target_obj(e), LV_COORD_MAX, LV_ANIM_OFF);
    }, LV_EVENT_SIZE_CHANGED, nullptr);

    lv_obj_update_layout(pane.body);
    _bubbleTextW = lv_obj_get_content_width(_messages) * 3 / 4 - 44;
    buildMessages();
}

void ChatsView::buildMessages() {
    const Kit::Palette& p = Kit::pal();
    lv_obj_clean(_messages);
    _messagesRev = Chat::store().revision();
    const Conversation* c = Chat::store().at(Chat::store().find(route().conv));
    if (!c) return;
    if (c->messages.empty()) {
        lv_obj_t* hint = Kit::para(_messages, c->key.isChannel ? "Nothing said here yet. Say hello to the channel."
                                                               : "No messages yet. Say hello.",
                                   Kit::body(), p.text2);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(hint, 80, 0);
        return;
    }
    bool showSender = !c->key.isNostr() && c->key.isChannel;
    for (const ChatMessage& m : c->messages) {
        lv_obj_t* line = Kit::hrow(_messages, 0);
        lv_obj_set_flex_align(line, m.fromMe ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_t* bubble = Kit::box(line);
        lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(bubble, 6, 0);
        lv_obj_set_style_pad_hor(bubble, 22, 0);
        lv_obj_set_style_pad_ver(bubble, 16, 0);
        lv_obj_set_style_radius(bubble, 28, 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bubble, m.fromMe ? p.mine : p.surface, 0);
        if (showSender && !m.fromMe) {
            Kit::text(bubble, Chat::senderLabel(*c, m) + "  " + Board::screenText(gMesh.nodeName(m.from)),
                      Kit::caption(), Kit::avatarColor(m.from));
        }
        lv_obj_t* body = Kit::text(bubble, Board::screenText(m.text), Kit::body(), p.text);
        lv_obj_set_style_max_width(body, _bubbleTextW, 0);
        std::string meta = ago(m.timestamp);
        if (m.fromMe) {
            std::string d = deliveryText(m.delivery);
            if (!d.empty()) meta += (meta.empty() ? "" : ", ") + d;
        }
        if (!meta.empty()) {
            Kit::text(bubble, meta, Kit::caption(), m.fromMe && m.delivery == Delivery::Failed ? p.err : p.text2);
        }
    }
    lv_obj_update_layout(_messages);
    lv_obj_scroll_to_y(_messages, LV_COORD_MAX, LV_ANIM_OFF);
}

void ChatsView::send(const std::string& text) {
    int idx = Chat::store().find(route().conv);
    std::string err = Chat::send(idx, text);
    if (!err.empty()) toast(err);
    else if (_input) lv_textarea_set_text(_input, "");
}

void ChatsView::onSend(lv_event_t*) {
    if (!sView._input) return;
    std::string text = lv_textarea_get_text(sView._input);
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
    if (text.empty()) return;
    sView.send(text);
}

void ChatsView::onQuickToggle(lv_event_t*) {
    if (!sView._quick) return;
    if (lv_obj_has_flag(sView._quick, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(sView._quick, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(sView._quick, LV_OBJ_FLAG_HIDDEN);
}

void ChatsView::onQuick(lv_event_t* e) {
    int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (i < 0 || i >= kMaxCannedResponses) return;
    sView.send(Settings::get().canned[i]);
    if (sView._quick) lv_obj_add_flag(sView._quick, LV_OBJ_FLAG_HIDDEN);
}

void ChatsView::onEmoji(lv_event_t* e) {
    int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (!sView._input || i < 0 || i >= kEmojiCount) return;
    lv_textarea_add_text(sView._input, kEmojis[i].utf8);
}

void ChatsView::onNode(lv_event_t*) {
    if (route().hasConv && !route().conv.isChannel && !route().conv.isNostr()) openNode(route().conv.peer);
}

// ---- new internet chat ------------------------------------------------------------

void ChatsView::buildNewChat(Pane& pane) {
    const Kit::Palette& p = Kit::pal();
    pane.setTitle("New internet chat");
    pane.setSubtitle("Private message over Nostr");
    lv_obj_t* page = Kit::scroller(pane.body, 24);
    lv_obj_set_style_pad_all(page, Kit::kPad, 0);
    Kit::para(page, "Type the other person's npub. Another Meshputer shows it in Settings > Internet; "
                    "phone apps such as Amethyst or 0xchat show it on the profile.",
              Kit::body(), p.text2);
    _npub = Kit::input(page, "Their npub", "", 120, "npub1...");
    lv_obj_add_event_cb(_npub, onStart, LV_EVENT_READY, nullptr);
    lv_obj_t* actions = Kit::hrow(page, 16);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    Kit::button(actions, "Cancel", Kit::Btn::Secondary, onCancelNew);
    Kit::button(actions, "Start chat", Kit::Btn::Primary, onStart);
    if (!Settings::get().internetEnabled) {
        Kit::para(page, "Internet is off. Turn it on in Settings > Internet to send and receive.",
                  Kit::small(), p.warn);
    }
    Kit::para(page, "To message someone on the mesh instead, open them in Nodes.", Kit::small(), p.text2);
}

void ChatsView::onStart(lv_event_t*) {
    if (!sView._npub) return;
    std::string text = lv_textarea_get_text(sView._npub);
    while (!text.empty() && text.back() == ' ') text.pop_back();
    while (!text.empty() && text.front() == ' ') text.erase(0, 1);
    if (text.compare(0, 6, "nostr:") == 0) text = text.substr(6);
    std::string hrp;
    uint8_t pub[32];
    if (!Nostr::decodeKey(text, hrp, pub) || (hrp != "npub" && hrp != "hex")) {
        toast("That is not an npub");
        return;
    }
    std::string hex = Crypto::toHex(pub, 32);
    if (hex == NostrService::pubHex()) {
        toast("That is this device's own npub");
        return;
    }
    openConversation(Chat::openNostr(hex));
}

// ---- refresh ------------------------------------------------------------------------

void ChatsView::update() {
    uint32_t chatRev = Chat::store().revision();
    if (_messages && chatRev != _messagesRev) {
        if (Chat::store().find(route().conv) < 0) {
            route().hasConv = false;  // gone, e.g. history of another radio loaded
            invalidate(kDetail);
        } else {
            buildMessages();
        }
    }
    bool changed = chatRev != _seenChat || gMesh.revision() != _seenMesh;
    if (!changed || millis() - _listAt < kListRefreshMs) return;
    _seenChat = chatRev;
    _seenMesh = gMesh.revision();
    _listAt = millis();
    if (_detail) {
        // Names arrive with the radio's node list, after the page was built.
        const Conversation* c = Chat::store().at(Chat::store().find(route().conv));
        std::string head = c ? Chat::title(*c) + "\n" + describe(c->key) : _headShown;
        if (head != _headShown) {
            _headShown = head;
            _detail->setTitle(Chat::title(*c));
            _detail->setSubtitle(describe(c->key));
        }
    }
    if (_list && listSignature() != _listSig) buildRows();
}

} // namespace

View& chatsView() { return sView; }

} // namespace Tab5

#endif
