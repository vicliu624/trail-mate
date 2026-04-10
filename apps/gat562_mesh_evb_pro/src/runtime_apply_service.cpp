#include "apps/gat562_mesh_evb_pro/runtime_apply_service.h"

#include "apps/gat562_mesh_evb_pro/debug_console.h"
#include "ble/ble_manager.h"
#include "boards/gat562_mesh_evb_pro/gat562_board.h"
#include "boards/gat562_mesh_evb_pro/settings_store.h"
#include "chat/infra/mesh_adapter_router_core.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/usecase/chat_service.h"

#include <cstring>

namespace apps::gat562_mesh_evb_pro
{
namespace
{

const char* protocolLabel(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::MeshCore ? "MC" : "MT";
}

} // namespace

void RuntimeApplyService::applyMesh(app::AppConfig& config,
                                    chat::IMeshAdapter* mesh_router,
                                    chat::ChatService* chat_service,
                                    ble::BleManager* ble_manager,
                                    boards::gat562_mesh_evb_pro::Gat562Board* board) const
{
    debug_console::printf("[gat562][cfg] applyMesh start proto=%u ok_to_mqtt=%u ignore_mqtt=%u\n",
                          static_cast<unsigned>(config.mesh_protocol),
                          config.meshtastic_config.config_ok_to_mqtt ? 1U : 0U,
                          config.meshtastic_config.ignore_mqtt ? 1U : 0U);
    ::boards::gat562_mesh_evb_pro::settings_store::normalizeConfig(config);

    if (mesh_router)
    {
        auto* router = static_cast<chat::MeshAdapterRouterCore*>(mesh_router);
        router->setActiveProtocol(config.mesh_protocol);
        mesh_router->applyConfig(config.activeMeshConfig());
    }
    if (board)
    {
        board->applyRadioConfig(config.mesh_protocol, config.activeMeshConfig());
    }
    if (chat_service)
    {
        chat_service->setActiveProtocol(config.mesh_protocol);
    }
    if (ble_manager)
    {
        ble_manager->applyProtocol(config.mesh_protocol);
    }

    debug_console::printf("[gat562][cfg] applyMesh end\n");

    const chat::MeshConfig& mesh = config.activeMeshConfig();
    debug_console::printf("[gat562] radio cfg %s region=%u preset=%u ch=%u tx=%d hop=%u\n",
                          protocolLabel(config.mesh_protocol),
                          static_cast<unsigned>(mesh.region),
                          static_cast<unsigned>(mesh.modem_preset),
                          static_cast<unsigned>(mesh.channel_num),
                          static_cast<int>(mesh.tx_power),
                          static_cast<unsigned>(mesh.hop_limit));
}

void RuntimeApplyService::applyUserInfo(const chat::runtime::EffectiveSelfIdentity& previous_identity,
                                        const chat::runtime::EffectiveSelfIdentity& current_identity,
                                        chat::IMeshAdapter* mesh_router,
                                        ble::BleManager* ble_manager) const
{
    if (mesh_router)
    {
        mesh_router->setUserInfo(current_identity.long_name, current_identity.short_name);
    }

    const bool ble_identity_changed =
        std::strcmp(previous_identity.long_name, current_identity.long_name) != 0 ||
        std::strcmp(previous_identity.short_name, current_identity.short_name) != 0;
    if (ble_identity_changed && ble_manager && ble_manager->isEnabled())
    {
        ble_manager->setEnabled(false);
        ble_manager->setEnabled(true);
    }
}

void RuntimeApplyService::applyPosition(const app::AppConfig& config,
                                        boards::gat562_mesh_evb_pro::Gat562Board* board) const
{
    if (board)
    {
        board->applyGpsConfig(config);
    }
}

} // namespace apps::gat562_mesh_evb_pro
