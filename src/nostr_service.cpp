#include "nostr_service.h"

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <time.h>

#include <deque>
#include <map>
#include <set>

#include "core/crypto.h"
#include "core/json.h"
#include "core/nostr.h"
#include "log.h"
#include "meshtastic_client.h"
#include "settings.h"

namespace {

// We publish both relays as our DM inbox (senders deliver to every listed
// relay) but hold only one TLS session at a time: two (~40 KB each) do not fit
// next to Bluetooth without PSRAM. After repeated failures we move to the next.
// relay.damus.io is not used: it requires NIP-42 auth to list gift wraps but
// rejects every AUTH ("relay needs serviceUrl to be configured").
const char* const kRelays[] = {"nos.lol", "relay.primal.net"};
constexpr int kFailoverAfter = 3;
constexpr int kRelayCount = sizeof(kRelays) / sizeof(kRelays[0]);
constexpr int64_t kHistorySeconds = 3 * 24 * 3600;  // gift wraps are backdated up to 2 days
constexpr uint32_t kSendTimeoutMs = 20000;
constexpr size_t kMaxSeen = 128;
constexpr size_t kMaxNames = 64;

struct Relay {
    WebSocketsClient ws;
    bool online = false;
    bool published = false;
    int failures = 0;      // consecutive connection failures
    std::string challenge; // NIP-42 AUTH challenge from the relay
    std::string authId;    // our AUTH event id, until the relay accepts it
    bool authed = false;
    uint32_t frames = 0;   // diagnostics for `nostr` in the console
    uint32_t events = 0;
    uint32_t dms = 0;
    std::string lastNotice;
};

struct Pending {
    uint32_t ref = 0;
    uint32_t sentMs = 0;
    int asked = 0;
    int rejected = 0;
    bool reported = false;
};

Relay sRelays[kRelayCount];
bool sEnabled = false;
bool sRelaysStarted = false;
int sActive = 0;
bool sTimeStarted = false;
Nostr::Keys sKeys;
std::string sPubHex;
std::string sStatus = "Internet off";
std::map<std::string, Pending> sPending;   // by gift-wrap event id
std::deque<std::string> sSeen;             // recent rumor ids (relays repeat events)
std::map<std::string, std::string> sNames; // pubkey hex -> profile name
std::set<std::string> sAskedNames;
std::deque<NostrService::Incoming> sIncoming;

int64_t now() { return static_cast<int64_t>(time(nullptr)); }

NostrService::Incoming deliveryReport(NostrService::Incoming::Kind kind, uint32_t ref) {
    NostrService::Incoming in;
    in.kind = kind;
    in.ref = ref;
    return in;
}
bool clockReady() { return now() > 1700000000; }

uint32_t refFromId(const std::string& idHex) {
    uint8_t b[4] = {0};
    Crypto::fromHex(idHex.substr(0, 8), b, 4);
    uint32_t v = uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
    return v == 0 ? 1 : v;
}

bool alreadySeen(const std::string& id) {
    for (const auto& s : sSeen) if (s == id) return true;
    sSeen.push_back(id);
    if (sSeen.size() > kMaxSeen) sSeen.pop_front();
    return false;
}

void sendTo(Relay& r, const std::string& text) {
    std::string copy = text;  // sendTXT takes a mutable buffer
    r.ws.sendTXT(reinterpret_cast<uint8_t*>(&copy[0]), copy.size());
}

void publishTo(Relay& r) {
    // kind 0: profile, named after the paired radio and linking to its Meshtastic node.
    uint32_t node = gMesh.myNodeNum();
    const MeshNode* me = gMesh.node(node);
    Nostr::Event profile;
    profile.kind = 0;
    profile.createdAt = now();
    std::string name = me && me->hasUser && !me->user.longName.empty() ? me->user.longName : "Meshputer";
    std::string about = node ? "Meshtastic " + MeshProto::nodeIdString(node) + " via Meshputer (M5Stack Cardputer)"
                             : "Meshputer (M5Stack Cardputer)";
    profile.content = "{\"name\":" + Json::quote(name) + ",\"about\":" + Json::quote(about) + "}";
    if (node) {
        profile.tags.push_back({"meshtastic", MeshProto::nodeIdString(node),
                                me ? me->user.publicKeyHex : std::string()});
    }
    // kind 10050: where to deliver NIP-17 DMs to us.
    Nostr::Event inbox;
    inbox.kind = 10050;
    inbox.createdAt = profile.createdAt;
    for (const char* host : kRelays) inbox.tags.push_back({"relay", std::string("wss://") + host + "/"});

    if (Nostr::sign(profile, sKeys)) sendTo(r, "[\"EVENT\"," + Nostr::toJson(profile) + "]");
    if (Nostr::sign(inbox, sKeys)) sendTo(r, "[\"EVENT\"," + Nostr::toJson(inbox) + "]");
    r.published = true;
    LOG("NOSTR", "published profile '%s'", name.c_str());
}

void subscribeDms(Relay& r) {
    std::string req = "[\"REQ\",\"dm\",{\"kinds\":[1059],\"#p\":[" + Json::quote(sPubHex) +
                      "],\"since\":" + std::to_string(now() - kHistorySeconds) + "}]";
    sendTo(r, req);
}

// NIP-42: relays may only list gift wraps to their authenticated recipient.
void authenticate(Relay& r, int index) {
    if (r.challenge.empty()) return;
    Nostr::Event auth;
    auth.kind = 22242;
    auth.createdAt = now();
    auth.tags = {{"relay", std::string("wss://") + kRelays[index]}, {"challenge", r.challenge}};
    if (!Nostr::sign(auth, sKeys)) return;
    r.authId = auth.id;
    sendTo(r, "[\"AUTH\"," + Nostr::toJson(auth) + "]");
    LOG("NOSTR", "authenticating to %s", kRelays[index]);
}

void requestName(const std::string& pubHex) {
    if (sNames.count(pubHex) || sAskedNames.count(pubHex)) return;
    sAskedNames.insert(pubHex);
    std::string req = "[\"REQ\",\"pf" + pubHex.substr(0, 8) + "\",{\"kinds\":[0],\"authors\":[" +
                      Json::quote(pubHex) + "],\"limit\":1}]";
    for (auto& r : sRelays) if (r.online) sendTo(r, req);
}

void onEvent(const std::string& sub, const Json::Value& raw) {
    Nostr::Event e;
    if (!Nostr::fromJson(raw, e)) return;

    if (sub == "dm" && e.kind == Nostr::kKindGiftWrap) {
        ++sRelays[sActive].dms;
        Nostr::DirectMessage dm;
        if (!Nostr::unwrap(sKeys, e, dm)) {
            LOG("NOSTR", "could not open gift wrap %.8s", e.id.c_str());
            return;
        }
        if (alreadySeen(dm.rumorId)) return;
        NostrService::Incoming in;
        in.kind = NostrService::Incoming::Kind::Message;
        in.fromHex = dm.fromHex;
        in.text = dm.text;
        in.createdAt = dm.createdAt;
        in.ref = refFromId(dm.rumorId);
        sIncoming.push_back(in);
        LOG("NOSTR", "dm from %.8s (%u bytes)", dm.fromHex.c_str(), static_cast<unsigned>(dm.text.size()));
        requestName(dm.fromHex);
        return;
    }

    if (sub.compare(0, 2, "pf") == 0 && e.kind == 0 && Nostr::verify(e)) {
        Json::Value meta;
        if (!Json::parse(e.content, meta)) return;
        std::string name;
        if (const Json::Value* v = meta.get("display_name")) if (v->isString()) name = v->str;
        if (name.empty()) if (const Json::Value* v = meta.get("name")) if (v->isString()) name = v->str;
        if (!name.empty() && sNames.size() < kMaxNames) sNames[e.pubkey] = name;
    }
}

void onText(Relay& r, const char* data, size_t len) {
    Json::Value msg;
    if (!Json::parse(std::string(data, len), msg) || !msg.isArray() || msg.items.empty()) return;
    const std::string& type = msg.items[0].str;
    if (type != "EVENT" && type != "OK") LOG("NOSTR", "<< %.100s", data);

    if (type == "EVENT" && msg.items.size() >= 3) {
        ++r.events;
        onEvent(msg.items[1].str, msg.items[2]);
    } else if (type == "AUTH" && msg.items.size() >= 2 && msg.items[1].isString()) {
        r.challenge = msg.items[1].str;
        authenticate(r, sActive);
    } else if (type == "OK" && msg.items.size() >= 3 && !r.authId.empty() && msg.items[1].str == r.authId) {
        r.authed = msg.items[2].type == Json::Value::Type::Bool && msg.items[2].boolean;
        r.authId.clear();
        LOG("NOSTR", "auth %s", r.authed ? "accepted" : "rejected");
        if (r.authed) subscribeDms(r);  // retry the inbox subscription, now allowed
        else r.failures = kFailoverAfter;  // can't read our inbox here: try the next relay
    } else if (type == "OK" && msg.items.size() >= 3) {
        auto it = sPending.find(msg.items[1].str);
        if (it == sPending.end()) return;
        Pending& p = it->second;
        bool accepted = msg.items[2].type == Json::Value::Type::Bool && msg.items[2].boolean;
        if (accepted && !p.reported) {
            p.reported = true;
            sIncoming.push_back(deliveryReport(NostrService::Incoming::Kind::Accepted, p.ref));
        } else if (!accepted) {
            ++p.rejected;
            if (msg.items.size() >= 4) LOG("NOSTR", "relay rejected: %s", msg.items[3].str.c_str());
        }
    } else if (type == "EOSE" && msg.items.size() >= 2) {
        const std::string& sub = msg.items[1].str;
        if (sub.compare(0, 2, "pf") == 0) sendTo(r, "[\"CLOSE\"," + Json::quote(sub) + "]");
    } else if ((type == "NOTICE" || type == "CLOSED") && msg.items.size() >= 2) {
        r.lastNotice = type + " " + msg.items.back().str;
        bool authNeeded = type == "CLOSED" && msg.items.back().str.compare(0, 13, "auth-required") == 0;
        if (authNeeded && !r.authed && r.authId.empty()) authenticate(r, sActive);
        LOG("NOSTR", "%s %s", type.c_str(), msg.items.back().str.c_str());
    }
}

void startRelay(int i) {
    Relay& r = sRelays[i];
    r.ws.onEvent([i](WStype_t type, uint8_t* payload, size_t length) {
        Relay& relay = sRelays[i];
        switch (type) {
        case WStype_CONNECTED: {
            relay.online = true;
            relay.failures = 0;
            relay.authed = false;
            relay.authId.clear();
            LOG("NOSTR", "relay %s connected", kRelays[i]);
            subscribeDms(relay);
            if (!relay.published) publishTo(relay);
            break;
        }
        case WStype_DISCONNECTED:
        case WStype_ERROR:
            if (relay.online) LOG("NOSTR", "relay %s disconnected", kRelays[i]);
            else ++relay.failures;
            relay.online = false;
            break;
        case WStype_TEXT:
            ++relay.frames;
            onText(relay, reinterpret_cast<const char*>(payload), length);
            break;
        default:
            break;
        }
    });
    // No CA bundle yet: TLS is unauthenticated. Message contents are end-to-end
    // encrypted and signed regardless; a MITM could only drop or observe metadata.
    r.ws.beginSSL(kRelays[i], 443, "/", "", "");
    r.ws.setReconnectInterval(15000);
    r.ws.enableHeartbeat(45000, 15000, 2);
    LOG("NOSTR", "connecting to %s", kRelays[i]);
}

void startRelays() {
    startRelay(sActive);
    sRelaysStarted = true;
}

void failoverIfStuck() {
    Relay& r = sRelays[sActive];
    if (r.failures < kFailoverAfter) return;
    r.ws.disconnect();
    r.failures = 0;
    sActive = (sActive + 1) % kRelayCount;
    startRelay(sActive);
}

void stopAll() {
    for (auto& r : sRelays) {
        if (sRelaysStarted) r.ws.disconnect();
        r.online = false;
        r.published = false;
    }
    sRelaysStarted = false;
    sTimeStarted = false;
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);  // radio unpowered while Internet is off
}

