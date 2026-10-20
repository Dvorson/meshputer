#pragma once

// The Bluetooth LE link to one Meshtastic radio, behind a small interface so
// each board can use the stack that works on its chip:
//   ble_link_nimble.cpp   NimBLE-Arduino      (Cardputer, ESP32-S3)
//   ble_link_arduino.cpp  arduino-esp32 BLE   (Tab5, ESP32-P4 + C6 over esp-hosted)
//
// Asynchronous results are reported through gMesh.ble*() entry points, which
// only raise flags; MeshtasticClient::loop() acts on them.

#include <cstddef>
#include <cstdint>
#include <string>

namespace BleLink {

bool up();     // power the controller, set security: bonding, MITM, keyboard-only IO
void down();   // power it off again

bool startScan(uint32_t ms);  // results: gMesh.bleScanResult / bleScanEnded
void stopScan();

// Starts connecting; result: gMesh.bleConnected / bleConnectFailed.
bool connect(const std::string& addr, uint8_t addrType);
void cancelConnect();
void disconnect();  // gMesh.bleDisconnected follows
bool isConnected();

// Starts pairing/encryption; result: gMesh.bleAuthComplete. A PIN request
// arrives as gMesh.blePasskeyRequested when interactivePin() is true.
void secure();
// true: the stack asks for the PIN mid-pairing (answer with submitPin).
// false: the PIN must be given up front with setPin() before connecting.
bool interactivePin();
bool submitPin(uint32_t pin);
void setPin(uint32_t pin);

// Finds the Meshtastic service and subscribes to FromNum (gMesh.bleFromNum).
bool findMeshService();
bool write(const uint8_t* data, size_t len);  // to ToRadio, with response
bool read(std::string& frame);                // one FromRadio frame; false when empty

void setFastInterval();  // while syncing the node database
void setSlowInterval();  // once synced, to save power

bool isBonded(const std::string& addr, uint8_t addrType);
void deleteBond(const std::string& addr, uint8_t addrType);

} // namespace BleLink
