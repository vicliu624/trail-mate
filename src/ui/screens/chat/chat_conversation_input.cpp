#include <Arduino.h>
#include "chat_conversation_input.h"
#include "chat_conversation_components.h"

#define CHAT_CONV_INPUT_DEBUG 1
#if CHAT_CONV_INPUT_DEBUG
#define CHAT_CONV_INPUT_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CHAT_CONV_INPUT_LOG(...)
#endif

namespace chat::ui::conversation::input {

static ChatConversationScreen* s_screen = nullptr;

void init(ChatConversationScreen* screen)
{
    s_screen = screen;
    // v0: no behavior change. Future: rotary scroll / focus reply button etc.
    CHAT_CONV_INPUT_LOG("[ChatConversationInput] init (v0 no-op)\n");
}

void cleanup()
{
    s_screen = nullptr;
    CHAT_CONV_INPUT_LOG("[ChatConversationInput] cleanup\n");
}

} // namespace chat::ui::conversation::input
