#include "apps/gat562_mesh_evb_pro/protocol_factory.h"

#include "platform/nrf52/arduino_common/chat/infra/meshcore/meshcore_radio_adapter.h"
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/meshtastic_radio_adapter.h"

namespace apps::gat562_mesh_evb_pro
{

std::unique_ptr<chat::IMeshAdapter> createProtocolAdapter(chat::MeshProtocol protocol,
                                                          const chat::runtime::SelfIdentityProvider* identity_provider,
                                                          platform::nrf52::arduino_common::chat::meshtastic::NodeStore* meshtastic_node_store,
                                                          chat::contacts::ContactService* contact_service)
{
    switch (protocol)
    {
    case chat::MeshProtocol::MeshCore:
        return std::unique_ptr<chat::IMeshAdapter>(
            new platform::nrf52::arduino_common::chat::meshcore::MeshCoreRadioAdapter(identity_provider));
    case chat::MeshProtocol::Meshtastic:
    default:
        return std::unique_ptr<chat::IMeshAdapter>(
            new platform::nrf52::arduino_common::chat::meshtastic::MeshtasticRadioAdapter(identity_provider,
                                                                                          meshtastic_node_store,
                                                                                          contact_service));
    }
}

} // namespace apps::gat562_mesh_evb_pro
