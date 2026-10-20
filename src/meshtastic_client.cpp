#include "meshtastic_client.h"

#include <NimBLEDevice.h>
#include <esp_random.h>

#include <algorithm>

#include "log.h"
#include "settings.h"

MeshtasticClient gMesh;

namespace {

const NimBLEUUID kServiceUuid("6ba1b218-15a8-461f-9fa8-5dcae273eafd");
const NimBLEUUID kToRadioUuid("f75c76d2-129e-4dad-a1dd-7866124401e7");
const NimBLEUUID kFromRadioUuid("2c55e69e-4993-11ed-b878-0242ac120002");
const NimBLEUUID kFromNumUuid("ed9da18c-a800-4f66-a670-aa7547e34453");

constexpr uint32_t kScanMs = 10000;
constexpr uint32_t kConnectTimeoutMs = 8000;
constexpr uint32_t kSecureTimeoutMs = 15000;
constexpr uint32_t kPairingTimeoutMs = 90000;
constexpr uint32_t kSyncTimeoutMs = 60000;
constexpr uint32_t kIdlePowerDownMs = 5000;
constexpr uint32_t kSafetyPollMs = 30000;
constexpr uint32_t kHeartbeatMs = 300000;
constexpr uint32_t kMaxBackoffMs = 60000;
constexpr uint32_t kLongOutageMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kLongOutageBackoffMs = 5UL * 60UL * 1000UL;
constexpr size_t kMaxEvents = 32;
constexpr size_t kMaxNodes = 150;
constexpr size_t kMaxScanResults = 20;

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
NimBLERemoteCharacteristic* sFromNum = nullptr;
bool sBondRetried = false;

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

uint32_t newPacketId() {
    uint32_t id = esp_random();
    return id == 0 ? 1 : id;
}

} // namespace

const char* linkStateLabel(LinkState s) {
    switch (s) {
    case LinkState::Off:        return "off";
    case LinkState::Idle:       return "idle";
    case LinkState::Scanning:   return "scanning";
    case LinkState::Connecting: return "connecting";
    case LinkState::Pairing:    return "pairing";
    case LinkState::Syncing:    return "syncing";
    case LinkState::Ready:      return "ready";
    }
    return "?";
}

// ---- lifecycle ----------------------------------------------------------

void MeshtasticClient::begin() {
    const Settings& s = Settings::get();
    _myNode = s.lastNodeNum;
    if (!s.radioAddr.empty()) {
        _radio.addr = s.radioAddr;
        _radio.addrType = s.radioAddrType;
        _radio.name = s.radioName;
        _wasReady = true;
        if (s.autoConnect) _reconnectAt = millis() + 300;
    }
    _status = s.radioAddr.empty() ? "No radio paired" : "Radio offline";
}

void MeshtasticClient::setState(LinkState s) {
    if (_state != s) LOG("MESH", "state %s -> %s", linkStateLabel(_state), linkStateLabel(s));
    _state = s;
    _stateSince = millis();
    ++_revision;
}

bool MeshtasticClient::bleUp() {
    if (_bleUp) return true;
    if (!NimBLEDevice::init("Meshputer")) {
        _status = "Bluetooth failed to start";
        LOG("MESH", "NimBLE init failed");
        return false;
    }
    NimBLEDevice::setMTU(517);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);
    sClient = NimBLEDevice::createClient();
    sClient->setClientCallbacks(&sClientCallbacks, false);
    sClient->setConnectTimeout(kConnectTimeoutMs);
    _bleUp = true;
    LOG("MESH", "bluetooth on");
    setState(LinkState::Idle);
    return true;
}

void MeshtasticClient::bleDown() {
    if (!_bleUp) return;
    NimBLEDevice::deinit(true);
    sClient = nullptr;
    sToRadio = sFromRadio = sFromNum = nullptr;
    _bleUp = false;
    _flagConnected = false;
    _flagConnectFail = 0;
    _flagDisconnected = 0;
    _flagPasskey = false;
    _flagAuth = 0;
    _flagScanEnded = false;
    _flagPull = false;
    LOG("MESH", "bluetooth off");
    setState(LinkState::Off);
}

bool MeshtasticClient::wantsBle() const {
    return _state != LinkState::Idle && _state != LinkState::Off;
}

