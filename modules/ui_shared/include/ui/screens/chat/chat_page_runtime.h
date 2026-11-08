#pragma once

#include "chat/domain/chat_types.h"
#include "ui/screens/chat/chat_page_shell.h"

namespace chat::ui::runtime
{

bool is_available();
void enter(const shell::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);
lv_obj_t* get_container();

// Queues a compose route before the Chat app is made active. The target is
// consumed by enter(), after Chat owns the page and its focus group.
bool requestCompose(const chat::ConversationId& conversation);
// Opens the existing conversation through Chat's normal read-state workflow.
bool requestConversation(const chat::ConversationId& conversation);
// Cancels either queued Chat destination if application navigation fails.
void clearRequestedCompose();

} // namespace chat::ui::runtime
