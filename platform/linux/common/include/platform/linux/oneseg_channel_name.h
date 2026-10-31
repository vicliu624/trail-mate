#pragma once
#include "platform/linux/oneseg_service_text.h"

namespace platform::linux_runtime
{
// Persist only a decoded name belonging to the selected service. The store's
// 192-byte limit must never split a UTF-8 character. Mark truncation explicitly.
inline std::string oneseg_received_channel_name(const OneSegServiceText& text, uint16_t selected)
{
    if (!selected || text.service != selected || !text.service_valid || !text.name) return {};
    const auto& name = text.name.text;
    if (name.size() <= 192) return name;
    size_t length = 189;
    while (length && (static_cast<unsigned char>(name[length]) & 0xc0) == 0x80) --length;
    return name.substr(0, length) + "\xe2\x80\xa6";
}
} // namespace platform::linux_runtime