void MeshtasticClient::loop() {
    processBleFlags();
    uint32_t now = millis();

    switch (_state) {
    case LinkState::Off:
    case LinkState::Idle:
        if (_reconnectAt != 0 && static_cast<int32_t>(now - _reconnectAt) >= 0) {
            startConnect();
        } else if (_state == LinkState::Idle && _reconnectAt == 0 &&
                   now - _stateSince > kIdlePowerDownMs) {
            bleDown();  // nothing to talk to: power the radio down
        }
        break;

    case LinkState::Scanning:
        break;

    case LinkState::Connecting:
        // NimBLE reports a timeout itself; this is a backstop.
        if (now - _stateSince > kConnectTimeoutMs + 4000) dropLink("Connect timed out", true);
        break;

    case LinkState::Pairing:
        if (now - _stateSince > kPairingTimeoutMs) dropLink("PIN entry timed out", false);
        break;

    case LinkState::Syncing:
        if (_syncStep == SyncStep::Securing) {
            // Radios in "no PIN" mode may never complete a security exchange.
            if (now - _stateSince > kSecureTimeoutMs) {
                LOG("MESH", "no security exchange, trying discovery anyway");
                if (!discoverAndRequestConfig()) dropLink(_status, syncFailureIsTransient());
            }
        } else {
            pullFrames(6);
            if (_state == LinkState::Syncing && now - _stateSince > kSyncTimeoutMs) {
                dropLink("Radio did not finish sync", true);
            }
        }
        break;

    case LinkState::Ready:
        if (_flagPull.exchange(false) || now - _lastPollMs > kSafetyPollMs) pullFrames(8);
        if (_state == LinkState::Ready && now - _lastHeartbeatMs > kHeartbeatMs) {
            uint8_t buf[16];
            size_t n = MeshProto::encodeHeartbeat(buf, sizeof(buf));
            writeToRadio(buf, n);
            _lastHeartbeatMs = now;
        }
        stepOwnerUpdate();
        break;
    }
}

void MeshtasticClient::processBleFlags() {
    if (_flagConnected.exchange(false) && _state == LinkState::Connecting) {
        _syncStep = SyncStep::Securing;
        _status = "Securing link";
        setState(LinkState::Syncing);
        if (!sClient->secureConnection(true)) LOG("MESH", "secureConnection did not start");
    }

    int fail = _flagConnectFail.exchange(0);
    if (fail != 0 && _state == LinkState::Connecting) {
        dropLink("Radio not reachable (" + std::to_string(fail) + ")", true);
    }

    // Auth before disconnect: a radio rejecting a stale bond usually drops the
    // link in the same breath, and the bond must still be cleared.
    int auth = _flagAuth.exchange(0);
    bool linked = _state == LinkState::Connecting || _state == LinkState::Pairing ||
                  _state == LinkState::Syncing || _state == LinkState::Ready;
    if (auth == 1 && (_state == LinkState::Syncing || _state == LinkState::Pairing) &&
        _syncStep == SyncStep::Securing) {
        LOG("MESH", "link encrypted");
        if (!discoverAndRequestConfig()) dropLink(_status, syncFailureIsTransient());
    } else if (auth == -1 && linked) {
        _flagDisconnected = 0;
        NimBLEAddress addr(_radio.addr, _radio.addrType);
        bool hadBond = NimBLEDevice::isBonded(addr);
        NimBLEDevice::deleteBond(addr);
        if (hadBond && !sBondRetried) {
            // The radio forgot us (reflashed or reset): pair again from scratch.
            sBondRetried = true;
            dropLink("Old pairing rejected, pairing again", false);
            _reconnectAt = millis() + 1000;
        } else {
            dropLink("Pairing failed - check the PIN", false);
        }
    }

    int reason = _flagDisconnected.exchange(0);
    if (reason != 0 && (_state == LinkState::Connecting || _state == LinkState::Pairing ||
                        _state == LinkState::Syncing || _state == LinkState::Ready)) {
        sToRadio = sFromRadio = sFromNum = nullptr;
        dropLink("Link lost (" + std::to_string(reason) + ")", true);
    }

    // Only meaningful while a link is being set up; a late flag must not open
    // the PIN prompt over a dropped link.
    if (_flagPasskey.exchange(false) &&
        (_state == LinkState::Connecting || _state == LinkState::Syncing)) {
        _pinHandle = _flagPinHandle.load();
        _status = "Enter the radio's PIN";
        setState(LinkState::Pairing);
    }

    if (_flagScanEnded.exchange(false) && _state == LinkState::Scanning) {
        _status = "Scan finished";
        setState(LinkState::Idle);
    }
}

