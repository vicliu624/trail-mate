#pragma once

namespace chat::ui {

class ChatConversationScreen;

namespace conversation::input {

void init(ChatConversationScreen* screen);
void cleanup();

} // namespace conversation::input
} // namespace chat::ui
