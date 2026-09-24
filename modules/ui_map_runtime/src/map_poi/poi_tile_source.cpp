#include "ui_map_runtime/map_poi/poi_tile_source.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

namespace ui::map_poi
{
PoiTileSource::PoiTileSource(map_tiles::IMapTileFileSystem& files, const Parser& parser, const char* root)
    : files_(files), parser_(parser), root_(root ? root : "")
{
}

void PoiTileSource::reset()
{
    policy_checked_ = policy_valid_ = false;
    policy_ = {};
}

map_tiles::MapTileLookupResult PoiTileSource::lookup(const map_tiles::MapTileRef& ref) const
{
    char path[160]{};
    map_tiles::MapTileLookupResult result{};
    result.format = map_tiles::MapTileFormat::PoiRecords;
    const map_tiles::MapTileResolver resolver(root_);
    result.status = resolver.resolvePath(ref, path, sizeof(path)) && files_.exists(path)
                        ? map_tiles::MapTileStatus::Available
                        : map_tiles::MapTileStatus::Missing;
    return result;
}

map_tiles::MapTileReadResult PoiTileSource::read(const map_tiles::MapTileRef& ref, uint8_t* buffer, std::size_t capacity) const
{
    using namespace map_tiles;
    constexpr std::size_t records_bytes = sizeof(TileHeader) + (TileHeader::kMaxRecords + 1) * sizeof(Record);
    if (ref.layer != MapTileLayer::Poi || !buffer || capacity < records_bytes + 16384 ||
        reinterpret_cast<std::uintptr_t>(buffer) % alignof(TileHeader) != 0)
        return {MapTileReadStatus::Invalid, 0, -1, MapTileFormat::PoiRecords};
    auto* raw = buffer + records_bytes;
    const auto raw_capacity = capacity - records_bytes;
    char path[160]{};
    if (!policy_checked_)
    {
        const auto length = std::strlen(root_);
        const int written = std::snprintf(path, sizeof(path), "%s%smaps/poi/manifest.json", root_,
                                          length && root_[length - 1] != '/' ? "/" : "");
        if (written < 0 || static_cast<std::size_t>(written) >= sizeof(path))
            return {MapTileReadStatus::Invalid, 0, -2, MapTileFormat::PoiRecords};
        const auto read = files_.readFile(path, raw, raw_capacity);
        // Only a definitive missing/invalid package may be remembered. A
        // transient SD failure must not disable annotations until page reopen.
        if (read.status == MapTileReadStatus::RetryLater || read.status == MapTileReadStatus::Error) return read;
        policy_checked_ = true;
        policy_valid_ = read.status == MapTileReadStatus::Ready &&
                        parser_.manifest(reinterpret_cast<char*>(raw), read.size, policy_);
    }
    auto* output = new (buffer) TileHeader{};
    output->policy = policy_;
    output->manifest_valid = policy_valid_;
    const auto ready = [&]()
    {
        return MapTileReadResult{MapTileReadStatus::Ready, sizeof(TileHeader) + output->count * sizeof(Record),
                                 0, MapTileFormat::PoiRecords};
    };
    // Explicitly disabled levels and absent/invalid packages never load indexes.
    if (!policy_valid_ || !policy_.enabled(ref.z)) return ready();
    const MapTileResolver resolver(root_);
    if (!resolver.resolvePath(ref, path, sizeof(path))) return {MapTileReadStatus::Invalid, 0, -2, MapTileFormat::PoiRecords};
    const auto read = files_.readFile(path, raw, raw_capacity);
    if (read.status == MapTileReadStatus::Missing) return ready();
    if (read.status != MapTileReadStatus::Ready) return read;
    if (read.size > raw_capacity) return {MapTileReadStatus::Invalid, 0, -3, MapTileFormat::PoiRecords};
    std::size_t offset = 0;
    while (offset < read.size)
    {
        const auto begin = offset;
        while (offset < read.size && raw[offset] != '\n') ++offset;
        auto length = offset - begin;
        if (offset < read.size) ++offset;
        if (length && raw[begin + length - 1] == '\r') --length;
        if (length == 0) continue;
        // Parsed records borrow the existing map worker scratch; even overflow
        // parsing uses its extra slot, not a resident or task-stack buffer.
        auto* target = new (buffer + sizeof(TileHeader) + output->count * sizeof(Record)) Record{};
        if (!parser_.record(reinterpret_cast<char*>(raw + begin), length, *target) ||
            (policy_.schema_version == 3 && !target->explicit_kind) ||
            (policy_.schema_version < 3 && target->kind != ui::map::AnnotationKind::Poi))
        {
            if (output->invalid_rows < UINT16_MAX) ++output->invalid_rows;
            continue;
        }
        if (output->count < TileHeader::kMaxRecords) ++output->count;
        else output->truncated = true;
    }
    return ready();
}
} // namespace ui::map_poi
