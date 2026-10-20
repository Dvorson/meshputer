#pragma once

// Internet reach for Meshputer: the device's Nostr identity, a couple of public
// relays over Wi-Fi, and NIP-17 private DMs in and out.
//
// Off by default; Wi-Fi is powered only while this is enabled. On connect it
// publishes a profile (kind 0) that links the npub to the paired Meshtastic
// node, and a DM relay list (kind 10050) so other clients know where to deliver.

#include <cstdint>
#include <string>

namespace NostrService {

enum class State : uint8_t { Off, Connecting, Online };

void begin();
void loop();

void setEnabled(bool on);
State state();
const std::string& status();
int relaysOnline();

const std::string& pubHex();
std::string npub();

// Sends a NIP-17 DM. Returns a reference id for delivery tracking, 0 on failure.
uint32_t sendDm(const std::string& toHex, const std::string& text, std::string& error);

// Re-publish profile + relay list (e.g. after the radio identity changed).
void publishProfile();

// Display name from the peer's Nostr profile, or a short npub until it is known.
std::string displayName(const std::string& pubHex);

struct Incoming {
    enum class Kind : uint8_t { Message, Accepted, Rejected } kind;
    std::string fromHex;   // Message
    std::string text;      // Message
    int64_t createdAt = 0; // Message
    uint32_t ref = 0;      // Message: stable id for dedupe; Accepted/Rejected: sendDm's id
};
bool popIncoming(Incoming& out);

// One line per relay with counters, for the serial console.
std::string diagnostics();

} // namespace NostrService
