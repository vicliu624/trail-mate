#pragma once
#include <cstdint>
#include <vector>
namespace fake_usb
{
inline std::vector<std::vector<uint8_t>> replies;
inline uint16_t sequence = 0;
} // namespace fake_usb
namespace hostlink
{
inline bool enqueue_event(uint8_t, const uint8_t* bytes, size_t size, bool, uint16_t sequence = 0, uint32_t = 0)
{
    fake_usb::replies.emplace_back(bytes, bytes + size);
    fake_usb::sequence = sequence;
    return true;
}
} // namespace hostlink
