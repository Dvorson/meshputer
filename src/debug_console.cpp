#include "debug_console.h"

#include <Arduino.h>
#include <cstdlib>
#include <deque>
#include <string>

#include "chat_service.h"
#include "core/text_util.h"
#include "log.h"
#include "core/crypto.h"
#include "core/nostr.h"
#include "meshtastic_client.h"
#include "nostr_service.h"
#include "power.h"
#include "screens/screens.h"
#include "settings.h"

namespace {

std::string sLine;
std::deque<KeyInput> sKeys;

void done() { Serial.println("## done"); }
void fail(const char* why) { Serial.printf("## error %s\n", why); }

std::string nextWord(std::string& rest) {
    size_t start = rest.find_first_not_of(' ');
    if (start == std::string::npos) { rest.clear(); return ""; }
    size_t end = rest.find(' ', start);
    std::string word = rest.substr(start, end == std::string::npos ? std::string::npos : end - start);
    rest = end == std::string::npos ? "" : rest.substr(end + 1);
    return word;
}

bool parseNode(const std::string& s, uint32_t& out) {
    std::string hex = (!s.empty() && s[0] == '!') ? s.substr(1) : s;
    if (hex.empty() || hex.size() > 8) return false;
    char* end = nullptr;
    out = static_cast<uint32_t>(strtoul(hex.c_str(), &end, 16));
    return end && *end == '\0';
}

void printHelp() {
    Serial.println(
        "status                     link, radio, node, memory, power\n"
        "logs on|off                tagged log lines\n"
        "scan / radios              scan for Meshtastic radios / list results\n"
        "connect <n>|saved          connect to scan result n, or the saved radio\n"
        "pin <digits>               answer a pairing request\n"
        "disconnect / forget        drop the link / also forget the pairing\n"
        "nodes / node <!id>         node database / one node's identity\n"
        "channels / convs           channels / conversations\n"
        "msgs <conv>                messages of a conversation\n"
        "send <!id|chN> <text>      send text (DM or channel)\n"
        "owner <short> <long name>  set this radio's identity\n"
        "key <text>|enter|del|esc|up|down|left|right|fn+<c>   inject keys\n"
        "screen / shot              current screen / dump the frame (RGB565 hex)\n"
        "open home|nodes|channels|settings|radio|chat <n>|node <!id>   jump to a screen\n"
        "demo                       fictional nodes + chats for screenshots (RAM only)\n"
        "internet on|off            Wi-Fi + Nostr relays\n"
        "wifi <ssid> [password]     set the Wi-Fi network (stored on the device)\n"
        "nostr                      identity and relay status\n"
        "nostr dm <npub|hex> <text> private message over the internet (NIP-17)\n"
        "nostr publish              re-publish profile and DM relay list\n"
        "reboot");
}

void printStatus() {
    uint32_t me = gMesh.myNodeNum();
    Serial.printf("link=%s status=\"%s\"\n", linkStateLabel(gMesh.state()), gMesh.status().c_str());
    Serial.printf("radio=\"%s\" addr=%s saved=%d reconnect_in=%lu\n", gMesh.radio().name.c_str(),
                  gMesh.radio().addr.c_str(), gMesh.hasSavedRadio(), static_cast<unsigned long>(gMesh.reconnectInMs()));
    Serial.printf("me=%s long=\"%s\" short=\"%s\" nodes=%u channels=%u\n", MeshProto::nodeIdString(me).c_str(),
                  gMesh.nodeName(me).c_str(), gMesh.nodeShortName(me).c_str(),
                  static_cast<unsigned>(gMesh.nodes().size()), static_cast<unsigned>(gMesh.channels().size()));
    Serial.printf("owner_update=%d \"%s\"\n", static_cast<int>(gMesh.ownerUpdate()), gMesh.ownerMessage().c_str());
    Serial.printf("convs=%u unread=%d screen=%s\n", static_cast<unsigned>(Chat::store().size()),
                  Chat::store().unreadCount(), Nav::current() ? Nav::current()->name() : "-");
    Serial.printf("heap=%lu min_heap=%lu cpu=%luMHz screen_on=%d battery=%d sd=%d\n",
                  static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
                  static_cast<unsigned long>(getCpuFrequencyMhz()), Power::screenOn(), Board::batteryPercent(),
                  Board::sdReady());
}

void printNodes() {
    uint32_t me = gMesh.myNodeNum();
    for (const auto& n : gMesh.nodes()) {
        Serial.printf("%s%s short=\"%s\" long=\"%s\" hops=%d snr=%.1f heard=%lu pki=%d\n",
                      n.num == me ? "*" : " ", MeshProto::nodeIdString(n.num).c_str(), n.user.shortName.c_str(),
                      n.user.longName.c_str(), n.hasHopsAway ? n.hopsAway : -1, n.snr,
                      static_cast<unsigned long>(n.lastHeard), n.user.hasPublicKey);
    }
}

void printNode(uint32_t num) {
    const MeshNode* n = gMesh.node(num);
    if (!n) { fail("unknown node"); return; }
    Serial.printf("id=%s user_id=%s long=\"%s\" short=\"%s\"\n", MeshProto::nodeIdString(num).c_str(),
                  n->user.id.c_str(), n->user.longName.c_str(), n->user.shortName.c_str());
    Serial.printf("hw=%s role=%s pki=%d licensed=%d channel=%u\n", MeshProto::hwModelName(n->user.hwModel),
                  MeshProto::roleName(n->user.role), n->user.hasPublicKey, n->user.isLicensed, n->channel);
    Serial.printf("hops=%d snr=%.1f heard=%lu battery=%lu mqtt=%d\n", n->hasHopsAway ? n->hopsAway : -1, n->snr,
                  static_cast<unsigned long>(n->lastHeard), static_cast<unsigned long>(n->batteryLevel), n->viaMqtt);
    done();
}

void printConvs() {
    const ConversationStore& s = Chat::store();
    for (size_t i = 0; i < s.size(); ++i) {
        const Conversation* c = s.at(static_cast<int>(i));
        std::string where = c->key.isNostr() ? "nostr " + c->key.nostr.substr(0, 16)
                          : c->key.isChannel ? "ch" + std::to_string(c->key.channel)
                          : "dm" + MeshProto::nodeIdString(c->key.peer);
        Serial.printf("%u %s msgs=%u unread=%d title=\"%s\"\n", static_cast<unsigned>(i), where.c_str(),
                      static_cast<unsigned>(c->messages.size()), c->unread, Chat::title(*c).c_str());
    }
}

void printMsgs(int idx) {
    const Conversation* c = Chat::store().at(idx);
    if (!c) { fail("no such conversation"); return; }
    for (const auto& m : c->messages) {
        Serial.printf("%s %s id=%08lx state=%s \"%s\"\n", m.fromMe ? ">" : "<",
                      MeshProto::nodeIdString(m.from).c_str(), static_cast<unsigned long>(m.packetId),
                      m.fromMe ? deliveryLabel(m.delivery) : "rx", m.text.c_str());
    }
    done();
}

void injectKey(const std::string& spec) {
    KeyInput k;
    k.any = true;
    if (spec == "enter") k.enter = true;
    else if (spec == "del") k.del = true;
    else if (spec == "esc") k.chars.push_back('`');
    else if (spec == "up") k.chars.push_back(';');
    else if (spec == "down") k.chars.push_back('.');
    else if (spec == "left") k.chars.push_back(',');
    else if (spec == "right") k.chars.push_back('/');
    else if (spec.size() == 4 && spec.compare(0, 3, "fn+") == 0) { k.fn = true; k.chars.push_back(spec[3]); }
    else {
        // Literal text: one key event per character, like typing.
        for (char ch : spec) {
            KeyInput t;
            t.any = true;
            t.chars.push_back(ch);
            sKeys.push_back(t);
        }
        return;
    }
    sKeys.push_back(k);
}

void dumpFrame() {
    M5Canvas& c = Board::canvas();
    const uint8_t* buf = static_cast<const uint8_t*>(c.getBuffer());
    int w = c.width(), h = c.height();
    Serial.printf("SHOT %d %d rgb565be\n", w, h);
    static char line[240 * 4 + 2];
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = buf + static_cast<size_t>(y) * w * 2;
        static const char* kHex = "0123456789abcdef";
        int n = 0;
        for (int i = 0; i < w * 2; ++i) {
            line[n++] = kHex[row[i] >> 4];
            line[n++] = kHex[row[i] & 0xF];
        }
        line[n] = '\0';
        Serial.println(line);
    }
    Serial.println("SHOT END");
}

