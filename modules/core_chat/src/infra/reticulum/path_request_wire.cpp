#include "chat/infra/reticulum/path_request_wire.h"
#include <algorithm>
#include <cstring>

namespace chat::reticulum
{
bool buildPathRequest(const uint8_t destination[16], const uint8_t tag[16], uint8_t* output, size_t* capacity)
{
    if (!capacity) return false;
    const auto available = *capacity;
    *capacity = 0;
    if (!destination || !tag || !output || available < kPathRequestPacketSize ||
        !std::any_of(destination, destination + 16, [](uint8_t value)
                     { return value != 0; }) ||
        !std::any_of(tag, tag + 16, [](uint8_t value)
                     { return value != 0; })) return false;
    uint8_t payload[32], control[16];
    std::memcpy(payload, destination, 16);
    std::memcpy(payload + 16, tag, 16);
    computePathRequestDestinationHash(control);
    auto size = available;
    if (!buildHeader1Packet(PacketType::Data, DestinationType::Plain, PacketContext::None, false,
                            control, payload, sizeof(payload), output, &size)) return false;
    *capacity = size;
    return true;
}
} // namespace chat::reticulum
