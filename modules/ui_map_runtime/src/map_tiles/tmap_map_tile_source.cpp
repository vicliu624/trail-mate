#include "ui_map_runtime/map_tiles/tmap_map_tile_source.h"
#include "ui_map_runtime/map_poi/poi_types.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace ui::map_tiles
{
namespace
{
constexpr size_t kPathBytes = 192, kEntriesPerBlock = 16;
struct Entry
{
    tmap::Package package{};
    char path[kPathBytes]{};
};
struct Block
{
    Block* next = nullptr;
    Entry entries[kEntriesPerBlock]{};
};
bool before(const Entry* a, const Entry* b)
{
    if (a->package.specificity != b->package.specificity) return a->package.specificity > b->package.specificity;
    if (a->package.revision != b->package.revision) return a->package.revision > b->package.revision;
    if (a->package.id != b->package.id) return a->package.id < b->package.id;
    return std::strcmp(a->path, b->path) < 0;
}
uint32_t semantic(MapTileLayer layer)
{
    const auto n = static_cast<unsigned>(layer);
    if (n <= static_cast<unsigned>(MapTileLayer::Satellite)) return n + 1;
    if (n <= static_cast<unsigned>(MapTileLayer::ContourMajor25)) return 100 + n - 3;
    if (n <= static_cast<unsigned>(MapTileLayer::ContourMinor5)) return 110 + n - 8;
    return 0;
}
MapTileReadResult result(tmap::Status status)
{
    MapTileReadResult r{};
    switch (status)
    {
    case tmap::Status::Ok:
        r.status = MapTileReadStatus::Ready;
        r.error = 0;
        break;
    case tmap::Status::Missing:
        r.status = MapTileReadStatus::Missing;
        r.error = -2;
        break;
    case tmap::Status::Busy:
    case tmap::Status::More:
        r.status = MapTileReadStatus::RetryLater;
        r.error = -11;
        break;
    case tmap::Status::Invalid:
        r.status = MapTileReadStatus::Invalid;
        r.error = -22;
        break;
    default:
        r.status = MapTileReadStatus::Error;
        r.error = -5;
        break;
    }
    return r;
}
tmap::Bounds tileBounds(const MapTileRef& ref)
{
    const double scale = std::ldexp(1.0, ref.z);
    const auto latitude = [scale](double y)
    { return std::atan(std::sinh(3.14159265358979323846 * (1 - 2 * y / scale))) * 180 / 3.14159265358979323846; };
    return {static_cast<int32_t>(std::floor((ref.x / scale * 360 - 180) * 1e7)),
            static_cast<int32_t>(std::floor(latitude(double(ref.y) + 1) * 1e7)),
            static_cast<int32_t>(std::ceil(((double(ref.x) + 1) / scale * 360 - 180) * 1e7)),
            static_cast<int32_t>(std::ceil(latitude(ref.y) * 1e7))};
}
void copyText(char* out, size_t capacity, const char* in, bool& truncated)
{
    const size_t length = std::strlen(in);
    size_t n = std::min(length, capacity - 1);
    if (n < length)
    {
        truncated = true;
        while (n && (static_cast<uint8_t>(in[n]) & 0xc0) == 0x80) --n;
    }
    std::memcpy(out, in, n);
    out[n] = 0;
}
struct AnnotationOutput
{
    ui::map_poi::TileHeader* header;
    uint8_t* output;
    size_t limit;
    MapTileRef ref;
};
bool emitAnnotation(void* opaque, const tmap::Annotation& annotation, const tmap::Poi& poi)
{
    auto& out = *static_cast<AnnotationOutput*>(opaque);
    if (out.header->count == out.limit)
    {
        out.header->truncated = true;
        return false;
    }
    auto* record = new (out.output + sizeof(ui::map_poi::TileHeader) + out.header->count * sizeof(ui::map_poi::Record)) ui::map_poi::Record{};
    constexpr char hex[] = "0123456789abcdef";
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < poi.id.size(); ++i)
    {
        record->id[2 * i] = hex[poi.id[i] >> 4];
        record->id[2 * i + 1] = hex[poi.id[i] & 15];
        hash = (hash ^ poi.id[i]) * UINT64_C(1099511628211);
    }
    record->feature_key = record->key = hash;
    copyText(record->name, sizeof(record->name), poi.name, out.header->truncated);
    record->lat = annotation.latitude / 1e7;
    record->lon = annotation.longitude / 1e7;
    record->priority = static_cast<uint8_t>(std::min<unsigned>(255, (annotation.priority + 128U) / 256U));
    record->kind = annotation.kind == 3 ? ui::map::AnnotationKind::Road : annotation.kind == 2 ? ui::map::AnnotationKind::Place
                                                                                               : ui::map::AnnotationKind::Poi;
    record->explicit_kind = true;
    record->path_points = annotation.point_count;
    const auto scale = std::ldexp(256.0, out.ref.z);
    for (size_t i = 0; i < annotation.point_count; ++i)
    {
        const auto lat = std::max(-85.05112878, std::min(85.05112878, annotation.path[2 * i] / 1e7));
        const auto lon = annotation.path[2 * i + 1] / 1e7;
        const double sine = std::sin(lat * 3.14159265358979323846 / 180);
        const double x = (lon + 180) / 360 * scale - double(out.ref.x) * 256;
        const double y = (0.5 - std::log((1 + sine) / (1 - sine)) / (4 * 3.14159265358979323846)) * scale - double(out.ref.y) * 256;
        record->path[2 * i] = static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, std::round(x))));
        record->path[2 * i + 1] = static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, std::round(y))));
    }
    ++out.header->count;
    return true;
}
} // namespace

