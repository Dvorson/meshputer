#include "chat_service.h"

#include <Arduino.h>

#include "board.h"
#include "core/text_util.h"
#include "log.h"
#include "meshtastic_client.h"
#include "nostr_service.h"
#include "power.h"
#include "settings.h"
#include "sound.h"

namespace {

ConversationStore sStore;
bool sDemo = false;  // demo data must never reach the SD card
int sActive = -1;
uint32_t sLoadedNode = 0;
std::string sToast;
uint32_t sToastAt = 0;

std::string nodeDir(uint32_t node) {
    char buf[32];
    snprintf(buf, sizeof(buf), "/meshputer/%08lx", static_cast<unsigned long>(node));
    return buf;
}

void persist(int idx) {
    const Conversation* c = sStore.at(idx);
    if (!c || sDemo || sLoadedNode == 0 || !Board::sdReady()) return;
    std::string dir = nodeDir(sLoadedNode);
    if (!Board::ensureDir(dir.c_str())) return;
    std::string path = dir + "/" + ConversationStore::fileName(c->key);
    if (!Board::writeFile(path.c_str(), ConversationStore::serialize(*c))) {
        LOG("CHAT", "could not save %s", path.c_str());
    }
}

// History is kept per radio node: a different radio has different channels
// and a different identity, so its conversations live in their own folder.
void loadHistory(uint32_t node) {
    sStore.clear();
    sActive = -1;
    sLoadedNode = node;
    if (node == 0 || !Board::sdReady()) return;
    std::string dir = nodeDir(node);
    std::vector<std::string> names;
    if (!Board::listDir(dir.c_str(), names)) return;
    int loaded = 0;
    for (const auto& name : names) {
        if (name.size() < 5 || name.compare(name.size() - 4, 4, ".txt") != 0) continue;
        std::string data;
        if (!Board::readFile((dir + "/" + name).c_str(), data)) continue;
        Conversation conv;
        if (ConversationStore::parse(data, conv)) {
            sStore.adopt(conv);
            ++loaded;
        }
    }
    LOG("CHAT", "loaded %d conversations for %08lx", loaded, static_cast<unsigned long>(node));
}

void setToast(const std::string& text) {
    sToast = text;
    sToastAt = millis();
}

const char* routingReason(int error) {
    switch (error) {
    case MeshProto::kRoutingNoRoute:          return "no route";
    case MeshProto::kRoutingGotNak:           return "rejected";
    case MeshProto::kRoutingTimeout:
    case MeshProto::kRoutingMaxRetransmit:    return "no ack";
    case MeshProto::kRoutingNoChannel:        return "no channel";
    case MeshProto::kRoutingTooLarge:         return "too large";
    case MeshProto::kRoutingNoResponse:       return "no response";
    case MeshProto::kRoutingPkiFailed:
    case MeshProto::kRoutingPkiUnknownPubkey: return "no key for node";
    default:                                  return "error";
    }
}

void onText(const MeshProto::TextMessage& t) {
    uint32_t me = gMesh.myNodeNum();
    if (t.from == me) return;  // our own echo
    ConversationKey key;
    if (t.to == MeshProto::kBroadcast) key = ConversationKey::onChannel(t.channel);
    else if (t.to == me) key = ConversationKey::direct(t.from);
    else return;  // not for us

    int idx = sStore.find(key);
    bool onScreen = idx >= 0 && idx == sActive;
    idx = sStore.addIncoming(key, t.from, t.text, t.id, t.rxTime, !onScreen);
    if (idx < 0) return;  // duplicate
    persist(idx);

    std::string who = TextUtil::toDisplay(gMesh.nodeShortName(t.from));
    std::string text = TextUtil::toDisplay(t.text);
    for (char& c : text) if (c == '\n') c = ' ';
    setToast(who + ": " + text);
    Sound::message();
    Power::wakeForMessage();
}

void onRouting(const MeshProto::RoutingReport& r) {
    Delivery next;
    if (r.error != MeshProto::kRoutingNone) next = Delivery::Failed;
    else if (r.from == gMesh.myNodeNum()) next = Delivery::Relayed;  // implicit ack: mesh repeated it
    else next = Delivery::Delivered;                                  // ack from the destination

    int idx = sStore.updateDelivery(r.requestId, next);
    if (idx < 0) return;
    persist(idx);
    if (next == Delivery::Failed) {
        setToast(std::string("Not delivered: ") + routingReason(r.error));
        Sound::error();
    }
}

} // namespace

