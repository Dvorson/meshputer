#pragma once

// Meshtastic client-API framing: encodes ToRadio and decodes FromRadio.
//
// Pure C++ on top of the nanopb-generated schema in lib/meshtastic-protobuf,
// with no Arduino or BLE dependency, so it runs in the native unit tests.
// The BLE layer (meshtastic_client.cpp) only moves these bytes across the
// ToRadio / FromRadio characteristics.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace MeshProto {

constexpr uint32_t kBroadcast = 0xFFFFFFFF;

// Text payload budget. The Data payload field holds 233 bytes, but PKI-encrypted
// direct messages lose 12 bytes to overhead; the official apps cap text at 200.
constexpr size_t kMaxTextBytes = 200;

// Field limits from the User message (excluding the NUL terminator in the schema).
constexpr size_t kMaxLongNameBytes  = 39;
constexpr size_t kMaxShortNameBytes = 4;

// Largest encoded ToRadio we ever produce; callers size buffers with this.
constexpr size_t kMaxToRadioBytes = 512;

// Routing.Error values we act on (see meshtastic/mesh.proto).
enum RoutingError : int {
    kRoutingNone = 0,
    kRoutingNoRoute = 1,
    kRoutingGotNak = 2,
    kRoutingTimeout = 3,
    kRoutingMaxRetransmit = 5,
    kRoutingNoChannel = 6,
    kRoutingTooLarge = 7,
    kRoutingNoResponse = 8,
    kRoutingPkiFailed = 34,
    kRoutingPkiUnknownPubkey = 35,
};

struct User {
    std::string id;          // "!a1b2c3d4"
    std::string longName;
    std::string shortName;
    uint32_t hwModel = 0;    // meshtastic_HardwareModel
    uint32_t role = 0;       // meshtastic_Config_DeviceConfig_Role
    bool isLicensed = false;
    bool hasPublicKey = false;
    std::string publicKeyHex;  // the node's X25519 key (PKI DMs), hex
};

struct NodeInfo {
    uint32_t num = 0;
    bool hasUser = false;
    User user;
    uint32_t lastHeard = 0;  // epoch seconds as reported by the radio
    float snr = 0.0f;
    uint8_t channel = 0;
    bool viaMqtt = false;
    bool hasHopsAway = false;
    uint8_t hopsAway = 0;
    bool hasBattery = false;
    uint32_t batteryLevel = 0;  // 0-100, 101 = powered
    bool hasVoltage = false;
    float voltage = 0.0f;
};

struct Channel {
    int8_t index = -1;
    std::string name;
    uint8_t role = 0;        // 0 disabled, 1 primary, 2 secondary
    uint8_t pskLen = 0;
    uint8_t pskFirstByte = 0;
};

struct TextMessage {
    uint32_t id = 0;
    uint32_t from = 0;
    uint32_t to = 0;
    uint8_t channel = 0;
    std::string text;        // raw UTF-8 as received
    uint32_t rxTime = 0;
    float snr = 0.0f;
    bool pkiEncrypted = false;
};

struct RoutingReport {
    uint32_t from = 0;       // node that produced the ack/nak
    uint32_t requestId = 0;  // our packet id
    int error = kRoutingNone;
};

struct AdminReply {
    uint32_t from = 0;
    bool hasOwner = false;
    User owner;
    std::vector<uint8_t> sessionPasskey;
};

struct QueueStatus {
    int res = 0;
    uint32_t free = 0;
    uint32_t maxlen = 0;
    uint32_t packetId = 0;
};

enum class EventKind : uint8_t {
    Ignored = 0,       // valid frame we do not care about
    MyInfo,            // myNodeNum
    NodeInfo,          // node db entry (config dump or live update)
    Channel,
    ConfigComplete,    // configCompleteId
    Text,
    UserUpdate,        // NODEINFO_APP packet: a node announced its identity
    Routing,           // ack / nak for one of our packets
    Admin,             // admin response from our node
    QueueStatus,
    Rebooted,
    Notification,      // clientNotification from the radio (human-readable)
    ModemPreset,       // LoRa preset name ("LongFast"): the default channel's display name
};

struct Event {
    EventKind kind = EventKind::Ignored;
    uint32_t myNodeNum = 0;          // MyInfo
    uint32_t configId = 0;           // ConfigComplete
    NodeInfo node;                   // NodeInfo; UserUpdate uses num + user
    MeshProto::Channel channel;      // Channel
    TextMessage text;                // Text
    RoutingReport routing;           // Routing
    AdminReply admin;                // Admin
    MeshProto::QueueStatus queue;    // QueueStatus
    std::string message;             // Notification; ModemPreset name
    uint8_t hopLimit = 0;            // ModemPreset: the radio's configured hop limit
};

// Decode one FromRadio frame. Returns false only on malformed protobuf.
bool decodeFromRadio(const uint8_t* data, size_t len, Event& out);

// Encoders return the number of bytes written to buf, or 0 on failure.
size_t encodeWantConfig(uint32_t nonce, uint8_t* buf, size_t cap);
size_t encodeDisconnect(uint8_t* buf, size_t cap);
size_t encodeHeartbeat(uint8_t* buf, size_t cap);

// hopLimit should be the radio's configured LoRa hop limit (default 3).
size_t encodeText(uint32_t packetId, uint32_t to, uint8_t channel,
                  const std::string& utf8, bool wantAck,
                  uint8_t* buf, size_t cap, uint8_t hopLimit = 3);

size_t encodeGetOwner(uint32_t packetId, uint32_t myNodeNum,
                      uint8_t* buf, size_t cap);

size_t encodeSetOwner(uint32_t packetId, uint32_t myNodeNum,
                      const std::string& longName, const std::string& shortName,
                      bool isLicensed, const std::vector<uint8_t>& sessionPasskey,
                      uint8_t* buf, size_t cap);

// "!a1b2c3d4" form used by Meshtastic for node ids.
std::string nodeIdString(uint32_t num);

// Short labels for enums shown on the node identity screen.
const char* hwModelName(uint32_t hwModel);
const char* roleName(uint32_t role);

} // namespace MeshProto
