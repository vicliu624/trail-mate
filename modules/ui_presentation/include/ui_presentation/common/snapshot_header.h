#pragma once

#include <stdint.h>

namespace ui
{

struct SnapshotHeader
{
    bool valid = false;
    uint32_t version = 0;
    uint32_t generated_at_ms = 0;
};

} // namespace ui
