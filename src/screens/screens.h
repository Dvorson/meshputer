#pragma once

// Screens and the navigation stack.
//
//   Home ─┬─ Radio (scan / connect / disconnect)   PIN prompt pops up over
//         ├─ Channels ── Chat                       any screen while pairing
//         ├─ Nodes ── Node info ── Chat
//         ├─ <conversation> ── Chat ─┬─ Canned replies
//         │                          └─ Emoji
//         └─ Settings (identity, sound, display, radio)

#include <cstdint>
#include <string>
#include <vector>

#include "board.h"
#include "core/conversations.h"
#include "meshtastic_client.h"
#include "ui.h"

class Screen {
public:
    virtual ~Screen() = default;
    virtual const char* name() const = 0;
    virtual void show() {}  // pushed, or returned to
    virtual void hide() {}  // covered by another screen, or popped
    virtual void key(const KeyInput& k) = 0;
    virtual void tick() {}
    virtual void draw(M5Canvas& c) = 0;

    void invalidate() { _dirty = true; }
    bool takeDirty() { bool d = _dirty; _dirty = false; return d; }

private:
    bool _dirty = true;
};

namespace Nav {
void reset(Screen* root);
// Pushing a screen already on the stack returns to it instead of stacking a copy.
void push(Screen* s);
void pop();
Screen* current();
}

class HomeScreen : public Screen {
public:
    const char* name() const override { return "home"; }
    void show() override;
    void key(const KeyInput& k) override;
    void tick() override;
    void draw(M5Canvas& c) override;

private:
    enum class Kind : uint8_t { Radio, Channels, Nodes, Conversation, Settings };
    struct Entry { Kind kind; int conv; };
    void rebuild();
    std::vector<Entry> _entries;
    ListView _list;
    uint32_t _seenChat = 0xFFFFFFFF;
    uint32_t _seenMesh = 0xFFFFFFFF;
};

class RadioScreen : public Screen {
public:
    const char* name() const override { return "radio"; }
    void show() override;
    void key(const KeyInput& k) override;
    void tick() override;
    void draw(M5Canvas& c) override;

private:
    enum class Action : uint8_t { Scan, Connect, Reconnect, Disconnect, Forget };
    struct Item { Action action; std::string label; std::string right; int scanIdx; };
    void rebuild();
    std::vector<Item> _items;
    std::vector<RadioInfo> _results;
    ListView _list;
    bool _connectRequested = false;
    uint32_t _seenMesh = 0xFFFFFFFF;
    uint32_t _lastPollMs = 0;
};

class PinScreen : public Screen {
public:
    const char* name() const override { return "pin"; }
    void show() override;
    void key(const KeyInput& k) override;
    void tick() override;
    void draw(M5Canvas& c) override;

private:
    LineEdit _pin;
};

class NodesScreen : public Screen {
public:
    const char* name() const override { return "nodes"; }
    void show() override;
    void key(const KeyInput& k) override;
    void tick() override;
    void draw(M5Canvas& c) override;

private:
    void rebuild();
    std::vector<uint32_t> _nodes;
    ListView _list;
    uint32_t _seenMesh = 0xFFFFFFFF;
};

class NodeInfoScreen : public Screen {
public:
    const char* name() const override { return "node"; }
    void open(uint32_t num) { _num = num; _scroll = 0; }
    void key(const KeyInput& k) override;
    void tick() override;
    void draw(M5Canvas& c) override;

private:
    uint32_t _num = 0;
    int _scroll = 0;
    uint32_t _seenMesh = 0xFFFFFFFF;
};

class ChannelsScreen : public Screen {
public:
    const char* name() const override { return "channels"; }
    void show() override;
    void key(const KeyInput& k) override;
    void draw(M5Canvas& c) override;

private:
    std::vector<uint8_t> active();
    ListView _list;
};

class ChatScreen : public Screen {
public:
    const char* name() const override { return "chat"; }
    void open(int convIdx);
    void show() override;
    void hide() override;
    void key(const KeyInput& k) override;
    void tick() override;
    void draw(M5Canvas& c) override;

    int conversation() const { return _conv; }
    void insert(const std::string& utf8);
    void sendNow(const std::string& utf8);

private:
    void drawMessages(M5Canvas& c);
    void drawInput(M5Canvas& c);
    bool resolve();  // re-finds _conv by key; history reloads shift indices
    int _conv = -1;
    ConversationKey _key;
    LineEdit _input;
    int _scroll = 0;  // messages hidden below the bottom edge
    std::string _notice;
    uint32_t _noticeAt = 0;
    uint32_t _seenChat = 0xFFFFFFFF;
};

class CannedScreen : public Screen {
public:
    const char* name() const override { return "canned"; }
    void show() override { _list = ListView(); }
    void key(const KeyInput& k) override;
    void draw(M5Canvas& c) override;

private:
    ListView _list;
};

class EmojiScreen : public Screen {
public:
    const char* name() const override { return "emoji"; }
    void key(const KeyInput& k) override;
    void draw(M5Canvas& c) override;

private:
    int _sel = 0;
    int _topRow = 0;
};

class SettingsScreen : public Screen {
public:
    const char* name() const override { return "settings"; }
    void show() override;
    void key(const KeyInput& k) override;
    void tick() override;
    void draw(M5Canvas& c) override;

private:
    void activate(int row);
    void commitEdit();
    ListView _list;
    int _editing = -1;
    LineEdit _edit;
    std::string _notice;
    uint32_t _noticeAt = 0;
    uint32_t _seenMesh = 0xFFFFFFFF;
};

extern HomeScreen gHomeScreen;
extern RadioScreen gRadioScreen;
extern PinScreen gPinScreen;
extern NodesScreen gNodesScreen;
extern NodeInfoScreen gNodeInfoScreen;
extern ChannelsScreen gChannelsScreen;
extern ChatScreen gChatScreen;
extern CannedScreen gCannedScreen;
extern EmojiScreen gEmojiScreen;
extern SettingsScreen gSettingsScreen;

// Opens the chat for a conversation (from any screen).
void openChat(int convIdx);
// "5m", "3h", "2d" since an epoch timestamp from the radio; "" if unknown.
std::string ageLabel(uint32_t epochSeconds);
