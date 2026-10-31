#pragma once
#include <cstdint>
#include <modules/LR2021/LR2021_commands.h>

namespace platform::esp::idf_common::lr2021_irq
{
struct Mapping
{
    uint32_t raw;
    uint32_t logical;
};
inline constexpr Mapping kMappings[] = {
    {RADIOLIB_LR2021_IRQ_TX_DONE, 0x0001},
    {RADIOLIB_LR2021_IRQ_RX_DONE, 0x0002},
    {RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED, 0x0004},
    {RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID, 0x0018},
    {RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR, 0x0020},
    {RADIOLIB_LR2021_IRQ_CRC_ERROR | RADIOLIB_LR2021_IRQ_LEN_ERROR | RADIOLIB_LR2021_IRQ_ADDR_ERROR, 0x0040},
    {RADIOLIB_LR2021_IRQ_CAD_DONE, 0x0080},
    {RADIOLIB_LR2021_IRQ_CAD_DETECTED, 0x0100},
    {RADIOLIB_LR2021_IRQ_TIMEOUT, 0x0200},
};
constexpr uint32_t logical(uint32_t raw)
{
    uint32_t result = 0;
    for (const auto& item : kMappings)
        if (raw & item.raw) result |= item.logical;
    return result;
}
constexpr uint32_t physical(uint32_t flags)
{
    if (flags == 0xffffu || flags == 0xffffffffu) return RADIOLIB_LR2021_IRQ_ALL;
    uint32_t result = 0;
    for (const auto& item : kMappings)
        if (flags & item.logical) result |= item.raw;
    return result;
}
} // namespace platform::esp::idf_common::lr2021_irq
