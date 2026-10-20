#include "core/conversations.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

const char* deliveryLabel(Delivery d) {
    switch (d) {
    case Delivery::Received:  return "";
    case Delivery::Queued:    return "sent";
    case Delivery::Relayed:   return "relayed";
    case Delivery::Delivered: return "delivered";
    case Delivery::Failed:    return "failed";
    }
    return "";
}

void ConversationStore::clear() {
    _convs.clear();
    _nextActivity = 1;
    ++_revision;
}

Conversation* ConversationStore::at(int index) {
    if (index < 0 || index >= static_cast<int>(_convs.size())) return nullptr;
    return &_convs[index];
}

const Conversation* ConversationStore::at(int index) const {
    if (index < 0 || index >= static_cast<int>(_convs.size())) return nullptr;
    return &_convs[index];
}

int ConversationStore::find(const ConversationKey& key) const {
    for (size_t i = 0; i < _convs.size(); ++i) {
        if (_convs[i].key == key) return static_cast<int>(i);
    }
    return -1;
}

int ConversationStore::ensure(const ConversationKey& key) {
    int idx = find(key);
    if (idx >= 0) return idx;
    Conversation c;
    c.key = key;
    _convs.push_back(c);
    ++_revision;
    return static_cast<int>(_convs.size()) - 1;
}

void ConversationStore::touch(Conversation& conv) {
    if (conv.messages.size() > kMaxMessagesPerConversation) {
        conv.messages.erase(conv.messages.begin(),
                            conv.messages.end() - kMaxMessagesPerConversation);
    }
    conv.lastActivity = _nextActivity++;
    ++_revision;
}

int ConversationStore::addIncoming(const ConversationKey& key, uint32_t from,
                                   const std::string& text, uint32_t packetId,
                                   uint32_t timestamp, bool markUnread) {
    int idx = ensure(key);
    Conversation& conv = _convs[idx];
    if (packetId != 0) {
        for (const auto& m : conv.messages) {
            if (!m.fromMe && m.from == from && m.packetId == packetId) return -1;
        }
    }
    ChatMessage m;
    m.fromMe = false;
    m.from = from;
    m.text = text;
    m.packetId = packetId;
    m.delivery = Delivery::Received;
    m.timestamp = timestamp;
    conv.messages.push_back(m);
    if (markUnread) conv.unread = true;
    touch(conv);
    return idx;
}

int ConversationStore::addOutgoing(const ConversationKey& key, uint32_t from,
                                   const std::string& text, uint32_t packetId,
                                   uint32_t timestamp) {
    int idx = ensure(key);
    Conversation& conv = _convs[idx];
    ChatMessage m;
    m.fromMe = true;
    m.from = from;
    m.text = text;
    m.packetId = packetId;
    m.delivery = Delivery::Queued;
    m.timestamp = timestamp;
    conv.messages.push_back(m);
    touch(conv);
    return idx;
}

static bool deliveryAdvances(Delivery current, Delivery next) {
    switch (next) {
    case Delivery::Delivered: return current != Delivery::Delivered;
    case Delivery::Failed:    return current == Delivery::Queued || current == Delivery::Relayed;
    case Delivery::Relayed:   return current == Delivery::Queued;
    default:                  return false;
    }
}

int ConversationStore::updateDelivery(uint32_t packetId, Delivery next) {
    if (packetId == 0) return -1;
    for (size_t i = 0; i < _convs.size(); ++i) {
        for (auto& m : _convs[i].messages) {
            if (!m.fromMe || m.packetId != packetId) continue;
            if (!deliveryAdvances(m.delivery, next)) return -1;
            m.delivery = next;
            ++_revision;
            return static_cast<int>(i);
        }
    }
    return -1;
}

void ConversationStore::markRead(int index) {
    Conversation* c = at(index);
    if (!c || !c->unread) return;
    c->unread = false;
    ++_revision;
}

int ConversationStore::unreadCount() const {
    int n = 0;
    for (const auto& c : _convs) if (c.unread) ++n;
    return n;
}

std::vector<int> ConversationStore::homeOrder() const {
    std::vector<int> order;
    for (size_t i = 0; i < _convs.size(); ++i) {
        if (!_convs[i].messages.empty()) order.push_back(static_cast<int>(i));
    }
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
        const Conversation& ca = _convs[a];
        const Conversation& cb = _convs[b];
        if (ca.unread != cb.unread) return ca.unread;
        return ca.lastActivity > cb.lastActivity;
    });
    return order;
}

// ---- persistence -------------------------------------------------------