// ---- link management ----------------------------------------------------

bool MeshtasticClient::hasSavedRadio() const {
    return !Settings::get().radioAddr.empty();
}

uint32_t MeshtasticClient::reconnectInMs() const {
    if (_reconnectAt == 0) return 0;
    int32_t left = static_cast<int32_t>(_reconnectAt - millis());
    return left > 0 ? static_cast<uint32_t>(left) : 0;
}

void MeshtasticClient::startConnect() {
    _reconnectAt = 0;
    if (_radio.addr.empty() || !bleUp()) return;
    if (_state == LinkState::Scanning) NimBLEDevice::getScan()->stop();

    _flagDisconnected = 0;  // anything pending belongs to the previous link
    _flagPasskey = false;
    _flagAuth = 0;
    NimBLEAddress addr(_radio.addr, _radio.addrType);
    _status = "Connecting to " + (_radio.name.empty() ? _radio.addr : _radio.name);
    setState(LinkState::Connecting);
    sClient->setConnectionParams(kFastIntervalMin, kFastIntervalMax, 0, kSupervisionTimeout);
    if (!sClient->connect(addr, true, true, true)) {
        dropLink("Connect failed to start", true);
    }
}

void MeshtasticClient::dropLink(const std::string& why, bool allowRetry) {
    _status = why;
    LOG("MESH", "drop link: %s", why.c_str());
    if (sClient && (sClient->isConnected() || _state == LinkState::Connecting)) {
        if (sClient->isConnected()) sClient->disconnect();
        else sClient->cancelConnect();
    }
    sToRadio = sFromRadio = sFromNum = nullptr;
    _pinHandle = 0xFFFF;
    if (_ownerUpdate == OwnerUpdate::Working) {
        _ownerUpdate = OwnerUpdate::Failed;
        _ownerMsg = "Radio link lost";
        _ownerStep = OwnerStep::None;
    }
    setState(_bleUp ? LinkState::Idle : LinkState::Off);

    bool saved = hasSavedRadio() && Settings::get().radioAddr == _radio.addr;
    if (allowRetry && !_userDisconnected && saved && Settings::get().autoConnect) {
        scheduleReconnect();
    } else {
        _reconnectAt = 0;
    }
}

void MeshtasticClient::scheduleReconnect() {
    uint32_t now = millis();
    if (_failingSince == 0) _failingSince = now;
    _backoffMs = _backoffMs == 0 ? 2000 : _backoffMs * 2;
    if (_backoffMs > kMaxBackoffMs) _backoffMs = kMaxBackoffMs;
    if (now - _failingSince > kLongOutageMs) _backoffMs = kLongOutageBackoffMs;
    _reconnectAt = now + _backoffMs;
    if (_reconnectAt == 0) _reconnectAt = 1;
    LOG("MESH", "reconnect in %lu ms", static_cast<unsigned long>(_backoffMs));
}

void MeshtasticClient::connect(const RadioInfo& radio) {
    if (_state == LinkState::Connecting || _state == LinkState::Pairing ||
        _state == LinkState::Syncing || _state == LinkState::Ready) {
        // Let the old link finish closing before dialing the new radio.
        dropLink("Switching radio", false);
        _radio = radio;
        _userDisconnected = false;
        _wasReady = (radio.addr == Settings::get().radioAddr);
        _backoffMs = 0;
        _failingSince = 0;
        sBondRetried = false;
        _reconnectAt = millis() + 500;
        return;
    }
    _radio = radio;
    _userDisconnected = false;
    _wasReady = (radio.addr == Settings::get().radioAddr);
    _backoffMs = 0;
    _failingSince = 0;
    sBondRetried = false;
    startConnect();
}

void MeshtasticClient::reconnectNow() {
    const Settings& s = Settings::get();
    if (s.radioAddr.empty()) return;
    if (_state != LinkState::Idle && _state != LinkState::Off && _state != LinkState::Scanning) return;
    _radio.addr = s.radioAddr;
    _radio.addrType = s.radioAddrType;
    _radio.name = s.radioName;
    _userDisconnected = false;
    _backoffMs = 0;
    _failingSince = 0;
    sBondRetried = false;
    startConnect();
}

