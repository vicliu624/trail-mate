#pragma once
#include "ui_map_runtime/map_tiles/map_tile_source.h"
#include <array>
#include <filesystem>
#include <memory>
#include <vector>

namespace platform::linux_runtime
{
enum class PackagedMapMode : uint8_t
{
    Pending,
    Legacy,
    Tmap,
    Error
};
struct PackagedMapSnapshot
{
    PackagedMapMode mode = PackagedMapMode::Pending;
    uint32_t session = 1;
    std::array<uint32_t, 13> coverage{};
};
struct PackagedMapPixels
{
    ~PackagedMapPixels();
    PackagedMapPixels(const PackagedMapPixels&) = delete;
    PackagedMapPixels& operator=(const PackagedMapPixels&) = delete;
    explicit PackagedMapPixels(uint8_t* data) : data(data) {}
    uint8_t* data;
};
struct PackagedMapTile
{
    ui::map_tiles::MapTileRef ref;
    ui::map_tiles::MapTileReadResult result;
    std::shared_ptr<PackagedMapPixels> pixels;
    std::vector<uint64_t> annotations; // Aligned shared POI records, never LVGL objects.
    const uint8_t* data() const { return pixels ? pixels->data : reinterpret_cast<const uint8_t*>(annotations.data()); }
};
// UI calls only synchronize bounded metadata/queues. The owned worker performs
// catalog scans, CRC-checked reads and media/file-change checks without LVGL.
class PackagedMapSession
{
  public:
    explicit PackagedMapSession(std::filesystem::path root, size_t pixel_limit = kPixelBudget);
    ~PackagedMapSession();
    PackagedMapSession(const PackagedMapSession&) = delete;
    PackagedMapSession& operator=(const PackagedMapSession&) = delete;
    PackagedMapSnapshot snapshot() const;
    std::shared_ptr<const PackagedMapTile> request(const ui::map_tiles::MapTileRef& ref);
    void cancel();
    static size_t pixelBytes();
    static constexpr size_t kPixelBudget = 32U * 1024U * 1024U;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
