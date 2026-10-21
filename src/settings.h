#pragma once

// User settings persisted in NVS ("meshputer" namespace).

#include <cstdint>
#include <string>

constexpr int kMaxCannedResponses = 8;

struct Settings {
    // Sound
    bool soundEnabled = true;
    uint8_t volume = 128;

    // Radio link
    std::string blePin = "123456";  // Meshtastic's default fixed PIN
    bool autoConnect = true;        // reconnect to the saved radio on boot / link loss
    std::string radioAddr;          // saved radio BLE address, empty = none
    uint8_t radioAddrType = 0;
    std::string radioName;
    uint32_t lastNodeNum = 0;       // node of the saved radio, to show history offline

    // Internet (Nostr over Wi-Fi). Off by default: Wi-Fi stays unpowered.
    bool internetEnabled = false;
    std::string wifiSsid;
    std::string wifiPassword;
    std::string nostrSecretHex;     // generated on first use; the device's Nostr identity

    // Display
    uint8_t brightness = 140;
    uint16_t dimAfterSec = 20;
    uint16_t screenOffSec = 60;     // 0 = never
    uint8_t themeIndex = 0;
    uint8_t orientation = 0;        // touch screens: 0 follows the device, 1 portrait, 2 landscape

    std::string canned[kMaxCannedResponses];

    static Settings& get();
    void save();
    void resetToDefaults();

private:
    void load();
};
