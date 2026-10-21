// Nodes: everyone our radio has heard on the mesh, and each node's identity
// and signal, with a way to message it.
#if defined(MESHPUTER_BOARD_TAB5)

#include <algorithm>
#include <cstdio>
#include <vector>

#include "board.h"
#include "chat_service.h"
#include "core/mesh_protocol.h"
#include "kit.h"
#include "meshtastic_client.h"
#include "views.h"

namespace Tab5 {

namespace {

constexpr uint32_t kRefreshMs = 1000;
constexpr int kProfileSection = 0;  // Settings > Profile

std::string hops(const MeshNode& n) {
    if (!n.hasHopsAway) return "";
    if (n.hopsAway == 0) return "direct";
    return std::to_string(n.hopsAway) + (n.hopsAway == 1 ? " hop" : " hops");
}

std::string fmt(const char* f, double v) {
    char buf[24];
    snprintf(buf, sizeof(buf), f, v);
    return buf;
}

// What a node row or page shows; a change means it must be redrawn.
std::string signature(const MeshNode& n) {
    return n.user.longName + "|" + n.user.shortName + "|" + std::to_string(n.lastHeard) + "|" +
           std::to_string(n.hopsAway) + "|" + fmt("%.1f", n.snr) + "|" + std::to_string(n.batteryLevel);
}

class NodesView : public View {
public:
    const char* name() const override { return "nodes"; }
    const char* detailName() const override { return "node"; }
    const char* label() const override { return "Nodes"; }
    const char* icon() const override { return LV_SYMBOL_GPS; }

    bool hasDetail() const override { return route().node != 0 && gMesh.node(route().node) != nullptr; }
    void buildList(Pane& pane) override;
    void buildDetail(Pane& pane) override;
    void buildPlaceholder(Pane& pane) override {
        Kit::emptyState(pane.body, LV_SYMBOL_GPS, "Pick a node", "See who it is, how far away, and message it.");
    }
    void update() override;
    void cleared(uint8_t parts) override {
        if (parts & kList) { _list = nullptr; _subtitle = nullptr; }
        if (parts & kDetail) _detailShown = false;
    }

private:
    void buildRows();
    static void onRow(lv_event_t* e);
    static void onMessage(lv_event_t*);
    static void onEdit(lv_event_t*) { openSection(kProfileSection); }

