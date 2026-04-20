#pragma once

#include "chat/domain/chat_types.h"
#include "chat/ports/i_mesh_adapter.h"

namespace chat::ui::support
{

chat::MeshProtocol active_mesh_protocol();
chat::MeshCapabilities active_mesh_capabilities();
bool supports_local_text_chat();
bool supports_team_chat();
const char* local_text_chat_unavailable_message();
const char* team_chat_unavailable_message();

} // namespace chat::ui::support
