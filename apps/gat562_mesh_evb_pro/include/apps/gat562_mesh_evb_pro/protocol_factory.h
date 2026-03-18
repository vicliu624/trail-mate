#pragma once

#include "chat/domain/chat_types.h"
#include "chat/runtime/self_identity_provider.h"

#include <memory>

namespace chat
{
class IMeshAdapter;
}

namespace apps::gat562_mesh_evb_pro
{

std::unique_ptr<chat::IMeshAdapter> createProtocolAdapter(chat::MeshProtocol protocol,
                                                          const chat::runtime::SelfIdentityProvider* identity_provider);

} // namespace apps::gat562_mesh_evb_pro
