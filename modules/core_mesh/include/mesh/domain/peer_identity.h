#pragma once

#include "mesh/domain/node_id.h"

#include <stdint.h>

namespace mesh
{

struct PeerPublicKey
{
    NodeId node_id;
    uint8_t public_key[32]{};
    uint32_t updated_at_ms = 0;
    bool verified = false;
};

} // namespace mesh
