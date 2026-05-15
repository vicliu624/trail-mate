#pragma once

#include "chat/domain/chat_types.h"
#include "chat/runtime/self_identity_provider.h"
#include "chat/usecase/contact_service.h"
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/node_store.h"

#include <memory>

namespace chat
{
class IMeshAdapter;
}

namespace apps::gat562_mesh_evb_pro
{

std::unique_ptr<chat::IMeshAdapter> createProtocolAdapter(chat::MeshProtocol protocol,
                                                          const chat::runtime::SelfIdentityProvider* identity_provider,
                                                          platform::nrf52::arduino_common::chat::meshtastic::NodeStore* meshtastic_node_store = nullptr,
                                                          chat::contacts::ContactService* contact_service = nullptr);

} // namespace apps::gat562_mesh_evb_pro
