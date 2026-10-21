#include "screens/screens.h"

#include <algorithm>
#include <cstdio>

#include "chat_service.h"
#include "core/text_util.h"
#include "meshtastic_client.h"

NodesScreen gNodesScreen;
NodeInfoScreen gNodeInfoScreen;
ChannelsScreen gChannelsScreen;

namespace {

std::string hopsLabel(const MeshNode& n) {
    if (!n.hasHopsAway) return "";
    if (n.hopsAway == 0) return "direct";
    return std::to_string(n.hopsAway) + "hop";
}

std::string padded(const std::string& s, size_t width) {
    std::string out = s.substr(0, width);
    while (out.size() < width) out += ' ';
    return out;
}

} // namespace

// ---- node list ----------------------------------------------------------------

void NodesScreen::show() {
    rebuild();
}

void NodesScreen::rebuild() {
    uint32_t me = gMesh.myNodeNum();
    std::vector<const MeshNode*> list;
    for (const auto& n : gMesh.nodes()) if (n.num != me) list.push_back(&n);
    std::sort(list.begin(), list.end(), [](const MeshNode* a, const MeshNode* b) {
        if (a->heardMs != b->heardMs) return a->heardMs > b->heardMs;  // heard this session first
        return a->lastHeard > b->lastHeard;
    });
    // Keep the highlight on the same node when the order changes.
    uint32_t selected = _list.sel < static_cast<int>(_nodes.size()) ? _nodes[_list.sel] : 0;
    _nodes.clear();
    for (const MeshNode* n : list) _nodes.push_back(n->num);
    for (int i = 0; i < static_cast<int>(_nodes.size()); ++i) {
        if (_nodes[i] == selected) { _list.sel = i; break; }
    }
    _list.clamp(static_cast<int>(_nodes.size()));
    _seenMesh = gMesh.revision();
}

void NodesScreen::tick() {
    if (gMesh.revision() != _seenMesh) {
        rebuild();
        invalidate();
    }
}

void NodesScreen::key(const KeyInput& k) {
    tick();
    if (k.del || k.esc()) { Nav::pop(); return; }
    if (_list.move(k, static_cast<int>(_nodes.size()))) invalidate();
    if (_nodes.empty()) return;
    uint32_t num = _nodes[_list.sel];
    if (k.has('i') || k.right()) {
        gNodeInfoScreen.open(num);
        Nav::push(&gNodeInfoScreen);
    } else if (k.enter) {
        openChat(Chat::openDirect(num));
    }
}

void NodesScreen::draw(M5Canvas& c) {
    UI::header(c, "Nodes  " + std::to_string(_nodes.size()));
    UI::clearContent(c);
    if (_nodes.empty()) {
        UI::centered(c, gMesh.ready() ? "No other nodes heard yet" : "Connect a radio first", Color::DIM());
        UI::footer(c, "Del back");
        return;
    }
    for (int i = _list.top; i < static_cast<int>(_nodes.size()) && _list.visible(i); ++i) {
        const MeshNode* n = gMesh.node(_nodes[i]);
        if (!n) continue;
        std::string text = padded(TextUtil::toDisplay(gMesh.nodeShortName(n->num)), 4) + " " +
                           TextUtil::toDisplay(gMesh.nodeName(n->num));
        std::string right = hopsLabel(*n);
        std::string age = ageLabel(n->lastHeard);
        if (!age.empty()) right += (right.empty() ? "" : " ") + age;
        UI::row(c, i - _list.top, text, i == _list.sel, right);
    }
    UI::footer(c, "Enter chat  i identity  Del back");
}

// ---- node identity ------------------------------------------------------------

void NodeInfoScreen::tick() {
    if (gMesh.revision() != _seenMesh) {
        _seenMesh = gMesh.revision();
        invalidate();
    }
}

void NodeInfoScreen::key(const KeyInput& k) {
    if (k.del || k.esc()) { Nav::pop(); return; }
    if (k.up() && _scroll > 0) { --_scroll; invalidate(); }
    if (k.down()) { ++_scroll; invalidate(); }
    if (k.enter && _num != gMesh.myNodeNum()) openChat(Chat::openDirect(_num));
}

