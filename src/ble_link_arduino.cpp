// BleLink over the arduino-esp32 BLE library (Tab5: ESP32-P4, radio on the
// ESP32-C6 via esp-hosted). NimBLE-Arduino does not support the P4.
//
// This library's connect() and secureConnection() block, so they run in a
// short-lived worker task and report back through gMesh.ble*() like the
// NimBLE callbacks do. It also takes the pairing PIN up front (static
// passkey) instead of asking mid-pairing, so interactivePin() is false.
#if defined(MESHPUTER_BLE_ARDUINO)

#include "ble_link.h"

#include <BLEDevice.h>
#include <BLESecurity.h>
#include "host/ble_store.h"

#include <cstdio>

#include "meshtastic_client.h"

namespace {

BLEUUID kServiceUuid("6ba1b218-15a8-461f-9fa8-5dcae273eafd");
BLEUUID kToRadioUuid("f75c76d2-129e-4dad-a1dd-7866124401e7");
BLEUUID kFromRadioUuid("2c55e69e-4993-11ed-b878-0242ac120002");
BLEUUID kFromNumUuid("ed9da18c-a800-4f66-a670-aa7547e34453");

constexpr uint8_t kIoKeyboardOnly = 0x02;
constexpr uint32_t kWorkerStack = 8192;

BLEClient* sClient = nullptr;
BLERemoteCharacteristic* sToRadio = nullptr;
BLERemoteCharacteristic* sFromRadio = nullptr;
bool sInitialized = false;
uint32_t sPin = 123456;
volatile bool sCancel = false;
std::string sConnectAddr;
uint8_t sConnectType = 0;

class SecurityCallbacks : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return sPin; }
    void onPassKeyNotify(uint32_t) override {}
    bool onSecurityRequest() override { return true; }
    bool onConfirmPIN(uint32_t) override { return true; }
    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        gMesh.bleAuthComplete(desc && desc->sec_state.encrypted);
    }
};

class ClientCallbacks : public BLEClientCallbacks {
    void onConnect(BLEClient*) override {}  // reported by the connect worker
    void onDisconnect(BLEClient*) override {
        sToRadio = sFromRadio = nullptr;
        gMesh.bleDisconnected(1);
    }
};

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        if (!dev.isAdvertisingService(kServiceUuid)) return;
        RadioInfo r;
        r.name = dev.getName().c_str();
        r.addr = dev.getAddress().toString().c_str();
        r.addrType = dev.getAddressType();
        r.rssi = dev.getRSSI();
        gMesh.bleScanResult(r);
    }
};

SecurityCallbacks sSecurityCallbacks;
ClientCallbacks sClientCallbacks;
ScanCallbacks sScanCallbacks;

void connectWorker(void*) {
    bool ok = sClient->connect(BLEAddress(sConnectAddr.c_str()), sConnectType);
    if (ok && sCancel) {
        sClient->disconnect();
        ok = false;
    }
    if (ok) {
        sClient->setMTU(517);
        gMesh.bleConnected();
    } else {
        gMesh.bleConnectFailed(-1);
    }
    vTaskDelete(nullptr);
}

void secureWorker(void*) {
    bool ok = sClient->secureConnection();
    if (!ok) gMesh.bleAuthComplete(false);  // success arrives via onAuthenticationComplete
    vTaskDelete(nullptr);
}

