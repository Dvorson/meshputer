#pragma once

// BLE link to a Meshtastic radio: scan, pair, sync the node database, send and
// receive. All public methods are called from the Arduino loop task; NimBLE
// callbacks (host task) only raise flags that loop() consumes, so state is
// never mutated concurrently.
//
// The Bluetooth controller is powered only while it is needed: scanning,
// connecting, connected, or waiting to reconnect to the saved radio.

#include <Arduino.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "core/mesh_protocol.h"

enum class LinkState : uint8_t {
    Off,         // Bluetooth controller powered down
    Idle,        // powered, no link (maybe waiting to reconnect)
    Scanning,
    Connecting,
    Pairing,     // radio asked for its PIN
    Syncing,     // securing, discovering, downloading the node database
    Ready,
};

const char* linkStateLabel(LinkState s);

struct RadioInfo {
    std::string name;
    std::string addr;   // "aa:bb:cc:dd:ee:ff"
    uint8_t addrType = 0;
    int rssi = 0;
};

struct MeshNode : MeshProto::NodeInfo {
    uint32_t heardMs = 0;  // millis() when we last saw traffic from it
};

enum class OwnerUpdate : uint8_t { Idle, Working, Done, Failed };

class MeshtasticClient {
public:
    void begin();
    void loop();

    // Discovery
    void startScan();
    void stopScan();
    std::vector<RadioInfo> scanResults() const;

    // Link
    void connect(const RadioInfo& radio);
    void disconnect();     // stays disconnected until connect()/reconnectNow()
    void reconnectNow();
    void forgetRadio();    // disconnect, drop the pairing and the saved radio
    void submitPin(uint32_t pin);
    void cancelPin();

    LinkState state() const { return _state; }
    bool ready() const { return _state == LinkState::Ready; }
    const std::string& status() const { return _status; }
    const RadioInfo& radio() const { return _radio; }
    bool hasSavedRadio() const;
    uint32_t reconnectInMs() const;

    // Node database. Kept after link loss so names stay readable offline.
    uint32_t myNodeNum() const { return _myNode; }
    const MeshNode* node(uint32_t num) const;
    const std::vector<MeshNode>& nodes() const { return _nodes; }
    const std::vector<MeshProto::Channel>& channels() const { return _channels; }
    std::string nodeName(uint32_t num) const;       // long name > short name > !id
    std::string nodeShortName(uint32_t num) const;  // short name > last 4 id digits
    std::string channelName(uint8_t index) const;
    uint32_t revision() const { return _revision; } // bumps on any visible change

    // Screenshot/demo mode: replaces the in-memory node list (not our own node)
    // with fictional nodes. Nothing is sent; the next radio sync restores it.
    void loadDemoNodes();

    // Best guess at the current epoch time (the Cardputer has no RTC): the
    // newest timestamp from our radio's own clock, advanced by local uptime. 0 = unknown.
    uint32_t epochNow() const;

    // Returns the packet id, or 0 if the radio did not take it.
    uint32_t sendText(uint32_t to, uint8_t channel, const std::string& utf8);

    // Owner (our identity on the mesh). Async; poll ownerUpdate().
    bool setOwner(const std::string& longName, const std::string& shortName);
    OwnerUpdate ownerUpdate() const { return _ownerUpdate; }
    const std::string& ownerMessage() const { return _ownerMsg; }

    // Text, Routing, QueueStatus and Notification events for the chat layer.
    bool popEvent(MeshProto::Event& out);

    // NimBLE callback entry points (BLE host task): flags only.
    void bleConnected();
    void bleConnectFailed(int reason);
    void bleDisconnected(int reason);
    void blePasskeyRequested(uint16_t connHandle);
    void bleAuthComplete(bool encrypted);
    void bleScanResult(const RadioInfo& r);
    void bleScanEnded();
    void bleFromNum();

private:
    enum class SyncStep : uint8_t { Securing, Downloading };
    enum class OwnerStep : uint8_t { None, AwaitKey, Settle, AwaitVerify };

    bool bleUp();
    void bleDown();
    bool wantsBle() const;
    void startConnect();
    void dropLink(const std::string& why, bool allowRetry);
    void scheduleReconnect();
    void processBleFlags();
    bool discoverAndRequestConfig();
    void pullFrames(int maxFrames);
    void handleFrame(const uint8_t* data, size_t len);
    void becomeReady();
    void stepOwnerUpdate();
    void sendSetOwner(const std::vector<uint8_t>& passkey);
    bool writeToRadio(const uint8_t* buf, size_t len);
    MeshNode& upsertNode(uint32_t num);
    void pushEvent(const MeshProto::Event& ev);
    void setState(LinkState s);
    void noteEpoch(uint32_t epoch);
    bool syncFailureIsTransient() const;

    LinkState _state = LinkState::Off;
    SyncStep _syncStep = SyncStep::Securing;
    std::string _status;
    RadioInfo _radio;
    bool _bleUp = false;
    bool _userDisconnected = false;
    bool _wasReady = false;          // this radio synced at least once
    uint32_t _stateSince = 0;
    uint32_t _reconnectAt = 0;       // 0 = no reconnect scheduled
    uint32_t _backoffMs = 0;
    uint32_t _failingSince = 0;
    uint32_t _configNonce = 0;
    uint32_t _lastPollMs = 0;
    uint32_t _lastHeartbeatMs = 0;
    uint16_t _pinHandle = 0xFFFF;

    // Flags raised by NimBLE callbacks.
    std::atomic<bool> _flagConnected{false};
    std::atomic<int> _flagConnectFail{0};
    std::atomic<int> _flagDisconnected{0};
    std::atomic<bool> _flagPasskey{false};
    std::atomic<int> _flagAuth{0};      // 1 = encrypted, -1 = failed
    std::atomic<bool> _flagScanEnded{false};
    std::atomic<bool> _flagPull{false};
    std::atomic<uint16_t> _flagPinHandle{0xFFFF};

    mutable std::mutex _scanMutex;
    std::vector<RadioInfo> _scan;

    uint32_t _myNode = 0;
    std::vector<MeshNode> _nodes;
    std::vector<MeshProto::Channel> _channels;
    uint32_t _revision = 0;
    uint8_t _hopLimit = 3;     // from the radio's LoRa config
    std::string _presetName;   // shown for a primary channel without a name
    uint32_t _epochRef = 0;
    uint32_t _epochRefMs = 0;
    std::deque<MeshProto::Event> _events;

    OwnerUpdate _ownerUpdate = OwnerUpdate::Idle;
    OwnerStep _ownerStep = OwnerStep::None;
    std::string _ownerMsg;
    std::string _ownerLong, _ownerShort;
    uint32_t _ownerAt = 0;
};

extern MeshtasticClient gMesh;
