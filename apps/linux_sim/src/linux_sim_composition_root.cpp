#include "linux_sim_composition_root.h"

namespace trailmate::linux_sim
{

bool LinuxSimCompositionRoot::initialize()
{
    presentation_.workspace.device = &device_model_;
    presentation_.workspace.gps = &gps_model_;
    presentation_.workspace.mesh = &mesh_model_;
    presentation_.workspace.settings = &settings_model_;
    presentation_.workspace.chat = &chat_model_;
    presentation_.workspace.team_chat = &team_chat_model_;
    presentation_.workspace.map = &map_model_;
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

chat::delivery::ChatDeliveryReadModel& LinuxSimCompositionRoot::deliveryReadModel()
{
    return delivery_read_model_;
}

chat::delivery::ChatDeliveryEventProjector&
LinuxSimCompositionRoot::deliveryProjector()
{
    return delivery_projector_;
}

} // namespace trailmate::linux_sim