void MeshtasticClient::disconnect() {
    _userDisconnected = true;
    _reconnectAt = 0;
    if (_state == LinkState::Ready) {
        uint8_t buf[16];
        size_t n = MeshProto::encodeDisconnect(buf, sizeof(buf));
        writeToRadio(buf, n);
    }
    if (_state == LinkState::Scanning) stopScan();
    if (_state != LinkState::Idle && _state != LinkState::Off) dropLink("Disconnected", false);
}

void MeshtasticClient::forgetRadio() {
    disconnect();
    if (!_radio.addr.empty() && bleUp()) {
        NimBLEDevice::deleteBond(NimBLEAddress(_radio.addr, _radio.addrType));
    }
    Settings& s = Settings::get();
    s.radioAddr.clear();
    s.radioName.clear();
    s.radioAddrType = 0;
    s.save();
    _radio = RadioInfo();
    _wasReady = false;
    _status = "Radio forgotten";
    ++_revision;
}

void MeshtasticClient::submitPin(uint32_t pin) {
    if (_state != LinkState::Pairing || !sClient) return;
    NimBLEConnInfo info = sClient->getConnInfo();
    if (!NimBLEDevice::injectPassKey(info, pin)) {
        dropLink("Could not send PIN", false);
        return;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%06lu", static_cast<unsigned long>(pin));
    Settings& s = Settings::get();
    if (s.blePin != buf) {
        s.blePin = buf;
        s.save();
    }
    _syncStep = SyncStep::Securing;
    _status = "Checking PIN";
    setState(LinkState::Syncing);
}

void MeshtasticClient::cancelPin() {
    if (_state == LinkState::Pairing) dropLink("Pairing cancelled", false);
}

// ---- scanning -------------------------------------------------------------

void MeshtasticClient::startScan() {
    if (_state != LinkState::Idle && _state != LinkState::Off) return;
    if (!bleUp()) return;
    {
        std::lock_guard<std::mutex> lock(_scanMutex);
        _scan.clear();
    }
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&sScanCallbacks, false);
    scan->setActiveScan(true);   // radios put their name in the scan response
    scan->setInterval(100);
    scan->setWindow(60);
    scan->setDuplicateFilter(false);
    if (!scan->start(kScanMs, false, true)) {
        _status = "Scan failed to start";
        return;
    }
    _status = "Scanning for radios";
    setState(LinkState::Scanning);
}

void MeshtasticClient::stopScan() {
    if (_bleUp) NimBLEDevice::getScan()->stop();
    if (_state == LinkState::Scanning) setState(LinkState::Idle);
}

std::vector<RadioInfo> MeshtasticClient::scanResults() const {
    std::lock_guard<std::mutex> lock(_scanMutex);
    std::vector<RadioInfo> out = _scan;
    std::sort(out.begin(), out.end(), [](const RadioInfo& a, const RadioInfo& b) { return a.rssi > b.rssi; });
    return out;
}

// ---- NimBLE callbacks (host task) ----------------------------------------

void MeshtasticClient::bleConnected() { _flagConnected = true; }
void MeshtasticClient::bleConnectFailed(int reason) { _flagConnectFail = reason != 0 ? reason : -1; }
void MeshtasticClient::bleDisconnected(int reason) { _flagDisconnected = reason != 0 ? reason : -1; }
void MeshtasticClient::blePasskeyRequested(uint16_t handle) { _flagPinHandle = handle; _flagPasskey = true; }
void MeshtasticClient::bleAuthComplete(bool encrypted) { _flagAuth = encrypted ? 1 : -1; }
void MeshtasticClient::bleScanEnded() { _flagScanEnded = true; }
void MeshtasticClient::bleFromNum() { _flagPull = true; }

void MeshtasticClient::bleScanResult(const RadioInfo& r) {
    std::lock_guard<std::mutex> lock(_scanMutex);
    for (auto& existing : _scan) {
        if (existing.addr == r.addr) {
            if (!r.name.empty()) existing.name = r.name;
            existing.rssi = r.rssi;
            return;
        }
    }
    if (_scan.size() < kMaxScanResults) _scan.push_back(r);
}