void run(const std::string& raw) {
    std::string rest = raw;
    std::string cmd = nextWord(rest);
    if (cmd.empty()) return;

    if (cmd == "help") { printHelp(); done(); }
    else if (cmd == "status") { printStatus(); done(); }
    else if (cmd == "logs") { Log::setEnabled(rest != "off"); done(); }
    else if (cmd == "scan") { gMesh.startScan(); done(); }
    else if (cmd == "radios") {
        auto results = gMesh.scanResults();
        for (size_t i = 0; i < results.size(); ++i) {
            Serial.printf("%u name=\"%s\" addr=%s type=%u rssi=%d\n", static_cast<unsigned>(i),
                          results[i].name.c_str(), results[i].addr.c_str(), results[i].addrType, results[i].rssi);
        }
        done();
    }
    else if (cmd == "connect") {
        std::string arg = nextWord(rest);
        if (arg == "saved") { gMesh.reconnectNow(); done(); return; }
        auto results = gMesh.scanResults();
        int n = atoi(arg.c_str());
        if (arg.empty() || n < 0 || n >= static_cast<int>(results.size())) { fail("bad index"); return; }
        gMesh.connect(results[n]);
        done();
    }
    else if (cmd == "pin") { gMesh.submitPin(static_cast<uint32_t>(strtoul(rest.c_str(), nullptr, 10))); done(); }
    else if (cmd == "disconnect") { gMesh.disconnect(); done(); }
    else if (cmd == "forget") { gMesh.forgetRadio(); done(); }
    else if (cmd == "nodes") { printNodes(); done(); }
    else if (cmd == "node") {
        uint32_t num;
        if (!parseNode(nextWord(rest), num)) { fail("bad node id"); return; }
        printNode(num);
    }
    else if (cmd == "channels") {
        for (const auto& ch : gMesh.channels()) {
            Serial.printf("ch%d role=%u name=\"%s\" psk_len=%u\n", ch.index, ch.role, ch.name.c_str(), ch.pskLen);
        }
        done();
    }
    else if (cmd == "convs") { printConvs(); done(); }
    else if (cmd == "msgs") { printMsgs(atoi(rest.c_str())); }
    else if (cmd == "send") {
        std::string target = nextWord(rest);
        int idx;
        if (target.size() > 2 && target.compare(0, 2, "ch") == 0) {
            idx = Chat::openChannel(static_cast<uint8_t>(atoi(target.c_str() + 2)));
        } else {
            uint32_t num;
            if (!parseNode(target, num)) { fail("bad target"); return; }
            idx = Chat::openDirect(num);
        }
        std::string err = Chat::send(idx, rest);
        if (err.empty()) {
            const Conversation* c = Chat::store().at(idx);
            Serial.printf("conv=%d id=%08lx\n", idx, static_cast<unsigned long>(c->messages.back().packetId));
            done();
        } else {
            fail(err.c_str());
        }
    }
    else if (cmd == "owner") {
        std::string shortName = nextWord(rest);
        if (shortName.empty() || rest.empty()) { fail("usage: owner <short> <long name>"); return; }
        if (gMesh.setOwner(rest, shortName)) done();
        else fail(gMesh.ownerMessage().c_str());
    }
    else if (cmd == "key") {
        if (rest.empty()) { fail("usage: key <text>|enter|..."); return; }
        injectKey(rest);
        done();
    }
    else if (cmd == "demo") {
        gMesh.loadDemoNodes();
        Chat::loadDemo();
        Nav::reset(&gHomeScreen);
        done();
    }
    else if (cmd == "open") {
        std::string what = nextWord(rest);
        if (what == "home") Nav::reset(&gHomeScreen);
        else if (what == "nodes") Nav::push(&gNodesScreen);
        else if (what == "channels") Nav::push(&gChannelsScreen);
        else if (what == "settings") Nav::push(&gSettingsScreen);
        else if (what == "radio") Nav::push(&gRadioScreen);
        else if (what == "chat") {
            int idx = atoi(rest.c_str());
            if (!Chat::store().at(idx)) { fail("no such conversation"); return; }
            openChat(idx);
        } else if (what == "node") {
            uint32_t num;
            if (!parseNode(nextWord(rest), num)) { fail("bad node id"); return; }
            gNodeInfoScreen.open(num);
            Nav::push(&gNodeInfoScreen);
        } else { fail("unknown screen"); return; }
        done();
    }
    else if (cmd == "internet") {
        NostrService::setEnabled(rest == "on");
        done();
    }
    else if (cmd == "wifi") {
        std::string ssid = nextWord(rest);
        if (ssid.empty()) { fail("usage: wifi <ssid> [password]"); return; }
        Settings& s = Settings::get();
        s.wifiSsid = ssid;
        s.wifiPassword = rest;
        s.save();
        if (s.internetEnabled) { NostrService::setEnabled(false); NostrService::setEnabled(true); }
        done();
    }
    else if (cmd == "nostr") {
        std::string sub = nextWord(rest);
        if (sub.empty()) {
            Serial.printf("npub=%s\nhex=%s\nstate=%d status=\"%s\" relays=%d time=%ld\n",
                          NostrService::npub().c_str(), NostrService::pubHex().c_str(),
                          static_cast<int>(NostrService::state()), NostrService::status().c_str(),
                          NostrService::relaysOnline(), static_cast<long>(time(nullptr)));
            Serial.print(NostrService::diagnostics().c_str());
            done();
        } else if (sub == "dm") {
            std::string target = nextWord(rest), hrp;
            uint8_t pub[32];
            if (!Nostr::decodeKey(target, hrp, pub) || (hrp != "npub" && hrp != "hex")) { fail("bad npub"); return; }
            int idx = Chat::openNostr(Crypto::toHex(pub, 32));
            std::string err = Chat::send(idx, rest);
            if (err.empty()) {
                Serial.printf("conv=%d ref=%08lx\n", idx,
                              static_cast<unsigned long>(Chat::store().at(idx)->messages.back().packetId));
                done();
            } else {
                fail(err.c_str());
            }
        } else if (sub == "publish") {
            NostrService::publishProfile();
            done();
        } else {
            fail("usage: nostr [dm <npub> <text> | publish]");
        }
    }
    else if (cmd == "screen") { Serial.println(Nav::current() ? Nav::current()->name() : "-"); done(); }
    else if (cmd == "shot") { dumpFrame(); done(); }
    else if (cmd == "reboot") { done(); Serial.flush(); ESP.restart(); }
    else fail("unknown command (try help)");
}

} // namespace

namespace DebugConsole {

void poll() {
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c == '\n') {
            std::string line = sLine;
            sLine.clear();
            run(line);
        } else if (sLine.size() < 400) {
            sLine += c;
        }
    }
}

bool popKey(KeyInput& out) {
    if (sKeys.empty()) return false;
    out = sKeys.front();
    sKeys.pop_front();
    return true;
}

} // namespace DebugConsole
