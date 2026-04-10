#pragma once

#include "app/app_config.h"
#include "chat/runtime/self_identity_policy.h"

namespace ble
{
class BleManager;
}

namespace chat
{
class ChatService;
class IMeshAdapter;
}

namespace boards::gat562_mesh_evb_pro
{
class Gat562Board;
}

namespace apps::gat562_mesh_evb_pro
{

class RuntimeApplyService
{
  public:
    void applyMesh(app::AppConfig& config,
                   chat::IMeshAdapter* mesh_router,
                   chat::ChatService* chat_service,
                   ble::BleManager* ble_manager,
                   boards::gat562_mesh_evb_pro::Gat562Board* board) const;

    void applyUserInfo(const chat::runtime::EffectiveSelfIdentity& previous_identity,
                       const chat::runtime::EffectiveSelfIdentity& current_identity,
                       chat::IMeshAdapter* mesh_router,
                       ble::BleManager* ble_manager) const;

    void applyPosition(const app::AppConfig& config,
                       boards::gat562_mesh_evb_pro::Gat562Board* board) const;
};

} // namespace apps::gat562_mesh_evb_pro