// ---- sync and frames -------------------------------------------------------

bool MeshtasticClient::discoverAndRequestConfig() {
    NimBLERemoteService* svc = sClient ? sClient->getService(kServiceUuid) : nullptr;
    if (!svc) {
        _status = "Not a Meshtastic radio";
        return false;
    }
    sToRadio = svc->getCharacteristic(kToRadioUuid);
    sFromRadio = svc->getCharacteristic(kFromRadioUuid);
    sFromNum = svc->getCharacteristic(kFromNumUuid);
    if (!sToRadio || !sFromRadio || !sFromNum) {
        _status = "Radio is missing mesh characteristics";
        return false;
    }
    if (sFromNum->canNotify()) {
        sFromNum->subscribe(true, [](NimBLERemoteCharacteristic*, uint8_t*, size_t, bool) {
            gMesh.bleFromNum();
        }, true);
    }

    // 69420 / 69421 ask for partial dumps; anything else means "everything".
    do { _configNonce = esp_random(); } while (_configNonce < 100000);
    uint8_t buf[32];
    size_t n = MeshProto::encodeWantConfig(_configNonce, buf, sizeof(buf));
    if (!writeToRadio(buf, n)) {
        _status = "Radio refused config request";
        return false;
    }
    _channels.clear();
    _syncStep = SyncStep::Downloading;
    _status = "Syncing node list";
    setState(LinkState::Syncing);
    return true;
}

void MeshtasticClient::pullFrames(int maxFrames) {
    if (!sFromRadio || !sClient || !sClient->isConnected()) return;
    // Each read costs a BLE round trip; cap the time so keys stay responsive.
    constexpr uint32_t kPullBudgetMs = 120;
    uint32_t start = millis();
    _lastPollMs = start;
    for (int i = 0; i < maxFrames && (i == 0 || millis() - start < kPullBudgetMs); ++i) {
        NimBLEAttValue v = sFromRadio->readValue();
        if (v.size() == 0) return;
        handleFrame(v.data(), v.size());
        if (!sFromRadio) return;  // link dropped while handling
    }
    _flagPull = true;  // maybe more queued; continue next loop
}

bool MeshtasticClient::syncFailureIsTransient() const {
    return _status != "Not a Meshtastic radio";
}

MeshNode& MeshtasticClient::upsertNode(uint32_t num) {
    for (auto& n : _nodes) if (n.num == num) return n;
    if (_nodes.size() >= kMaxNodes) {
        // Evict the node heard least recently (never ourselves).
        size_t victim = 0;
        for (size_t i = 0; i < _nodes.size(); ++i) {
            if (_nodes[i].num == _myNode) continue;
            if (_nodes[victim].num == _myNode || _nodes[i].lastHeard < _nodes[victim].lastHeard) victim = i;
        }
        _nodes.erase(_nodes.begin() + victim);
    }
    MeshNode n;
    n.num = num;
    _nodes.push_back(n);
    return _nodes.back();
}

void MeshtasticClient::pushEvent(const MeshProto::Event& ev) {
    if (_events.size() >= kMaxEvents) _events.pop_front();
    _events.push_back(ev);
}

bool MeshtasticClient::popEvent(MeshProto::Event& out) {
    if (_events.empty()) return false;
    out = std::move(_events.front());
    _events.pop_front();
    return true;
}

