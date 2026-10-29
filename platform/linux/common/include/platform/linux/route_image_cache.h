#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>

namespace platform::linux_runtime
{
// The caller holds its route-image commit lease and supplies canonical paths.
bool build_route_image_bitmaps(const std::filesystem::path& source,
                              const std::filesystem::path& thumbnail,
                              const std::filesystem::path& view,
                              const uint8_t expected_digest[32], uint32_t expected_bytes,
                              const std::atomic_bool* cancelled);
}
