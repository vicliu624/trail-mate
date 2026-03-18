#include "apps/gat562_mesh_evb_pro/protocol_factory.h"

#include "platform/nrf52/arduino_common/chat/infra/meshcore/meshcore_adapter_lite.h"
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/mt_adapter_lite.h"

namespace apps::gat562_mesh_evb_pro
{

std::unique_ptr<chat::IMeshAdapter> createProtocolAdapter(chat::MeshProtocol protocol,
                                                          const chat::runtime::SelfIdentityProvider* identity_provider)
{
    switch (protocol)
    {
    case chat::MeshProtocol::MeshCore:
        return std::unique_ptr<chat::IMeshAdapter>(
            new platform::nrf52::arduino_common::chat::meshcore::MeshCoreAdapterLite(identity_provider));
    case chat::MeshProtocol::Meshtastic:
    default:
        return std::unique_ptr<chat::IMeshAdapter>(
            new platform::nrf52::arduino_common::chat::meshtastic::MtAdapterLite(identity_provider));
    }
}

} // namespace apps::gat562_mesh_evb_pro