namespace {

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:   out += c; break;
        }
    }
    return out;
}

std::string unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { out += s[i]; continue; }
        char n = s[++i];
        if (n == 'n') out += '\n';
        else if (n == 't') out += '\t';
        else if (n == 'r') out += '\r';
        else out += n;
    }
    return out;
}

std::vector<std::string> splitTabs(const std::string& line, size_t maxFields) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (fields.size() + 1 < maxFields) {
        size_t tab = line.find('\t', start);
        if (tab == std::string::npos) break;
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    fields.push_back(line.substr(start));
    return fields;
}

uint32_t hexField(const std::string& s) { return static_cast<uint32_t>(strtoul(s.c_str(), nullptr, 16)); }
uint32_t decField(const std::string& s) { return static_cast<uint32_t>(strtoul(s.c_str(), nullptr, 10)); }

std::string hex(uint32_t v) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%08x", static_cast<unsigned>(v));
    return buf;
}

} // namespace

std::string ConversationStore::serialize(const Conversation& conv) {
    std::string out = "MPC1\n";
    out += "k\t";
    if (conv.key.isNostr()) out += "n\t" + conv.key.nostr + "\t0\n";
    else {
        out += conv.key.isChannel ? "c" : "d";
        out += "\t" + hex(conv.key.peer) + "\t" + std::to_string(conv.key.channel) + "\n";
    }
    out += "u\t";
    out += conv.unread ? "1" : "0";
    out += "\t" + std::to_string(conv.lastActivity) + "\n";
    for (const auto& m : conv.messages) {
        out += "m\t";
        out += m.fromMe ? "1" : "0";
        out += "\t" + hex(m.from) + "\t" + hex(m.packetId) + "\t" +
               std::to_string(static_cast<int>(m.delivery)) + "\t" +
               std::to_string(m.timestamp) + "\t" + escape(m.text) + "\n";
    }
    return out;
}

bool ConversationStore::parse(const std::string& data, Conversation& out) {
    out = Conversation();
    bool sawHeader = false, sawKey = false;
    size_t start = 0;
    while (start < data.size()) {
        size_t nl = data.find('\n', start);
        std::string line = data.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        start = (nl == std::string::npos) ? data.size() : nl + 1;
        if (line.empty()) continue;

        if (!sawHeader) {
            if (line != "MPC1") return false;
            sawHeader = true;
            continue;
        }
        char tag = line[0];
        if (tag == 'k') {
            auto f = splitTabs(line, 4);
            if (f.size() != 4) return false;
            if (f[1] == "n") {
                if (f[2].size() != 64) return false;
                out.key.nostr = f[2];
            } else {
                out.key.isChannel = (f[1] == "c");
                out.key.peer = hexField(f[2]);
                out.key.channel = static_cast<uint8_t>(decField(f[3]));
            }
            sawKey = true;
        } else if (tag == 'u') {
            auto f = splitTabs(line, 3);
            if (f.size() != 3) continue;
            out.unread = (f[1] == "1");
            out.lastActivity = decField(f[2]);
        } else if (tag == 'm') {
            auto f = splitTabs(line, 7);
            if (f.size() != 7) continue;
            ChatMessage m;
            m.fromMe = (f[1] == "1");
            m.from = hexField(f[2]);
            m.packetId = hexField(f[3]);
            uint32_t d = decField(f[4]);
            m.delivery = d <= static_cast<uint32_t>(Delivery::Failed) ? static_cast<Delivery>(d)
                                                                      : Delivery::Received;
            m.timestamp = decField(f[5]);
            m.text = unescape(f[6]);
            out.messages.push_back(m);
        }
    }
    if (out.messages.size() > kMaxMessagesPerConversation) {
        out.messages.erase(out.messages.begin(),
                           out.messages.end() - kMaxMessagesPerConversation);
    }
    return sawHeader && sawKey;
}

int ConversationStore::adopt(const Conversation& conv) {
    int idx = find(conv.key);
    if (idx < 0) {
        _convs.push_back(conv);
        idx = static_cast<int>(_convs.size()) - 1;
    } else {
        _convs[idx] = conv;
    }
    if (conv.lastActivity >= _nextActivity) _nextActivity = conv.lastActivity + 1;
    ++_revision;
    return idx;
}

std::string ConversationStore::fileName(const ConversationKey& key) {
    if (key.isNostr()) return "n" + key.nostr.substr(0, 16) + ".txt";  // FAT: keep it short
    if (key.isChannel) return "c" + std::to_string(key.channel) + ".txt";
    return "d" + hex(key.peer) + ".txt";
}
