#include "screens/screens.h"

#include <algorithm>

#include "chat_service.h"
#include "core/text_util.h"
#include "meshtastic_client.h"

HomeScreen gHomeScreen;

// ---- navigation ------------------------------------------------------------

namespace {
std::vector<Screen*> sStack;
}

namespace Nav {

void reset(Screen* root) {
    if (!sStack.empty()) sStack.back()->hide();
    sStack.clear();
    sStack.push_back(root);
    root->show();
    root->invalidate();
}

void push(Screen* s) {
    auto it = std::find(sStack.begin(), sStack.end(), s);
    if (it != sStack.end()) {
        if (sStack.back() == s) return;
        sStack.back()->hide();
        sStack.erase(it + 1, sStack.end());
    } else {
        if (!sStack.empty()) sStack.back()->hide();
        sStack.push_back(s);
    }
    s->show();
    s->invalidate();
}

void pop() {
    if (sStack.size() <= 1) return;
    sStack.back()->hide();
    sStack.pop_back();
    sStack.back()->show();
    sStack.back()->invalidate();
}

Screen* current() { return sStack.empty() ? nullptr : sStack.back(); }

} // namespace Nav

void openChat(int convIdx) {
    if (convIdx < 0) return;
    gChatScreen.open(convIdx);
    Nav::push(&gChatScreen);
}

std::string ageLabel(uint32_t epochSeconds) {
    uint32_t now = gMesh.epochNow();
    if (epochSeconds == 0 || now == 0) return "";
    uint32_t age = now > epochSeconds ? now - epochSeconds : 0;
    if (age < 90) return "now";
    if (age < 3600) return std::to_string(age / 60) + "m";
    if (age < 86400) return std::to_string(age / 3600) + "h";
    return std::to_string(age / 86400) + "d";
}

// ---- home --------------------------------------------------------------------

namespace {

int activeChannelCount() {
    int n = 0;
    for (const auto& ch : gMesh.channels()) if (ch.role != 0) ++n;
    return n;
}

std::string radioLine(std::string& right) {
    const std::string name = gMesh.radio().name.empty() ? gMesh.radio().addr : gMesh.radio().name;
    switch (gMesh.state()) {
    case LinkState::Ready:
        right = "ok";
        return "Radio  " + TextUtil::toDisplay(name);
    case LinkState::Connecting:
    case LinkState::Syncing:
    case LinkState::Pairing:
        right = "...";
        return "Radio  " + TextUtil::toDisplay(name);
    case LinkState::Scanning:
        right = "scan";
        return "Radio  searching";
    default:
        break;
    }
    if (!gMesh.hasSavedRadio()) {
        right = "";
        return "Connect a radio";
    }
    uint32_t wait = gMesh.reconnectInMs();
    right = wait ? "retry " + std::to_string((wait + 999) / 1000) + "s" : "off";
    return "Radio  " + TextUtil::toDisplay(name);
}

} // namespace

void HomeScreen::show() {
    rebuild();
}

void HomeScreen::rebuild() {
    _entries.clear();
    _entries.push_back({Kind::Radio, -1});
    _entries.push_back({Kind::Channels, -1});
    _entries.push_back({Kind::Nodes, -1});
    for (int idx : Chat::store().homeOrder()) _entries.push_back({Kind::Conversation, idx});
    _entries.push_back({Kind::Settings, -1});
    _list.clamp(static_cast<int>(_entries.size()));
    _seenChat = Chat::store().revision();
    _seenMesh = gMesh.revision();
}

void HomeScreen::tick() {
    if (Chat::store().revision() != _seenChat || gMesh.revision() != _seenMesh) {
        rebuild();
        invalidate();
    }
}

void HomeScreen::key(const KeyInput& k) {
    tick();  // conversation indices must be current before acting on them
    if (_list.move(k, static_cast<int>(_entries.size()))) invalidate();
    if (k.has('r')) { Nav::push(&gRadioScreen); return; }
    if (k.has('c')) { Nav::push(&gChannelsScreen); return; }
    if (k.has('n')) { Nav::push(&gNodesScreen); return; }
    if (k.has('s')) { Nav::push(&gSettingsScreen); return; }
    if (!k.enter || _entries.empty()) return;

    const Entry& e = _entries[_list.sel];
    switch (e.kind) {
    case Kind::Radio:        Nav::push(&gRadioScreen); break;
    case Kind::Channels:     Nav::push(&gChannelsScreen); break;
    case Kind::Nodes:        Nav::push(&gNodesScreen); break;
    case Kind::Conversation: openChat(e.conv); break;
    case Kind::Settings:     Nav::push(&gSettingsScreen); break;
    }
}

void HomeScreen::draw(M5Canvas& c) {
    std::string title = "Meshputer";
    uint32_t me = gMesh.myNodeNum();
    if (me != 0 && gMesh.node(me)) title += "  " + TextUtil::toDisplay(gMesh.nodeShortName(me));
    UI::header(c, title);
    UI::clearContent(c);

    int nodeCount = 0;
    for (const auto& n : gMesh.nodes()) if (n.num != me) ++nodeCount;

    for (int i = _list.top; i < static_cast<int>(_entries.size()) && _list.visible(i); ++i) {
        const Entry& e = _entries[i];
        bool sel = i == _list.sel;
        int r = i - _list.top;
        switch (e.kind) {
        case Kind::Radio: {
            std::string right;
            std::string text = radioLine(right);
            UI::row(c, r, text, sel, right);
            break;
        }
        case Kind::Channels:
            UI::row(c, r, "Channels", sel, gMesh.channels().empty() ? "" : std::to_string(activeChannelCount()));
            break;
        case Kind::Nodes:
            UI::row(c, r, "Nodes / new chat", sel, nodeCount ? std::to_string(nodeCount) : "");
            break;
        case Kind::Conversation: {
            const Conversation* conv = Chat::store().at(e.conv);
            if (!conv) break;
            std::string text = Chat::title(*conv) + "  " + Chat::preview(*conv, Layout::COLS);
            UI::row(c, r, text, sel, conv->unread ? "*" : "");
            break;
        }
        case Kind::Settings:
            UI::row(c, r, "Settings", sel);
            break;
        }
    }
    UI::footer(c, "Enter open  r radio  n nodes  c chan");
}
