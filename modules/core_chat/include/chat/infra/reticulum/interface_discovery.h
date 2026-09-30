#pragma once

#include <cstddef>
#include <cstdint>

namespace chat::reticulum
{
// Borrowed views into the original announce. Parsing does not authenticate the
// signature or stamp; callers must verify both before admitting any candidate.
struct InterfaceDiscoveryView
{
    const uint8_t* packed = nullptr;
    size_t packed_size = 0;
    const uint8_t* stamp = nullptr;
    const uint8_t* host = nullptr;
    size_t host_size = 0;
    const uint8_t* transport_id = nullptr;
    uint16_t port = 0;
};

// Public, unencrypted, unauthenticated TCP/Backbone interface subset of native
// rnstransport.discovery.interface announcements. No allocations or recursion.
bool parseInterfaceDiscovery(const uint8_t* app_data, size_t size, InterfaceDiscoveryView& out);
} // namespace chat::reticulum