void NodeInfoScreen::draw(M5Canvas& c) {
    const MeshNode* n = gMesh.node(_num);
    bool isMe = _num == gMesh.myNodeNum();
    UI::header(c, isMe ? "This radio" : "Identity");
    UI::clearContent(c);

    struct Line { const char* label; std::string value; };
    std::vector<Line> lines;
    lines.push_back({"Name", TextUtil::toDisplay(gMesh.nodeName(_num))});
    lines.push_back({"Short", TextUtil::toDisplay(gMesh.nodeShortName(_num))});
    std::string id = (n && !n->user.id.empty()) ? n->user.id : MeshProto::nodeIdString(_num);
    lines.push_back({"Node ID", id});
    if (n && n->hasUser) {
        lines.push_back({"Hardware", MeshProto::hwModelName(n->user.hwModel)});
        lines.push_back({"Role", MeshProto::roleName(n->user.role)});
        lines.push_back({"DM crypto", n->user.hasPublicKey ? "public key (PKI)" : "channel key"});
    } else {
        lines.push_back({"Identity", "not announced yet"});
    }
    if (n && !isMe) {
        std::string age = ageLabel(n->lastHeard);
        lines.push_back({"Heard", age.empty() ? "unknown" : (age == "now" ? age : age + " ago")});
        std::string hops = hopsLabel(*n);
        lines.push_back({"Hops", hops.empty() ? "unknown" : hops});
        char snr[16];
        snprintf(snr, sizeof(snr), "%.1f dB", n->snr);
        lines.push_back({"SNR", snr});
        if (n->viaMqtt) lines.push_back({"Via", "MQTT"});
    }
    if (n && n->hasBattery) {
        std::string bat = n->batteryLevel > 100 ? "powered" : std::to_string(n->batteryLevel) + "%";
        if (n->hasVoltage) {
            char v[12];
            snprintf(v, sizeof(v), " %.2fV", n->voltage);
            bat += v;
        }
        lines.push_back({"Battery", bat});
    }

    int maxScroll = static_cast<int>(lines.size()) - Layout::ROWS;
    if (_scroll > maxScroll) _scroll = maxScroll > 0 ? maxScroll : 0;
    for (int i = _scroll; i < static_cast<int>(lines.size()) && i - _scroll < Layout::ROWS; ++i) {
        UI::field(c, i - _scroll, lines[i].label, lines[i].value);
    }
    UI::footer(c, isMe ? "Edit in Settings  Del back" : "Enter chat  ;/. scroll  Del back");
}

// ---- channels -------------------------------------------------------------------

std::vector<uint8_t> ChannelsScreen::active() {
    std::vector<uint8_t> out;
    for (const auto& ch : gMesh.channels()) {
        if (ch.role != 0 && ch.index >= 0) out.push_back(static_cast<uint8_t>(ch.index));
    }
    return out;
}

void ChannelsScreen::show() {
    _list.clamp(static_cast<int>(active().size()));
}

void ChannelsScreen::key(const KeyInput& k) {
    if (k.del || k.esc()) { Nav::pop(); return; }
    std::vector<uint8_t> chans = active();
    if (_list.move(k, static_cast<int>(chans.size()))) invalidate();
    bool open = k.enter;
    if (open && !chans.empty()) openChat(Chat::openChannel(chans[_list.sel]));
}

void ChannelsScreen::draw(M5Canvas& c) {
    UI::header(c, "Channels");
    UI::clearContent(c);
    std::vector<uint8_t> chans = active();
    if (chans.empty()) {
        UI::centered(c, gMesh.ready() ? "No channels on this radio" : "Connect a radio first", Color::DIM());
        UI::footer(c, "Del back");
        return;
    }
    for (int i = _list.top; i < static_cast<int>(chans.size()) && _list.visible(i); ++i) {
        const MeshProto::Channel* ch = nullptr;
        for (const auto& x : gMesh.channels()) if (x.index == chans[i]) ch = &x;
        std::string right;
        if (ch && ch->role == 1) right = "primary";
        bool open = ch && (ch->pskLen == 0 || (ch->pskLen == 1 && ch->pskFirstByte == 0));
        if (open) right += right.empty() ? "open" : " open";
        UI::row(c, i - _list.top, "#" + TextUtil::toDisplay(gMesh.channelName(chans[i])), i == _list.sel, right);
    }
    UI::footer(c, "Enter chat  Del back");
}