void loadKeys() {
    Settings& s = Settings::get();
    uint8_t secret[32];
    if (s.nostrSecretHex.empty() || !Crypto::fromHex(s.nostrSecretHex, secret, 32) ||
        !Nostr::keysFromSecret(secret, sKeys)) {
        if (!Nostr::generateKeys(sKeys)) return;
        s.nostrSecretHex = Crypto::toHex(sKeys.secret, 32);
        s.save();
        LOG("NOSTR", "created a new identity");
    }
    sPubHex = sKeys.pubHex();
}

} // namespace

namespace NostrService {

void begin() {
    loadKeys();
    if (Settings::get().internetEnabled) setEnabled(true);
}

void setEnabled(bool on) {
    Settings& s = Settings::get();
    if (s.internetEnabled != on) {
        s.internetEnabled = on;
        s.save();
    }
    if (on == sEnabled) return;
    sEnabled = on;
    if (!on) {
        stopAll();
        sStatus = "Internet off";
        LOG("NOSTR", "off");
        return;
    }
    if (s.wifiSsid.empty()) {
        sStatus = "Set a Wi-Fi network first";
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(s.wifiSsid.c_str(), s.wifiPassword.c_str());
    sStatus = "Joining " + s.wifiSsid;
    LOG("NOSTR", "joining Wi-Fi '%s'", s.wifiSsid.c_str());
}

void loop() {
    if (!sEnabled || Settings::get().wifiSsid.empty()) return;

    if (WiFi.status() != WL_CONNECTED) {
        if (sRelaysStarted) for (auto& r : sRelays) r.online = false;
        sStatus = "Joining " + Settings::get().wifiSsid;
        return;
    }
    if (!sTimeStarted) {
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        sTimeStarted = true;
        sStatus = "Getting time";
    }
    if (!clockReady()) return;
    if (!sRelaysStarted) startRelays();

    sRelays[sActive].ws.loop();
    failoverIfStuck();

    int online = relaysOnline();
    sStatus = online ? std::string("Online via ") + kRelays[sActive] : "Connecting relays";

    uint32_t ms = millis();
    for (auto it = sPending.begin(); it != sPending.end();) {
        Pending& p = it->second;
        bool allRejected = p.rejected >= p.asked;
        if (!p.reported && (allRejected || ms - p.sentMs > kSendTimeoutMs)) {
            sIncoming.push_back(deliveryReport(Incoming::Kind::Rejected, p.ref));
            p.reported = true;
        }
        if (p.reported && ms - p.sentMs > kSendTimeoutMs) it = sPending.erase(it);
        else ++it;
    }
}

State state() {
    if (!sEnabled) return State::Off;
    return relaysOnline() > 0 ? State::Online : State::Connecting;
}

const std::string& status() { return sStatus; }

int relaysOnline() {
    int n = 0;
    for (const auto& r : sRelays) if (r.online) ++n;
    return n;
}

const std::string& pubHex() { return sPubHex; }
std::string npub() { return sPubHex.empty() ? "" : Nostr::npub(sKeys.pub); }

uint32_t sendDm(const std::string& toHex, const std::string& text, std::string& error) {
    uint8_t to[32];
    if (!Crypto::fromHex(toHex, to, 32)) { error = "Bad Nostr key"; return 0; }
    if (state() != State::Online) { error = "Internet offline"; return 0; }
    Nostr::Event wrap;
    if (!Nostr::giftWrap(sKeys, to, text, now(), wrap)) { error = "Encryption failed"; return 0; }
    std::string frame = "[\"EVENT\"," + Nostr::toJson(wrap) + "]";
    Pending p;
    p.ref = refFromId(wrap.id);
    p.sentMs = millis();
    for (auto& r : sRelays) {
        if (!r.online) continue;
        sendTo(r, frame);
        ++p.asked;
    }
    sPending[wrap.id] = p;
    LOG("NOSTR", "sent dm %.8s to %.8s via %d relays", wrap.id.c_str(), toHex.c_str(), p.asked);
    requestName(toHex);
    return p.ref;
}

void publishProfile() {
    for (auto& r : sRelays) {
        if (r.online) publishTo(r);
        else r.published = false;
    }
}

std::string displayName(const std::string& pubHex) {
    auto it = sNames.find(pubHex);
    if (it != sNames.end()) return it->second;
    uint8_t pub[32];
    if (!Crypto::fromHex(pubHex, pub, 32)) return pubHex.substr(0, 8);
    std::string n = Nostr::npub(pub);
    return n.substr(0, 10) + ".." + n.substr(n.size() - 4);
}

std::string diagnostics() {
    std::string out;
    for (int i = 0; i < kRelayCount; ++i) {
        const Relay& r = sRelays[i];
        out += std::string(kRelays[i]) + (i == sActive ? " (active)" : "") + " online=" + (r.online ? "1" : "0") +
               " auth=" + (r.authed ? "1" : "0") + " failures=" + std::to_string(r.failures) + " frames=" + std::to_string(r.frames) +
               " events=" + std::to_string(r.events) + " dms=" + std::to_string(r.dms) +
               (r.lastNotice.empty() ? "" : " last=\"" + r.lastNotice + "\"") + "\n";
    }
    return out;
}

bool popIncoming(Incoming& out) {
    if (sIncoming.empty()) return false;
    out = sIncoming.front();
    sIncoming.pop_front();
    return true;
}

} // namespace NostrService
