// Meshputer: Meshtastic text chat for the M5Stack Cardputer.
//
// The Cardputer pairs over Bluetooth LE with a Meshtastic radio and acts as
// its keyboard and screen. All LoRa traffic goes through that radio; this
// firmware never powers Wi-Fi and only powers Bluetooth while it is needed.

#include <Arduino.h>

#include "board.h"
#include "chat_service.h"
#include "debug_console.h"
#include "log.h"
#include "meshtastic_client.h"
#include "power.h"
#include "screens/screens.h"
#include "sound.h"

namespace {

constexpr uint32_t kHeaderRefreshMs = 30000;  // battery level in the header

LinkState sLastLink = LinkState::Off;
uint32_t sSeenMesh = 0;
uint32_t sSeenChat = 0;
bool sToastShown = false;
uint32_t sLastFrameMs = 0;

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
    gMesh.begin();
    Chat::begin();
    Nav::reset(&gHomeScreen);
}

void loop() {
    Board::update();
    DebugConsole::poll();
    gMesh.loop();
    Chat::loop();
    Power::loop();
    announceLinkChanges();

    // The pairing prompt appears over whatever is on screen.
    if (gMesh.state() == LinkState::Pairing && Nav::current() != &gPinScreen) {
        Power::onKey();
        Nav::push(&gPinScreen);
    }

    KeyInput keys;
    bool injected = DebugConsole::popKey(keys);
    if (injected || Board::pollKeys(keys)) {
        bool wokeScreen = Power::onKey();
        if (injected || !wokeScreen) Nav::current()->key(keys);
    }

    Nav::current()->tick();
    Screen* screen = Nav::current();  // tick() may have navigated

    // Redraw on any change the current screen might show.
    bool toast = UI::toastVisible();
    uint32_t now = millis();
    if (gMesh.revision() != sSeenMesh || Chat::store().revision() != sSeenChat ||
        toast != sToastShown || now - sLastFrameMs > kHeaderRefreshMs) {
        sSeenMesh = gMesh.revision();
        sSeenChat = Chat::store().revision();
        sToastShown = toast;
        screen->invalidate();
    }
    if (Power::screenOn() && screen->takeDirty()) {
        screen->draw(Board::canvas());
        Board::scanKeyboard();  // a redraw takes tens of ms; don't miss quick taps
        Board::present();
        Board::scanKeyboard();
        sLastFrameMs = now;
    }

    delay(Power::loopDelayMs());
}
