#pragma once
#include "app/app_config.h"
#include <type_traits>

namespace trailmate::linux_app
{
// Existing native on-disk layout, shared by startup, reset and TMS transactions.
// Version 2 migration remains owned by application startup.
struct LinuxAppConfigBlob
{
    static constexpr uint32_t kMagic = 0x544D4346U;
    static constexpr uint32_t kVersion = 3U;
    uint32_t magic = kMagic;
    uint32_t version = kVersion;
    app::AppConfig config{};
};
static_assert(std::is_trivially_copyable_v<app::AppConfig>);
} // namespace trailmate::linux_app
