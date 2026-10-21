// Meshputer: Meshtastic text chat for the M5Stack Cardputer and Tab5.
//
// The device pairs over Bluetooth LE with a Meshtastic radio and acts as its
// keyboard and screen. All LoRa traffic goes through that radio. Wi-Fi is
// powered only for the optional internet chats (Nostr), Bluetooth only while
// a radio is needed.

#include <Arduino.h>

#include "app_ui.h"
#include "board.h"
#include "chat_service.h"
#include "debug_console.h"
#include "log.h"
#include "core/crypto.h"
#include "meshtastic_client.h"
#include "nostr_service.h"
#include "power.h"
#include "sound.h"

namespace {

LinkState sLastLink = LinkState::Off;

void announceLinkChanges() {
    LinkState now = gMesh.state();
    if (now == sLastLink) return;
    if (now == LinkState::Ready) Sound::connected();
    else if (sLastLink == LinkState::Ready) Sound::disconnected();
    sLastLink = now;
}

} // namespace

void setup() {
    Serial.begin(115200);
    Board::begin();
    Power::begin();
    LOG("APP", "Meshputer starting, sd=%d", Board::sdReady());
    Crypto::setRandom([](uint8_t* out, size_t len) { esp_fill_random(out, len); });  // hardware RNG
    gMesh.begin();
    NostrService::begin();
    Chat::begin();
    AppUi::begin();
}

void loop() {
    Board::update();
    DebugConsole::poll();
    gMesh.loop();
    NostrService::loop();
    Chat::loop();
    Power::loop();
    announceLinkChanges();
    AppUi::loop();
    delay(Power::loopDelayMs());
}
