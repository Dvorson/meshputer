#include "check.h"

#include "core/conversations.h"

namespace {
constexpr uint32_t kMe = 0xDA6573C0;     // node numbers above 2^31 are common
constexpr uint32_t kPeer = 0xF00DBABE;
}

TEST(dm_and_channel_keys_are_distinct) {
    ConversationStore s;
    int dm = s.ensure(ConversationKey::direct(5));
    int ch = s.ensure(ConversationKey::onChannel(5));
    CHECK(dm != ch);
    CHECK_EQ(s.ensure(ConversationKey::direct(5)), dm);
    CHECK_EQ(s.ensure(ConversationKey::onChannel(5)), ch);
}

TEST(incoming_duplicates_are_dropped) {
    ConversationStore s;
    auto key = ConversationKey::direct(kPeer);
    CHECK(s.addIncoming(key, kPeer, "hi", 42, 0, true) >= 0);
    CHECK_EQ(s.addIncoming(key, kPeer, "hi", 42, 0, true), -1);
    CHECK_EQ(s.at(s.find(key))->messages.size(), (size_t)1);
    // Packet id 0 is never treated as a duplicate.
    CHECK(s.addIncoming(key, kPeer, "a", 0, 0, true) >= 0);
    CHECK(s.addIncoming(key, kPeer, "b", 0, 0, true) >= 0);
}

TEST(delivery_only_moves_forward) {
    ConversationStore s;
    auto key = ConversationKey::direct(kPeer);
    s.addOutgoing(key, kMe, "ping", 0x77, 0);
    const ChatMessage& m = s.at(0)->messages.back();
    CHECK(m.delivery == Delivery::Queued);
    CHECK_EQ(s.updateDelivery(0x77, Delivery::Relayed), 0);
    CHECK(m.delivery == Delivery::Relayed);
    CHECK_EQ(s.updateDelivery(0x77, Delivery::Delivered), 0);
    CHECK(m.delivery == Delivery::Delivered);
    // A late implicit ack or a stray nak must not hide the real ack.
    CHECK_EQ(s.updateDelivery(0x77, Delivery::Relayed), -1);
    CHECK_EQ(s.updateDelivery(0x77, Delivery::Failed), -1);
    CHECK(m.delivery == Delivery::Delivered);
    CHECK_EQ(s.updateDelivery(0x999, Delivery::Delivered), -1);
}

TEST(failed_then_late_ack_is_delivered) {
    ConversationStore s;
    s.addOutgoing(ConversationKey::direct(kPeer), kMe, "x", 9, 0);
    s.updateDelivery(9, Delivery::Failed);
    CHECK(s.at(0)->messages[0].delivery == Delivery::Failed);
    s.updateDelivery(9, Delivery::Delivered);
    CHECK(s.at(0)->messages[0].delivery == Delivery::Delivered);
}

TEST(history_is_capped) {
    ConversationStore s;
    auto key = ConversationKey::onChannel(0);
    for (uint32_t i = 1; i <= kMaxMessagesPerConversation + 5; ++i) {
        s.addIncoming(key, kPeer, "m" + std::to_string(i), i, 0, false);
    }
    const Conversation* c = s.at(s.find(key));
    CHECK_EQ(c->messages.size(), kMaxMessagesPerConversation);
    CHECK_EQ(c->messages.front().text, std::string("m6"));
}

TEST(home_order_unread_first_then_recent) {
    ConversationStore s;
    int a = s.addIncoming(ConversationKey::direct(1), 1, "a", 1, 0, false);
    int b = s.addIncoming(ConversationKey::direct(2), 2, "b", 2, 0, true);
    int c = s.addIncoming(ConversationKey::direct(3), 3, "c", 3, 0, false);
    s.ensure(ConversationKey::onChannel(1));  // empty: hidden
    auto order = s.homeOrder();
    CHECK_EQ(order.size(), (size_t)3);
    CHECK_EQ(order[0], b);
    CHECK_EQ(order[1], c);
    CHECK_EQ(order[2], a);
    s.markRead(b);
    order = s.homeOrder();
    CHECK_EQ(order[0], c);
    CHECK_EQ(s.unreadCount(), 0);
}

TEST(revision_changes_on_every_mutation) {
    ConversationStore s;
    uint32_t r0 = s.revision();
    s.addOutgoing(ConversationKey::direct(1), kMe, "x", 5, 0);
    uint32_t r1 = s.revision();
    CHECK(r1 != r0);
    s.updateDelivery(5, Delivery::Delivered);
    CHECK(s.revision() != r1);
}

TEST(serialize_round_trip_preserves_everything) {
    Conversation c;
    c.key = ConversationKey::direct(0xFFFFFFFE);
    c.unread = true;
    c.lastActivity = 1234;
    ChatMessage in;
    in.from = kPeer;
    in.packetId = 0x80000001;
    in.text = "tabs\tand\nnewlines \\ and } braces \xF0\x9F\x91\x8D";
    in.timestamp = 1760000000;
    c.messages.push_back(in);
    ChatMessage out;
    out.fromMe = true;
    out.from = kMe;
    out.packetId = 0xFFFFFFFF;
    out.delivery = Delivery::Relayed;
    out.text = "reply";
    c.messages.push_back(out);

    Conversation back;
    CHECK(ConversationStore::parse(ConversationStore::serialize(c), back));
    CHECK(back.key == c.key);
    CHECK_EQ(back.key.peer, 0xFFFFFFFEu);
    CHECK(back.unread);
    CHECK_EQ(back.lastActivity, 1234u);
    CHECK_EQ(back.messages.size(), (size_t)2);
    CHECK_EQ(back.messages[0].text, in.text);
    CHECK_EQ(back.messages[0].from, kPeer);
    CHECK_EQ(back.messages[0].packetId, 0x80000001u);
    CHECK_EQ(back.messages[0].timestamp, 1760000000u);
    CHECK(back.messages[1].fromMe);
    CHECK_EQ(back.messages[1].packetId, 0xFFFFFFFFu);
    CHECK(back.messages[1].delivery == Delivery::Relayed);
}

TEST(channel_round_trip) {
    Conversation c;
    c.key = ConversationKey::onChannel(3);
    Conversation back;
    CHECK(ConversationStore::parse(ConversationStore::serialize(c), back));
    CHECK(back.key.isChannel);
    CHECK_EQ(back.key.channel, 3);
}

TEST(parse_rejects_foreign_files) {
    Conversation back;
    CHECK(!ConversationStore::parse("{\"v\":3,\"peer\":1}", back));
    CHECK(!ConversationStore::parse("", back));
    CHECK(!ConversationStore::parse("MPC1\nm\t0\t1\t2\t0\t0\tno key line\n", back));
}

TEST(adopt_keeps_activity_monotonic) {
    ConversationStore s;
    Conversation c;
    c.key = ConversationKey::direct(9);
    c.lastActivity = 500;
    ChatMessage m;
    m.text = "old";
    c.messages.push_back(m);
    s.adopt(c);
    int idx = s.addIncoming(ConversationKey::direct(10), 10, "new", 1, 0, false);
    CHECK(s.at(idx)->lastActivity > 500);
}

TEST(file_names) {
    CHECK_EQ(ConversationStore::fileName(ConversationKey::direct(0xDA6573C0)), std::string("dda6573c0.txt"));
    CHECK_EQ(ConversationStore::fileName(ConversationKey::onChannel(2)), std::string("c2.txt"));
}
