#pragma once

// Tab5 touch UI: four destinations (Chats, Nodes, Radio, Settings), each a
// list with a detail page. Landscape shows a navigation rail, the list and the
// detail side by side; portrait shows one page at a time with a bottom bar,
// and opening an item pushes its detail page.
//
// What is on screen is plain data (Route), so the whole UI can be rebuilt from
// it at any time: when the device turns, or the theme changes. Navigation only
// edits the route and schedules a rebuild, which runs after the current LVGL
// event has returned (an event handler never deletes its own widget).

#include <lvgl.h>

#include <cstdint>
#include <string>

#include "core/conversations.h"

namespace Tab5 {

enum class Tab : uint8_t { Chats, Nodes, Radio, Settings };
constexpr int kTabCount = 4;

struct Route {
    Tab tab = Tab::Chats;
    bool detail = false;     // portrait: the detail page covers the list
    bool hasConv = false;    // Chats: selected conversation
    ConversationKey conv;
    bool newChat = false;    // Chats: the "new internet chat" form instead of a conversation
    uint32_t node = 0;       // Nodes: selected node, 0 = none
    int section = -1;        // Settings: selected section, -1 = none
};

Route& route();
bool landscape();

// Parts of the screen to rebuild.
enum Part : uint8_t { kList = 1, kDetail = 2, kShell = 4, kAll = 7 };
void invalidate(uint8_t parts);

// Navigation (applied after the current event).
void showTab(Tab tab);
void openConversation(int convIdx);
void openNewChat();
void openNode(uint32_t num);
void openSection(int section);
void back();

// The on-screen keyboard, docked under the content while a field is edited.
// Text fields with LV_OBJ_FLAG_USER_1 keep it open after Enter (chat composer).
void editText(lv_obj_t* textarea);
void closeKeyboard();

void toast(const std::string& text);

// A finger is on the screen. Views do not rebuild widgets meanwhile: a row
// deleted between press and release would swallow the tap.
bool touching();

// One column of the layout: a header (title, subtitle, action buttons) over
// a body the view fills.
struct Pane {
    lv_obj_t* title = nullptr;
    lv_obj_t* subtitle = nullptr;  // hidden while empty
    lv_obj_t* actions = nullptr;
    lv_obj_t* body = nullptr;
    void setTitle(const std::string& text);
    void setSubtitle(const std::string& text);
};

class View {
public:
    virtual ~View() = default;
    virtual const char* name() const = 0;        // console `screen`, e.g. "chats"
    virtual const char* detailName() const = 0;  // e.g. "chat"
    virtual const char* label() const = 0;       // navigation label
    virtual const char* icon() const = 0;        // LV_SYMBOL_*
    virtual void buildList(Pane& pane) = 0;
    virtual bool hasDetail() const = 0;          // the route selects something to show
    virtual void buildDetail(Pane& pane) = 0;
    virtual void buildPlaceholder(Pane& pane);   // landscape detail with nothing selected
    // Portrait: the detail page takes the whole screen (no bottom bar).
    virtual bool fullScreenDetail() const { return false; }
    // Each loop while on screen: refresh widgets when the services changed.
    virtual void update() {}
    // The widgets of these parts were deleted; drop pointers to them.
    virtual void cleared(uint8_t parts) = 0;
};

View& chatsView();
View& nodesView();
View& radioView();
View& settingsView();

// "just now", "5 min", "3 h", "2 d" since an epoch time from the mesh; "" if unknown.
std::string ago(uint32_t epochSeconds);

} // namespace Tab5
