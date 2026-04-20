#include "ui/screens/chat/chat_protocol_support.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"

namespace chat::ui::support
{

chat::MeshProtocol active_mesh_protocol()
{
    return app::configFacade().getConfig().mesh_protocol;
}

chat::MeshCapabilities active_mesh_capabilities()
{
    chat::IMeshAdapter* adapter = app::messagingFacade().getMeshAdapter();
    return adapter ? adapter->getCapabilities() : chat::MeshCapabilities{};
}

bool supports_local_text_chat()
{
    return active_mesh_capabilities().supports_unicast_text;
}

bool supports_team_chat()
{
    return active_mesh_capabilities().supports_unicast_appdata;
}

const char* local_text_chat_unavailable_message()
{
    return (active_mesh_protocol() == chat::MeshProtocol::RNode)
               ? "RNode text chat runs on host"
               : "Text chat unavailable";
}

const char* team_chat_unavailable_message()
{
    return (active_mesh_protocol() == chat::MeshProtocol::RNode)
               ? "Team chat unavailable in RNode mode"
               : "Team chat unavailable";
}

} // namespace chat::ui::support
