#pragma once
#include "chat/usecase/chat_service.h"
namespace team
{
class TeamController
{
};
} // namespace team
namespace fake_app
{
struct Config
{
    chat::MeshProtocol mesh_protocol = chat::MeshProtocol::Meshtastic;
    chat::MeshConfig mesh;
    const chat::MeshConfig& activeMeshConfig() const { return mesh; }
};
struct ConfigFacade
{
    Config value;
    const Config& readConfig() const { return value; }
};
struct MessagingFacade
{
    uint32_t getSelfNodeId() const { return 42; }
    ChatService& getChatService() { return service; }
};
inline ConfigFacade config;
inline MessagingFacade messaging;
struct TeamFacade
{
    team::TeamController controller;
    auto* getTeamController() { return &controller; }
};
inline TeamFacade team_api;
} // namespace fake_app
namespace app
{
inline bool hasAppFacade() { return true; }
inline auto& configFacade() { return fake_app::config; }
inline auto& messagingFacade() { return fake_app::messaging; }
inline auto& teamFacade() { return fake_app::team_api; }
} // namespace app