void MeshtasticClient::handleFrame(const uint8_t* data, size_t len) {
    static MeshProto::Event ev;
    if (!MeshProto::decodeFromRadio(data, len, ev)) {
        LOG("MESH", "undecodable frame (%u bytes)", static_cast<unsigned>(len));
        return;
    }
    using MeshProto::EventKind;
    switch (ev.kind) {
    case EventKind::MyInfo:
        if (_myNode != 0 && _myNode != ev.myNodeNum) _nodes.clear();  // a different radio
        _myNode = ev.myNodeNum;
        LOG("MESH", "my node %08lx", static_cast<unsigned long>(_myNode));
        ++_revision;
        break;

    case EventKind::NodeInfo: {
        MeshNode& n = upsertNode(ev.node.num);
        uint32_t heard = n.heardMs;
        MeshProto::User keep = n.user;
        bool hadUser = n.hasUser;
        static_cast<MeshProto::NodeInfo&>(n) = ev.node;
        if (!ev.node.hasUser && hadUser) { n.hasUser = true; n.user = keep; }
        n.heardMs = heard;
        // Other nodes' timestamps can come from wrong clocks; only our own radio's counts.
        if (ev.node.num == _myNode) noteEpoch(ev.node.lastHeard);
        ++_revision;
        break;
    }

    case EventKind::Channel: {
        bool found = false;
        for (auto& c : _channels) {
            if (c.index == ev.channel.index) { c = ev.channel; found = true; break; }
        }
        if (!found) _channels.push_back(ev.channel);
        std::sort(_channels.begin(), _channels.end(),
                  [](const MeshProto::Channel& a, const MeshProto::Channel& b) { return a.index < b.index; });
        ++_revision;
        break;
    }

    case EventKind::ConfigComplete:
        if (_state == LinkState::Syncing && ev.configId == _configNonce) becomeReady();
        break;

    case EventKind::UserUpdate: {
        MeshNode& n = upsertNode(ev.node.num);
        n.hasUser = true;
        n.user = ev.node.user;
        n.snr = ev.node.snr;
        n.channel = ev.node.channel;
        n.heardMs = millis();
        LOG("MESH", "identity %08lx is now '%s' (%s)", static_cast<unsigned long>(n.num),
            n.user.longName.c_str(), n.user.shortName.c_str());
        ++_revision;
        break;
    }

    case EventKind::Text:
        if (ev.text.from != 0) {
            MeshNode& n = upsertNode(ev.text.from);
            n.heardMs = millis();
            if (ev.text.rxTime) n.lastHeard = ev.text.rxTime;
        }
        noteEpoch(ev.text.rxTime);
        LOG("MESH", "text from %08lx to %08lx ch%u (%u bytes)", static_cast<unsigned long>(ev.text.from),
            static_cast<unsigned long>(ev.text.to), ev.text.channel, static_cast<unsigned>(ev.text.text.size()));
        pushEvent(ev);
        break;

    case EventKind::Routing:
        LOG("MESH", "routing for %08lx from %08lx error %d", static_cast<unsigned long>(ev.routing.requestId),
            static_cast<unsigned long>(ev.routing.from), ev.routing.error);
        pushEvent(ev);
        break;

    case EventKind::QueueStatus:
        if (ev.queue.res != 0) pushEvent(ev);
        break;

    case EventKind::Admin:
        if (ev.admin.from != _myNode && ev.admin.from != 0) break;
        if (ev.admin.hasOwner) {
            MeshNode& me = upsertNode(_myNode);
            me.hasUser = true;
            me.user.longName = ev.admin.owner.longName;
            me.user.shortName = ev.admin.owner.shortName;
            me.user.isLicensed = ev.admin.owner.isLicensed;
            ++_revision;
        }
        if (_ownerStep == OwnerStep::AwaitKey) {
            sendSetOwner(ev.admin.sessionPasskey);
        } else if (_ownerStep == OwnerStep::AwaitVerify && ev.admin.hasOwner) {
            _ownerStep = OwnerStep::None;
            if (ev.admin.owner.longName == _ownerLong && ev.admin.owner.shortName == _ownerShort) {
                _ownerUpdate = OwnerUpdate::Done;
                _ownerMsg = "Saved on radio";
            } else {
                _ownerUpdate = OwnerUpdate::Failed;
                _ownerMsg = "Radio kept the old name";
            }
            LOG("MESH", "owner update: %s", _ownerMsg.c_str());
            ++_revision;
        }
        break;

    case EventKind::Rebooted:
        LOG("MESH", "radio rebooted");
        break;

    case EventKind::Notification:
        LOG("MESH", "radio says: %s", ev.message.c_str());
        pushEvent(ev);
        break;

    case EventKind::ModemPreset:
        if (ev.hopLimit != 0) _hopLimit = ev.hopLimit;
        if (_presetName != ev.message) {
            _presetName = ev.message;
            ++_revision;
        }
        break;

    case EventKind::Ignored:
        break;
    }
}

