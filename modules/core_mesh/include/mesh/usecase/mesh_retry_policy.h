#pragma once

#include <stdint.h>

namespace mesh
{

struct MeshRetryPolicy
{
    uint8_t max_attempts = 3;
    uint32_t base_delay_ms = 1500;
    uint32_t ack_timeout_ms = 15000;
};

} // namespace mesh
