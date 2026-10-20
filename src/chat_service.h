#pragma once

// Glue between the radio link and the conversation model: routes incoming
// text and delivery reports into conversations, sends outgoing text, and keeps
// history on the SD card (one folder per radio node).

#include <cstdint>
#include <string>

#include "core/conversations.h"

namespace Chat {

void begin();
void loop();

ConversationStore& store();

int openDirect(uint32_t node);
int openChannel(uint8_t channel);
int openNostr(const std::string& pubHex);

// The conversation on screen; its new messages are not marked unread.
void setActive(int convIdx);

// Sends UTF-8 text. Returns "" on success, otherwise a short reason.
std::string send(int convIdx, const std::string& utf8);

// Display-ready (ASCII) labels.
std::string title(const Conversation& conv);
std::string senderLabel(const Conversation& conv, const ChatMessage& msg);
std::string preview(const Conversation& conv, size_t width);

// Screenshot/demo mode: replaces the in-memory conversations with fictional
// ones (uses the demo nodes) and stops writing history to the SD card.
void loadDemo();

// Last notable event for a one-line toast ("Ana: hi", "Not delivered").
const std::string& toast();
uint32_t toastAtMs();

} // namespace Chat