struct TmapMapTileSource::State
{
    tmap::Workspace workspace{};
    tmap::Reader reader{workspace};
    Entry* entries[kMaxPackages]{};
    Block* blocks = nullptr;
    Block* tail = nullptr;
    Entry* active = nullptr;
    char pending[kPathBytes]{};
    uint32_t session = 0;
    size_t count = 0, bytes = sizeof(State);
    bool complete = false, failed = false;
};
TmapMapTileSource::~TmapMapTileSource() { reset(); }
void TmapMapTileSource::reset()
{
    storage_.closePackage();
    storage_.resetEnumeration();
    if (!state_) return;
    for (auto* block = state_->blocks; block;)
    {
        auto* next = block->next;
        block->~Block();
        storage_.release(block);
        block = next;
    }
    state_->~State();
    storage_.release(state_);
    state_ = nullptr;
}
size_t TmapMapTileSource::allocatedBytes() const { return state_ ? state_->bytes : 0; }
size_t TmapMapTileSource::packageCount() const { return state_ ? state_->count : 0; }
tmap::Status TmapMapTileSource::catalog() const
{
    if (state_ && state_->session != storage_.session()) const_cast<TmapMapTileSource*>(this)->reset();
    if (!state_)
    {
        auto* memory = storage_.allocate(sizeof(State), alignof(State));
        if (!memory) return tmap::Status::IoError;
        state_ = new (memory) State{};
        state_->session = storage_.session();
    }
    auto& s = *state_;
    if (s.failed) return tmap::Status::Invalid;
    if (s.complete) return tmap::Status::Ok;
    for (unsigned scanned = 0; scanned < 8; ++scanned)
    {
        if (!s.pending[0])
        {
            const auto status = storage_.nextPackage(s.pending, sizeof(s.pending));
            if (status == tmap::Status::Missing)
            {
                s.complete = true;
                std::sort(s.entries, s.entries + s.count, before);
                return tmap::Status::Ok;
            }
            if (status != tmap::Status::Ok) return status;
        }
        s.reader.close();
        s.active = nullptr;
        auto status = storage_.openPackage(s.pending);
        if (status == tmap::Status::Ok) status = s.reader.open(storage_);
        if (status == tmap::Status::Busy) return status;
        if (status != tmap::Status::Ok)
        {
            s.failed = true;
            return status;
        }
        if (s.count == kMaxPackages)
        {
            s.failed = true;
            return tmap::Status::Invalid;
        }
        if (s.count % kEntriesPerBlock == 0)
        {
            auto* memory = storage_.allocate(sizeof(Block), alignof(Block));
            if (!memory) return tmap::Status::IoError;
            auto* block = new (memory) Block{};
            if (s.tail) s.tail->next = block;
            else s.blocks = block;
            s.tail = block;
            s.bytes += sizeof(Block);
        }
        auto* entry = &s.tail->entries[s.count % kEntriesPerBlock];
        entry->package = s.reader.package();
        std::memcpy(entry->path, s.pending, sizeof(entry->path));
        s.entries[s.count++] = entry;
        s.pending[0] = 0;
    }
    return tmap::Status::More;
}
tmap::Status TmapMapTileSource::select(const MapTileRef& ref, tmap::Tile& tile) const
{
    uint64_t key = 0;
    if (!tmap::Reader::tileKey(ref.z, ref.x, ref.y, key)) return tmap::Status::Invalid;
    auto status = catalog();
    if (status != tmap::Status::Ok) return status;
    auto& s = *state_;
    const auto bounds = tileBounds(ref);
    for (size_t i = 0; i < s.count; ++i)
    {
        auto* entry = s.entries[i];
        if (!(entry->package.zoom_mask & (UINT32_C(1) << ref.z)) || !entry->package.bounds.intersects(bounds)) continue;
        if (s.active != entry)
        {
            s.reader.close();
            s.active = nullptr;
            status = storage_.openPackage(entry->path);
            if (status == tmap::Status::Ok) status = s.reader.open(storage_);
            if (status != tmap::Status::Ok) return status;
            // A package replaced within the same media session must not use old metadata.
            if (s.reader.package().build != entry->package.build) return tmap::Status::Invalid;
            s.active = entry;
        }
        status = s.reader.lookupTile(ref.layer == MapTileLayer::Poi ? 1 : semantic(ref.layer), ref.z, ref.x, ref.y, tile);
        if (status != tmap::Status::Missing) return status;
    }
    return tmap::Status::Missing;
}
MapTileLookupResult TmapMapTileSource::lookup(const MapTileRef& ref) const
{
    tmap::Tile tile{};
    const auto status = select(ref, tile);
    MapTileLookupResult info{};
    info.status = status == tmap::Status::Ok ? MapTileStatus::Available : status == tmap::Status::Missing ? MapTileStatus::Missing
                                                                                                          : MapTileStatus::Error;
    if (ref.layer == MapTileLayer::Poi)
        info.format = MapTileFormat::PoiRecords; // Generated record count is known only after read.
    else if (status == tmap::Status::Ok)
    {
        info.format = tile.codec == 1 ? MapTileFormat::Rgb565 : MapTileFormat::Rgba8888;
        info.size = tile.bytes;
    }
    return info;
}
MapTileReadResult TmapMapTileSource::read(const MapTileRef& ref, uint8_t* output, size_t capacity) const
{
    if (!output) return result(tmap::Status::Invalid);
    tmap::Tile tile{};
    auto status = select(ref, tile);
    if (status != tmap::Status::Ok) return result(status);
    if (ref.layer == MapTileLayer::Poi) return annotations(ref, output, capacity);
    if (capacity < tile.bytes)
    {
        auto r = result(tmap::Status::Invalid);
        r.size = tile.bytes; // Worker may resize its single PSRAM scratch on demand.
        r.error = -28;
        return r;
    }
    status = state_->reader.readTile(tile, output, capacity);
    auto r = result(status);
    r.format = tile.codec == 1 ? MapTileFormat::Rgb565 : MapTileFormat::Rgba8888;
    if (status == tmap::Status::Ok) r.size = tile.bytes;
    return r;
}
MapTileReadResult TmapMapTileSource::annotations(const MapTileRef& ref, uint8_t* output, size_t capacity) const
{
    if (capacity < sizeof(ui::map_poi::TileHeader) || reinterpret_cast<uintptr_t>(output) % alignof(ui::map_poi::TileHeader)) return result(tmap::Status::Invalid);
    auto* header = new (output) ui::map_poi::TileHeader{};
    header->policy.enabled_levels = state_->active->package.zoom_mask;
    header->manifest_valid = true;
    AnnotationOutput context{header, output, std::min(ui::map_poi::TileHeader::kMaxRecords, (capacity - sizeof(*header)) / sizeof(ui::map_poi::Record)), ref};
    auto status = state_->reader.visitAnnotations(ref.z, ref.x, ref.y, emitAnnotation, &context);
    if (status == tmap::Status::Missing || (status == tmap::Status::Cancelled && header->truncated)) status = tmap::Status::Ok;
    auto r = result(status);
    r.format = MapTileFormat::PoiRecords;
    if (status == tmap::Status::Ok) r.size = sizeof(*header) + header->count * sizeof(ui::map_poi::Record);
    return r;
}
} // namespace ui::map_tiles