namespace Chat {

void begin() {
    loadHistory(Settings::get().lastNodeNum);
}

void loop() {
    // A radio with a different node number was connected: switch history.
    uint32_t node = gMesh.myNodeNum();
    if (node != 0 && node != sLoadedNode) loadHistory(node);

    MeshProto::Event ev;
    while (gMesh.popEvent(ev)) {
        switch (ev.kind) {
        case MeshProto::EventKind::Text:
            onText(ev.text);
            break;
        case MeshProto::EventKind::Routing:
            onRouting(ev.routing);
            break;
        case MeshProto::EventKind::QueueStatus: {
            int idx = sStore.updateDelivery(ev.queue.packetId, Delivery::Failed);
            if (idx >= 0) {
                persist(idx);
                setToast("Radio queue full");
            }
            break;
        }
        case MeshProto::EventKind::Notification:
            setToast(TextUtil::toDisplay(ev.message));
            break;
        default:
            break;
        }
    }

    NostrService::Incoming in;
    while (NostrService::popIncoming(in)) {
        if (in.kind == NostrService::Incoming::Kind::Message) {
            ConversationKey key = ConversationKey::nostrDirect(in.fromHex);
            int idx = sStore.find(key);
            bool onScreen = idx >= 0 && idx == sActive;
            idx = sStore.addIncoming(key, 0, in.text, in.ref, static_cast<uint32_t>(in.createdAt), !onScreen);
            if (idx < 0) continue;
            persist(idx);
            setToast("@" + TextUtil::toDisplay(NostrService::displayName(in.fromHex)) + ": " +
                     TextUtil::toDisplay(in.text));
            Sound::message();
            Power::wakeForMessage();
        } else {
            // Over the internet "relayed" means at least one relay stored it for the recipient.
            Delivery d = in.kind == NostrService::Incoming::Kind::Accepted ? Delivery::Relayed : Delivery::Failed;
            int idx = sStore.updateDelivery(in.ref, d);
            if (idx >= 0) persist(idx);
            if (idx >= 0 && d == Delivery::Failed) setToast("Not delivered: no relay accepted it");
        }
    }
}

ConversationStore& store() { return sStore; }

int openDirect(uint32_t node) { return sStore.ensure(ConversationKey::direct(node)); }
int openChannel(uint8_t channel) { return sStore.ensure(ConversationKey::onChannel(channel)); }
int openNostr(const std::string& pubHex) { return sStore.ensure(ConversationKey::nostrDirect(pubHex)); }

void setActive(int convIdx) {
    sActive = convIdx;
    if (convIdx >= 0 && sStore.at(convIdx) && sStore.at(convIdx)->unread) {
        sStore.markRead(convIdx);
        persist(convIdx);
    }
}

std::string send(int convIdx, const std::string& utf8) {
    const Conversation* conv = sStore.at(convIdx);
    if (!conv) return "No conversation";
    if (sDemo) return "Demo data loaded - reboot to chat";
    if (utf8.empty()) return "Nothing to send";
    if (utf8.size() > MeshProto::kMaxTextBytes) return "Too long";

    if (conv->key.isNostr()) {
        std::string err;
        uint32_t ref = NostrService::sendDm(conv->key.nostr, utf8, err);
        if (ref == 0) {
            Sound::error();
            return err;
        }
        ConversationKey key = conv->key;
        int idx = sStore.addOutgoing(key, 0, utf8, ref, 0);
        persist(idx);
        Sound::sent();
        return "";
    }

    if (!gMesh.ready()) return "Radio not connected";

    uint32_t to;
    uint8_t channel;
    if (conv->key.isChannel) {
        to = MeshProto::kBroadcast;
        channel = conv->key.channel;
    } else {
        to = conv->key.peer;
        // Legacy (non-PKI) DMs must go out on a channel the peer shares with us.
        const MeshNode* n = gMesh.node(to);
        channel = n ? n->channel : 0;
    }
    uint32_t id = gMesh.sendText(to, channel, utf8);
    if (id == 0) {
        Sound::error();
        return "Radio did not accept it";
    }
    ConversationKey key = conv->key;
    int idx = sStore.addOutgoing(key, gMesh.myNodeNum(), utf8, id, 0);
    persist(idx);
    Sound::sent();
    return "";
}

std::string title(const Conversation& conv) {
    if (conv.key.isNostr()) return "@" + TextUtil::toDisplay(NostrService::displayName(conv.key.nostr));
    if (conv.key.isChannel) return "#" + TextUtil::toDisplay(gMesh.channelName(conv.key.channel));
    return TextUtil::toDisplay(gMesh.nodeName(conv.key.peer));
}

std::string senderLabel(const Conversation& conv, const ChatMessage& msg) {
    if (msg.fromMe) return "me";
    if (conv.key.isNostr()) return TextUtil::toDisplay(NostrService::displayName(conv.key.nostr));
    return TextUtil::toDisplay(gMesh.nodeShortName(msg.from));
}

std::string preview(const Conversation& conv, size_t width) {
    if (conv.messages.empty()) return "";
    const ChatMessage& m = conv.messages.back();
    std::string text = TextUtil::toDisplay(m.text);
    for (char& c : text) if (c == '\n') c = ' ';
    std::string who = senderLabel(conv, m);
    return TextUtil::ellipsize(who + ": " + text, width);
}

void loadDemo() {
    sDemo = true;
    sStore.clear();
    sActive = -1;
    sToast.clear();
    uint32_t now = gMesh.epochNow();
    uint32_t me = gMesh.myNodeNum();
    const uint32_t ana = 0x1a2b3c4d, sam = 0x3141a5b9, relay = 0x5e6f7a8b, base = 0x9c0d1e2f;

    auto dm = ConversationKey::direct(ana);
    sStore.addIncoming(dm, ana, "Made it to the ridge. Signal is great up here \xF0\x9F\x91\x8D", 11, now - 900, false);
    sStore.addOutgoing(dm, me, "Nice! On my way, 20 min", 12, now - 840);
    sStore.updateDelivery(12, Delivery::Delivered);
    sStore.addIncoming(dm, ana, "Bring water, the spring is dry", 13, now - 300, false);
    sStore.addOutgoing(dm, me, "Got 2L. See you at the trailhead", 14, now - 240);
    sStore.updateDelivery(14, Delivery::Delivered);
    sStore.addIncoming(dm, ana, "Waiting by the sign \xE2\x9B\xB0", 15, now - 60, false);

    auto bike = ConversationKey::direct(sam);
    sStore.addOutgoing(bike, me, "Lunch at the hut?", 21, now - 7300);
    sStore.updateDelivery(21, Delivery::Relayed);
    sStore.addIncoming(bike, sam, "yes, 12:30", 22, now - 7200, false);

    auto mesh = ConversationKey::onChannel(0);
    sStore.addIncoming(mesh, base, "Net check at 19:00, all welcome", 31, now - 3600, true);
    sStore.addIncoming(mesh, relay, "Relay back online, solar at 80%", 32, now - 600, true);
}

const std::string& toast() { return sToast; }
uint32_t toastAtMs() { return sToastAt; }

} // namespace Chat
