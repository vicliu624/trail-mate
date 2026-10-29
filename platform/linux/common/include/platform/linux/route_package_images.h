#pragma once
#include "platform/ui/route_storage.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace platform::linux_runtime
{
// Bits identify inputs materialized only after a fresh complete package proof.
uint64_t materialize_route_package_images(const std::string& asset_id,
                                          const std::string& route,
                                          const std::vector<platform::ui::route_storage::RouteImageCacheItem>& items,
                                          const std::atomic_bool* cancelled);
} // namespace platform::linux_runtime