void MeshtasticClient::becomeReady() {
    _wasReady = true;
    _backoffMs = 0;
    _failingSince = 0;
    sBondRetried = false;
    _lastPollMs = _lastHeartbeatMs = millis();
    _status = "Connected";
    setState(LinkState::Ready);

    // Synced: drop to a slow connection interval to save power.
    if (sClient) sClient->updateConnParams(kSlowIntervalMin, kSlowIntervalMax, 0, kSupervisionTimeout);

    Settings& s = Settings::get();
    if (s.radioAddr != _radio.addr || s.radioAddrType != _radio.addrType ||
        s.radioName != _radio.name || s.lastNodeNum != _myNode) {
        s.radioAddr = _radio.addr;
        s.radioAddrType = _radio.addrType;
        s.radioName = _radio.name;
        s.lastNodeNum = _myNode;
        s.save();
    }
    LOG("MESH", "ready: %u nodes, %u channels", static_cast<unsigned>(_nodes.size()),
        static_cast<unsigned>(_channels.size()));
}

bool MeshtasticClient::writeToRadio(const uint8_t* buf, size_t len) {
    if (!sToRadio || len == 0) return false;
    return sToRadio->writeValue(buf, len, true);
}

// ---- messaging ---------------------------------------------------------------

uint32_t MeshtasticClient::sendText(uint32_t to, uint8_t channel, const std::string& utf8) {
    if (!ready()) return 0;
    static uint8_t buf[MeshProto::kMaxToRadioBytes];
    uint32_t id = newPacketId();
    size_t n = MeshProto::encodeText(id, to, channel, utf8, true, buf, sizeof(buf), _hopLimit);
    if (n == 0 || !writeToRadio(buf, n)) return 0;
    LOG("MESH", "sent %08lx to %08lx ch%u", static_cast<unsigned long>(id), static_cast<unsigned long>(to), channel);
    return id;
}

// ---- identity ------------------------------------------------------------------

bool MeshtasticClient::setOwner(const std::string& longName, const std::string& shortName) {
    if (!ready()) {
        _ownerUpdate = OwnerUpdate::Failed;
        _ownerMsg = "Connect a radio first";
        return false;
    }
    if (longName.empty() || longName.size() > MeshProto::kMaxLongNameBytes ||
        shortName.empty() || shortName.size() > MeshProto::kMaxShortNameBytes) {
        _ownerUpdate = OwnerUpdate::Failed;
        _ownerMsg = "Name 1-39 chars, short 1-4";
        return false;
    }
    _ownerLong = longName;
    _ownerShort = shortName;
    _ownerUpdate = OwnerUpdate::Working;
    _ownerMsg = "Updating radio...";
    ++_revision;

    // Ask for the owner first: the reply carries the admin session passkey
    // that newer firmware wants on every set_* request.
    uint8_t buf[MeshProto::kMaxToRadioBytes];
    size_t n = MeshProto::encodeGetOwner(newPacketId(), _myNode, buf, sizeof(buf));
    if (!writeToRadio(buf, n)) {
        _ownerUpdate = OwnerUpdate::Failed;
        _ownerMsg = "Radio write failed";
        return false;
    }
    _ownerStep = OwnerStep::AwaitKey;
    _ownerAt = millis();
    return true;
}

void MeshtasticClient::sendSetOwner(const std::vector<uint8_t>& passkey) {
    const MeshNode* me = node(_myNode);
    bool licensed = me && me->user.isLicensed;
    uint8_t buf[MeshProto::kMaxToRadioBytes];
    size_t n = MeshProto::encodeSetOwner(newPacketId(), _myNode, _ownerLong, _ownerShort,
                                         licensed, passkey, buf, sizeof(buf));
    if (!writeToRadio(buf, n)) {
        _ownerStep = OwnerStep::None;
        _ownerUpdate = OwnerUpdate::Failed;
        _ownerMsg = "Radio write failed";
        ++_revision;
        return;
    }
    LOG("MESH", "set_owner sent (passkey %u bytes)", static_cast<unsigned>(passkey.size()));
    _ownerStep = OwnerStep::Settle;
    _ownerAt = millis();
}

