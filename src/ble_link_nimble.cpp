// BleLink over NimBLE-Arduino (Cardputer / ESP32-S3).
#if !defined(MESHPUTER_BLE_ARDUINO)

#include "ble_link.h"

#include <NimBLEDevice.h>

#include "meshtastic_client.h"

namespace {

const NimBLEUUID kServiceUuid("6ba1b218-15a8-461f-9fa8-5dcae273eafd");
const NimBLEUUID kToRadioUuid("f75c76d2-129e-4dad-a1dd-7866124401e7");
const NimBLEUUID kFromRadioUuid("2c55e69e-4993-11ed-b878-0242ac120002");
const NimBLEUUID kFromNumUuid("ed9da18c-a800-4f66-a670-aa7547e34453");

constexpr uint32_t kConnectTimeoutMs = 8000;
// Connection interval in 1.25 ms units: fast while downloading the node
// database, slow once synced so the radio wakes ~7x/s instead of ~40x/s.
constexpr uint16_t kFastIntervalMin = 12;   // 15 ms
constexpr uint16_t kFastIntervalMax = 24;   // 30 ms
constexpr uint16_t kSlowIntervalMin = 96;   // 120 ms
constexpr uint16_t kSlowIntervalMax = 128;  // 160 ms
constexpr uint16_t kSupervisionTimeout = 600;  // 6 s, in 10 ms units

NimBLEClient* sClient = nullptr;
NimBLERemoteCharacteristic* sToRadio = nullptr;
NimBLERemoteCharacteristic* sFromRadio = nullptr;

class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient*) override { gMesh.bleConnected(); }
    void onConnectFail(NimBLEClient*, int reason) override { gMesh.bleConnectFailed(reason); }
    void onDisconnect(NimBLEClient*, int reason) override { gMesh.bleDisconnected(reason); }
    void onPassKeyEntry(NimBLEConnInfo& info) override { gMesh.blePasskeyRequested(info.getConnHandle()); }
    void onAuthenticationComplete(NimBLEConnInfo& info) override { gMesh.bleAuthComplete(info.isEncrypted()); }
    bool onConnParamsUpdateRequest(NimBLEClient*, const ble_gap_upd_params*) override { return true; }
};

class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        if (!dev->isAdvertisingService(kServiceUuid)) return;
        RadioInfo r;
        r.name = dev->getName();
        r.addr = dev->getAddress().toString();
        r.addrType = dev->getAddress().getType();
        r.rssi = dev->getRSSI();
        gMesh.bleScanResult(r);
    }
    void onScanEnd(const NimBLEScanResults&, int) override { gMesh.bleScanEnded(); }
};

ClientCallbacks sClientCallbacks;
ScanCallbacks sScanCallbacks;

} // namespace

namespace BleLink {

bool up() {
    if (!NimBLEDevice::init("Meshputer")) return false;
    NimBLEDevice::setMTU(517);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);
    sClient = NimBLEDevice::createClient();
    sClient->setClientCallbacks(&sClientCallbacks, false);
    sClient->setConnectTimeout(kConnectTimeoutMs);
    return true;
}

void down() {
    NimBLEDevice::deinit(true);
    sClient = nullptr;
    sToRadio = sFromRadio = nullptr;
}

bool startScan(uint32_t ms) {
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&sScanCallbacks, false);
    scan->setActiveScan(true);   // radios put their name in the scan response
    scan->setInterval(100);
    scan->setWindow(60);
    scan->setDuplicateFilter(false);
    return scan->start(ms, false, true);
}

void stopScan() { NimBLEDevice::getScan()->stop(); }

bool connect(const std::string& addr, uint8_t addrType) {
    if (!sClient) return false;
    sToRadio = sFromRadio = nullptr;
    sClient->setConnectionParams(kFastIntervalMin, kFastIntervalMax, 0, kSupervisionTimeout);
    return sClient->connect(NimBLEAddress(addr, addrType), true, true, true);
}

void cancelConnect() { if (sClient) sClient->cancelConnect(); }

void disconnect() {
    sToRadio = sFromRadio = nullptr;
    if (sClient && sClient->isConnected()) sClient->disconnect();
}

bool isConnected() { return sClient && sClient->isConnected(); }

void secure() { if (sClient) sClient->secureConnection(true); }
bool interactivePin() { return true; }

bool submitPin(uint32_t pin) {
    if (!sClient) return false;
    return NimBLEDevice::injectPassKey(sClient->getConnInfo(), pin);
}

void setPin(uint32_t) {}

bool findMeshService() {
    NimBLERemoteService* svc = sClient ? sClient->getService(kServiceUuid) : nullptr;
    if (!svc) return false;
    sToRadio = svc->getCharacteristic(kToRadioUuid);
    sFromRadio = svc->getCharacteristic(kFromRadioUuid);
    NimBLERemoteCharacteristic* fromNum = svc->getCharacteristic(kFromNumUuid);
    if (!sToRadio || !sFromRadio || !fromNum) return false;
    if (fromNum->canNotify()) {
        fromNum->subscribe(true, [](NimBLERemoteCharacteristic*, uint8_t*, size_t, bool) {
            gMesh.bleFromNum();
        }, true);
    }
    return true;
}

bool write(const uint8_t* data, size_t len) {
    return sToRadio && len > 0 && sToRadio->writeValue(data, len, true);
}

bool read(std::string& frame) {
    if (!sFromRadio || !isConnected()) return false;
    NimBLEAttValue v = sFromRadio->readValue();
    if (v.size() == 0) return false;
    frame.assign(reinterpret_cast<const char*>(v.data()), v.size());
    return true;
}

void setFastInterval() {
    if (sClient) sClient->updateConnParams(kFastIntervalMin, kFastIntervalMax, 0, kSupervisionTimeout);
}

void setSlowInterval() {
    if (sClient) sClient->updateConnParams(kSlowIntervalMin, kSlowIntervalMax, 0, kSupervisionTimeout);
}

bool isBonded(const std::string& addr, uint8_t addrType) {
    return NimBLEDevice::isBonded(NimBLEAddress(addr, addrType));
}

void deleteBond(const std::string& addr, uint8_t addrType) {
    NimBLEDevice::deleteBond(NimBLEAddress(addr, addrType));
}

} // namespace BleLink

#endif
