#pragma once
#include "chat/domain/reticulum_network_config.h"
#include <array>

namespace trailmate::linux_app
{
// Bearer-neutral receive ownership. Logical configuration ID plus a live peer/
// connection generation identifies the route; no OS interface name enters it.
struct LinuxReticulumIpFrame
{
    char interface_id[chat::reticulum::kInterfaceIdMaxLen + 1]{};
    uint64_t connection_generation = 0;
    std::array<uint8_t, chat::reticulum::kReticulumMtu> data{};
    size_t size = 0;
};
} // namespace trailmate::linux_app
