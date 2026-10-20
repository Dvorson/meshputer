#pragma once

// Conversation model: DMs keyed by node number, channels keyed by channel index.
//
// Portable C++ (no Arduino), unit-tested natively. Persistence is a plain text
// codec; the caller decides where the bytes live (SD card on the device).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

constexpr size_t kMaxMessagesPerConversation = 50;

enum class Delivery : uint8_t {
    Received = 0,  // incoming message
    Queued,        // written to our radio, no ack yet
    Relayed,       // our radio heard the mesh repeat it (implicit ack)
    Delivered,     // the destination node acknowledged it
    Failed,        // radio reported an error (no route, timeout, ...)
};

const char* deliveryLabel(Delivery d);

struct ConversationKey {
    bool isChannel = false;
    uint32_t peer = 0;     // node number for DMs
    uint8_t channel = 0;   // channel index for channel conversations

    static ConversationKey direct(uint32_t node) { ConversationKey k; k.peer = node; return k; }
    static ConversationKey onChannel(uint8_t ch) { ConversationKey k; k.isChannel = true; k.channel = ch; return k; }
    bool operator==(const ConversationKey& o) const {
        return isChannel == o.isChannel && (isChannel ? channel == o.channel : peer == o.peer);
    }
};

struct ChatMessage {
    bool fromMe = false;
    uint32_t from = 0;
    std::string text;      // UTF-8 as sent/received
    uint32_t packetId = 0;
    Delivery delivery = Delivery::Received;
    uint32_t timestamp = 0;  // epoch seconds from the radio; 0 if unknown
};

struct Conversation {
    ConversationKey key;
    std::vector<ChatMessage> messages;
    bool unread = false;
    uint32_t lastActivity = 0;  // monotonically increasing sequence, for ordering
};

class ConversationStore {
public:
    void clear();
    size_t size() const { return _convs.size(); }
    Conversation* at(int index);
    const Conversation* at(int index) const;

    int find(const ConversationKey& key) const;
    int ensure(const ConversationKey& key);

    // Returns the conversation index, or -1 if this packet was already stored.
    int addIncoming(const ConversationKey& key, uint32_t from, const std::string& text,
                    uint32_t packetId, uint32_t timestamp, bool markUnread);
    int addOutgoing(const ConversationKey& key, uint32_t from, const std::string& text,
                    uint32_t packetId, uint32_t timestamp);

    // Applies an ack/nak to our message with this packet id. Delivery only moves
    // forward (a late implicit ack never hides a real one). Returns the index of
    // the conversation that changed, or -1.
    int updateDelivery(uint32_t packetId, Delivery next);

    void markRead(int index);
    int unreadCount() const;

    // Conversations worth listing on the home screen: any with messages,
    // unread first, then most recent.
    std::vector<int> homeOrder() const;

    // Bumped on any change so screens can cheaply detect when to redraw.
    uint32_t revision() const { return _revision; }

    // Persistence codec.
    static std::string serialize(const Conversation& conv);
    static bool parse(const std::string& data, Conversation& out);
    // Adds a parsed conversation (replacing one with the same key).
    int adopt(const Conversation& conv);

    // File name (no directory) for a conversation, e.g. "da1b2c3d4.txt" / "c0.txt".
    static std::string fileName(const ConversationKey& key);

private:
    void touch(Conversation& conv);

    std::vector<Conversation> _convs;
    uint32_t _nextActivity = 1;
    uint32_t _revision = 0;
};