bool toAddr(const std::string& addr, uint8_t type, ble_addr_t& out) {
    unsigned b[6];
    if (sscanf(addr.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return false;
    out.type = type;
    for (int i = 0; i < 6; ++i) out.val[i] = static_cast<uint8_t>(b[5 - i]);  // NimBLE stores LSB first
    return true;
}

} // namespace

namespace BleLink {

bool up() {
    // The esp-hosted link to the C6 does not survive repeated deinit/init
    // well, and the Tab5 is not battery-critical: initialise once, keep it.
    if (sInitialized) return true;
    if (!BLEDevice::init("Meshputer")) return false;
    BLEDevice::setMTU(517);
    BLEDevice::setSecurityCallbacks(&sSecurityCallbacks);
    BLESecurity::setAuthenticationMode(true, true, true);
    BLESecurity::setCapability(kIoKeyboardOnly);
    BLESecurity::setPassKey(true, sPin);
    sClient = BLEDevice::createClient();
    sClient->setClientCallbacks(&sClientCallbacks);
    sInitialized = true;
    return true;
}

void down() {
    disconnect();
}

bool startScan(uint32_t ms) {
    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&sScanCallbacks, true);
    scan->setActiveScan(true);
    scan->setInterval(160);
    scan->setWindow(96);
    return scan->start((ms + 999) / 1000, [](BLEScanResults) { gMesh.bleScanEnded(); }, false);
}

void stopScan() {
    BLEDevice::getScan()->stop();
    gMesh.bleScanEnded();
}

bool connect(const std::string& addr, uint8_t addrType) {
    if (!sClient) return false;
    sToRadio = sFromRadio = nullptr;
    sCancel = false;
    sConnectAddr = addr;
    sConnectType = addrType;
    return xTaskCreate(connectWorker, "ble-connect", kWorkerStack, nullptr, 5, nullptr) == pdPASS;
}

void cancelConnect() { sCancel = true; }

void disconnect() {
    sToRadio = sFromRadio = nullptr;
    if (sClient && sClient->isConnected()) sClient->disconnect();
}

bool isConnected() { return sClient && sClient->isConnected(); }

void secure() {
    if (sClient) xTaskCreate(secureWorker, "ble-secure", kWorkerStack, nullptr, 5, nullptr);
}

bool interactivePin() { return false; }
bool submitPin(uint32_t) { return false; }

void setPin(uint32_t pin) {
    sPin = pin;
    BLESecurity::setPassKey(true, pin);
}

bool findMeshService() {
    BLERemoteService* svc = sClient ? sClient->getService(kServiceUuid) : nullptr;
    if (!svc) return false;
    sToRadio = svc->getCharacteristic(kToRadioUuid);
    sFromRadio = svc->getCharacteristic(kFromRadioUuid);
    BLERemoteCharacteristic* fromNum = svc->getCharacteristic(kFromNumUuid);
    if (!sToRadio || !sFromRadio || !fromNum) return false;
    if (fromNum->canNotify()) {
        fromNum->subscribe(true, [](BLERemoteCharacteristic*, uint8_t*, size_t, bool) {
            gMesh.bleFromNum();
        }, true);
    }
    return true;
}

bool write(const uint8_t* data, size_t len) {
    return sToRadio && len > 0 && sToRadio->writeValue(const_cast<uint8_t*>(data), len, true);
}

bool read(std::string& frame) {
    if (!sFromRadio || !isConnected()) return false;
    String v = sFromRadio->readValue();
    if (v.length() == 0) return false;
    frame.assign(v.c_str(), v.length());
    return true;
}

// Interval in 1.25 ms units; same values as the Cardputer build.
void setFastInterval() { if (sClient) sClient->updateConnParams(12, 24, 0, 600); }
void setSlowInterval() { if (sClient) sClient->updateConnParams(96, 128, 0, 600); }

bool isBonded(const std::string& addr, uint8_t addrType) {
    ble_addr_t want;
    if (!toAddr(addr, addrType, want)) return false;
    ble_addr_t peers[8];
    int count = 0;
    if (ble_store_util_bonded_peers(peers, &count, 8) != 0) return false;
    for (int i = 0; i < count; ++i) {
        if (memcmp(peers[i].val, want.val, 6) == 0) return true;
    }
    return false;
}

void deleteBond(const std::string& addr, uint8_t addrType) {
    ble_addr_t a;
    if (toAddr(addr, addrType, a)) ble_store_util_delete_peer(&a);
}

} // namespace BleLink

#endif