    std::vector<uint32_t> _nodes;
    lv_obj_t* _list = nullptr;
    lv_obj_t* _subtitle = nullptr;
    bool _detailShown = false;
    std::string _detailSig;
    std::string _listSig;
    uint32_t _seenMesh = 0;
    uint32_t _builtAt = 0;
};

NodesView sView;

// Everything the rows show; the list is redrawn only when this changes.
std::string listSignature() {
    std::string sig;
    for (const auto& n : gMesh.nodes()) sig += signature(n) + ago(n.lastHeard) + std::to_string(n.heardMs > 0) + ";";
    return sig;
}

void NodesView::buildList(Pane& pane) {
    pane.setTitle("Nodes");
    _subtitle = pane.subtitle;
    _list = Kit::scroller(pane.body);
    buildRows();
}

void NodesView::buildRows() {
    const Kit::Palette& p = Kit::pal();
    int32_t scroll = lv_obj_get_scroll_y(_list);
    lv_obj_clean(_list);
    _seenMesh = gMesh.revision();
    _builtAt = millis();
    _listSig = listSignature();

    uint32_t me = gMesh.myNodeNum();
    std::vector<const MeshNode*> list;
    for (const auto& n : gMesh.nodes()) list.push_back(&n);
    std::sort(list.begin(), list.end(), [me](const MeshNode* a, const MeshNode* b) {
        if ((a->num == me) != (b->num == me)) return a->num == me;  // ourselves first
        if (a->heardMs != b->heardMs) return a->heardMs > b->heardMs;  // heard this session
        return a->lastHeard > b->lastHeard;
    });
    _nodes.clear();
    for (const MeshNode* n : list) _nodes.push_back(n->num);

    int others = static_cast<int>(_nodes.size()) - (gMesh.node(me) ? 1 : 0);
    std::string sub = others == 1 ? "1 other node" : std::to_string(others < 0 ? 0 : others) + " other nodes";
    lv_label_set_text(_subtitle, sub.c_str());
    lv_obj_remove_flag(_subtitle, LV_OBJ_FLAG_HIDDEN);

    if (_nodes.empty()) {
        Kit::emptyState(_list, LV_SYMBOL_GPS, gMesh.ready() ? "No nodes heard yet" : "No radio connected",
                        gMesh.ready() ? "Nodes appear as your radio hears them." : "Connect a radio in Radio.");
        return;
    }
    for (size_t i = 0; i < _nodes.size(); ++i) {
        const MeshNode* n = gMesh.node(_nodes[i]);
        if (!n) continue;
        bool selected = landscape() && route().node == n->num;
        lv_obj_t* row = Kit::listRow(_list, selected, onRow, reinterpret_cast<void*>(i));
        Kit::avatar(row, Board::screenText(gMesh.nodeShortName(n->num)), n->num, 72);
        lv_obj_t* words = Kit::column(row, 4);
        lv_obj_set_width(words, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(words, 1);
        lv_obj_t* top = Kit::hrow(words, 12);
        lv_obj_t* t = Kit::text(top, Board::screenText(gMesh.nodeName(n->num)), Kit::strong(), p.text);
        lv_obj_set_flex_grow(t, 1);
        Kit::oneLine(t);
        if (n->num == me) Kit::pill(top, "you", p.tint, p.accent);
        std::string sub = MeshProto::nodeIdString(n->num);
        if (n->num != me) {
            std::string h = hops(*n), heard = ago(n->lastHeard);
            if (!h.empty()) sub += ", " + h;
            if (!heard.empty()) sub += ", " + heard;
        }
        lv_obj_t* s = Kit::text(words, sub, Kit::small(), p.text2);
        lv_obj_set_width(s, lv_pct(100));
        Kit::oneLine(s);
    }
    lv_obj_update_layout(_list);
    lv_obj_scroll_to_y(_list, scroll, LV_ANIM_OFF);
}

void NodesView::onRow(lv_event_t* e) {
    size_t i = reinterpret_cast<size_t>(lv_event_get_user_data(e));
    if (i < sView._nodes.size()) openNode(sView._nodes[i]);
}

void NodesView::onMessage(lv_event_t*) {
    uint32_t num = route().node;
    if (num && num != gMesh.myNodeNum()) openConversation(Chat::openDirect(num));
}

void NodesView::buildDetail(Pane& pane) {
    const Kit::Palette& p = Kit::pal();
    const MeshNode* n = gMesh.node(route().node);
    if (!n) return;
    _detailShown = true;
    _detailSig = signature(*n);
    bool isMe = n->num == gMesh.myNodeNum();
    pane.setTitle(Board::screenText(gMesh.nodeName(n->num)));
    pane.setSubtitle(isMe ? "This radio" : MeshProto::nodeIdString(n->num));

    lv_obj_t* page = Kit::scroller(pane.body, 24);
    lv_obj_set_style_pad_all(page, Kit::kPad, 0);

    lv_obj_t* hero = Kit::hrow(page, 28);
    Kit::avatar(hero, Board::screenText(gMesh.nodeShortName(n->num)), n->num, 144);
    lv_obj_t* who = Kit::column(hero, 6);
    lv_obj_set_width(who, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(who, 1);
    lv_obj_t* name = Kit::para(who, Board::screenText(gMesh.nodeName(n->num)), Kit::title(), p.text);
    (void)name;
    std::string line = n->hasUser ? "\"" + Board::screenText(n->user.shortName) + "\"" : "No identity received yet";
    Kit::text(who, line, Kit::body(), p.text2);
    if (!isMe) {
        std::string h = hops(*n), heard = ago(n->lastHeard);
        std::string reach = h.empty() ? "" : h;
        if (!heard.empty()) reach += (reach.empty() ? "heard " : ", heard ") + heard;
        if (!reach.empty()) Kit::text(who, reach, Kit::body(), p.text2);
    }

    lv_obj_t* actions = Kit::hrow(page, 16);
    if (isMe) Kit::button(actions, "Edit name", Kit::Btn::Secondary, onEdit);
    else Kit::button(actions, LV_SYMBOL_ENVELOPE "  Message", Kit::Btn::Primary, onMessage);

    Kit::sectionLabel(page, "IDENTITY");
    lv_obj_t* id = Kit::card(page);
    lv_obj_set_style_pad_row(id, 4, 0);
    Kit::field(id, "Node ID", MeshProto::nodeIdString(n->num));
    if (n->hasUser) {
        Kit::field(id, "Long name", Board::screenText(n->user.longName));
        Kit::field(id, "Short name", Board::screenText(n->user.shortName));
        Kit::field(id, "Hardware", MeshProto::hwModelName(n->user.hwModel));
        Kit::field(id, "Role", MeshProto::roleName(n->user.role));
        Kit::field(id, "Encrypted DMs", n->user.hasPublicKey ? "yes (public key known)" : "channel key only");
        if (n->user.isLicensed) Kit::field(id, "Licensed", "ham operator");
    }

    if (!isMe) {
        Kit::sectionLabel(page, "SIGNAL");
        lv_obj_t* sig = Kit::card(page);
        lv_obj_set_style_pad_row(sig, 4, 0);
        Kit::field(sig, "Hops away", n->hasHopsAway ? std::to_string(n->hopsAway) : "unknown");
        Kit::field(sig, "Last heard", n->lastHeard ? ago(n->lastHeard) + " ago" : "unknown");
        if (n->snr != 0.0f) Kit::field(sig, "SNR", fmt("%.1f dB", n->snr));
        Kit::field(sig, "Channel", std::to_string(n->channel));
        if (n->viaMqtt) Kit::field(sig, "Path", "via MQTT (internet)");
    }
    if (n->hasBattery || n->hasVoltage) {
        Kit::sectionLabel(page, "POWER");
        lv_obj_t* pw = Kit::card(page);
        lv_obj_set_style_pad_row(pw, 4, 0);
        if (n->hasBattery) {
            Kit::field(pw, "Battery", n->batteryLevel > 100 ? "external power" : std::to_string(n->batteryLevel) + "%");
        }
        if (n->hasVoltage) Kit::field(pw, "Voltage", fmt("%.2f V", n->voltage));
    }
}

void NodesView::update() {
    if (gMesh.revision() == _seenMesh || millis() - _builtAt < kRefreshMs) return;
    _seenMesh = gMesh.revision();
    _builtAt = millis();
    if (_list && listSignature() != _listSig) buildRows();
    if (_detailShown) {
        const MeshNode* n = gMesh.node(route().node);
        if (!n || signature(*n) != _detailSig) invalidate(kDetail);
    }
}

} // namespace

View& nodesView() { return sView; }

} // namespace Tab5

#endif