void MeshtasticClient::stepOwnerUpdate() {
    uint32_t elapsed = millis() - _ownerAt;
    switch (_ownerStep) {
    case OwnerStep::AwaitKey:
        if (elapsed > 4000) sendSetOwner({});  // older firmware: no passkey needed
        break;
    case OwnerStep::Settle:
        if (elapsed > 1500) {
            uint8_t buf[MeshProto::kMaxToRadioBytes];
            size_t n = MeshProto::encodeGetOwner(newPacketId(), _myNode, buf, sizeof(buf));
            writeToRadio(buf, n);
            _ownerStep = OwnerStep::AwaitVerify;
            _ownerAt = millis();
        }
        break;
    case OwnerStep::AwaitVerify:
        if (elapsed > 6000) {
            _ownerStep = OwnerStep::None;
            _ownerUpdate = OwnerUpdate::Failed;
            _ownerMsg = "Radio did not confirm";
            ++_revision;
        }
        break;
    case OwnerStep::None:
        break;
    }
}

// ---- lookups ---------------------------------------------------------------------

void MeshtasticClient::loadDemoNodes() {
    if (epochNow() == 0) noteEpoch(1792000000);  // no radio clock yet: any fixed "now"
    uint32_t now = epochNow();
    std::vector<MeshNode> keep;
    for (const auto& n : _nodes) if (n.num == _myNode && _myNode != 0) keep.push_back(n);
    _nodes = keep;

    struct Demo { uint32_t num; const char* longName; const char* shortName; uint32_t hw; uint32_t role;
                  int hops; float snr; uint32_t battery; uint32_t ago; };
    const Demo demo[] = {
        {0x1a2b3c4d, "Ana Cardputer", "ANA", 112, 0, 0, 9.5f, 84, 60},
        {0x5e6f7a8b, "Ridge Relay", "RDG", 9, 2, 1, -3.2f, 101, 600},
        {0x9c0d1e2f, "Trail Base", "BASE", 43, 0, 2, -11.0f, 63, 3600},
        {0x3141a5b9, "Sam on bike", "SAM", 7, 0, 1, 1.5f, 47, 7200},
        {0x27182818, "Hut Weather", "HUT", 31, 6, 3, -14.5f, 101, 172800},
    };
    uint32_t ms = millis();
    for (const auto& d : demo) {
        MeshNode n;
        n.num = d.num;
        n.hasUser = true;
        n.user.id = MeshProto::nodeIdString(d.num);
        n.user.longName = d.longName;
        n.user.shortName = d.shortName;
        n.user.hwModel = d.hw;
        n.user.role = d.role;
        n.user.hasPublicKey = true;
        n.hasHopsAway = true;
        n.hopsAway = static_cast<uint8_t>(d.hops);
        n.snr = d.snr;
        n.hasBattery = true;
        n.batteryLevel = d.battery;
        n.lastHeard = now - d.ago;
        n.heardMs = d.ago < 3600 ? ms - d.ago * 1000 : 0;
        _nodes.push_back(n);
    }
    ++_revision;
}

void MeshtasticClient::noteEpoch(uint32_t epoch) {
    if (epoch == 0 || epoch <= epochNow()) return;
    _epochRef = epoch;
    _epochRefMs = millis();
}

uint32_t MeshtasticClient::epochNow() const {
    if (_epochRef == 0) return 0;
    return _epochRef + (millis() - _epochRefMs) / 1000;
}

const MeshNode* MeshtasticClient::node(uint32_t num) const {
    for (const auto& n : _nodes) if (n.num == num) return &n;
    return nullptr;
}

std::string MeshtasticClient::nodeName(uint32_t num) const {
    if (num == MeshProto::kBroadcast) return "Everyone";
    const MeshNode* n = node(num);
    if (n && n->hasUser) {
        if (!n->user.longName.empty()) return n->user.longName;
        if (!n->user.shortName.empty()) return n->user.shortName;
    }
    return MeshProto::nodeIdString(num);
}

std::string MeshtasticClient::nodeShortName(uint32_t num) const {
    const MeshNode* n = node(num);
    if (n && n->hasUser && !n->user.shortName.empty()) return n->user.shortName;
    return MeshProto::nodeIdString(num).substr(5);
}

std::string MeshtasticClient::channelName(uint8_t index) const {
    for (const auto& c : _channels) {
        if (c.index == index && !c.name.empty()) return c.name;
    }
    // Meshtastic names an unnamed primary channel after its modem preset.
    if (index == 0) return _presetName.empty() ? "Primary" : _presetName;
    return "Channel " + std::to_string(index);
}
