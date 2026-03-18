#pragma once

#include "chat/runtime/self_identity_provider.h"
#include "chat/domain/chat_types.h"

#include <cstddef>

namespace chat::runtime
{

struct EffectiveSelfIdentity
{
    NodeId node_id = 0;
    char long_name[32] = {};
    char short_name[16] = {};
    char ble_name[32] = {};
};

bool resolveEffectiveSelfIdentity(const SelfIdentityInput& input,
                                  EffectiveSelfIdentity* out_identity);

void formatCompactNodeId(NodeId node_id, char* out, size_t out_len);
void formatScreenNodeLabel(NodeId node_id, char* out, size_t out_len);
void buildBleVisibleName(const EffectiveSelfIdentity& identity,
                         MeshProtocol protocol,
                         char* out,
                         size_t out_len);

} // namespace chat::runtime
