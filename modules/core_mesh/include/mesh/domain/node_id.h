#pragma once

#include <stdint.h>

namespace mesh
{

struct NodeId
{
    uint32_t value = 0;

    constexpr NodeId() = default;

    constexpr explicit NodeId(uint32_t node_value)
        : value(node_value)
    {
    }

    constexpr bool isBroadcast() const
    {
        return value == 0xFFFFFFFFUL;
    }

    constexpr bool isValidUnicast() const
    {
        return value != 0 && !isBroadcast();
    }

    constexpr bool operator==(const NodeId& other) const
    {
        return value == other.value;
    }

    constexpr bool operator!=(const NodeId& other) const
    {
        return !(*this == other);
    }
};

} // namespace mesh
