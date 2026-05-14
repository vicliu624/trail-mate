#include "linux_sim_composition_root.h"

#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

namespace trailmate::linux_sim
{

bool LinuxSimCompositionRoot::initialize()
{
    return initialize(LinuxSimCompositionRootConfig{});
}

bool LinuxSimCompositionRoot::initialize(const LinuxSimCompositionRootConfig& config)
{
    if (config.ux_pack_id == nullptr ||
        !ui_lvgl_ux::buildMenuForUxPack(config.ux_pack_id, ux_menu_))
    {
        return false;
    }

    presentation_.workspace.device = &device_model_;
    presentation_.workspace.gps = &gps_model_;
    presentation_.workspace.mesh = &mesh_model_;
    presentation_.workspace.settings = &settings_model_;
    presentation_.workspace.chat = &chat_model_;
    presentation_.workspace.team_chat = &team_chat_model_;
    presentation_.workspace.map = &map_model_;
    presentation_.ux_menu = &ux_menu_;
    return true;
}

product_composition::PresentationBundle& LinuxSimCompositionRoot::presentation()
{
    return presentation_;
}

const product_composition::PresentationBundle& LinuxSimCompositionRoot::presentation() const
{
    return presentation_;
}

const ui_lvgl_ux::UxMenuModel& LinuxSimCompositionRoot::uxMenu() const
{
    return ux_menu_;
}

chat::delivery::ChatDeliveryReadModel& LinuxSimCompositionRoot::deliveryReadModel()
{
    return delivery_read_model_;
}

chat::delivery::ChatDeliveryEventProjector&
LinuxSimCompositionRoot::deliveryProjector()
{
    return delivery_projector_;
}

chat::delivery::IChatDeliveryEventPort&
LinuxSimCompositionRoot::deliveryEventPort()
{
    return delivery_event_port_;
}

chat::delivery::IChatDeliveryActionSink&
LinuxSimCompositionRoot::deliveryActionSink()
{
    return delivery_action_service_;
}

} // namespace trailmate::linux_sim
