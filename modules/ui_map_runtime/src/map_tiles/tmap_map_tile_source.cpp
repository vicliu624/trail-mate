#include "ui_map_runtime/map_tiles/tmap_map_tile_source.h"
#include "ui_map_runtime/map_poi/poi_types.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
    std::array<uint32_t, 13> layer_zooms{};
    bool has_pois = false;
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
        r.status = MapTileReadStatus::RetryLater;
        r.error = -11;
        break;
    case tmap::Status::More:
        r.status = MapTileReadStatus::RetryLater;
        r.error = -115;
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
    struct LocatedTile
    {
        uint64_t key = 0;
        tmap::Tile tile{};
        uint32_t semantic = 0;
        uint16_t package = 0;
        bool valid = false, missing = false;
    };
    tmap::Workspace workspace{};
    tmap::Reader reader{workspace};
    Entry* entries[kMaxPackages]{};
    Block* blocks = nullptr;
    Block* tail = nullptr;
    Entry* active = nullptr;
    char pending[kPathBytes]{};
    std::array<uint32_t, 13> pending_layers{};
    uint32_t session = 0;
    size_t count = 0, bytes = sizeof(State);
    uint32_t switches = 0, switch_log_ms = 0;
    bool complete = false, failed = false;
    struct AnnotationSlot
    {
        tmap::AnnotationCursor cursor{};
        uint8_t* payload = nullptr;
        uint64_t key = 0;
        uint32_t age = 0;
        uint32_t published_ms = 0;
        uint16_t published_count = 0;
        bool used = false;
    };
    AnnotationSlot annotation_slots[3]{};
    uint32_t annotation_age = 0;
    // All zoom-mask bits, 256 packages: 1024 bytes, entirely in PSRAM.
    uint32_t zoom_candidates[32][kMaxPackages / 32]{};
    // A bounded offset plan shared by lookup/read and OSM/POI requests.
    LocatedTile located[16]{};
    size_t next_location = 0;
};
TmapMapTileSource::~TmapMapTileSource() { reset(); }
void TmapMapTileSource::reset()
{
    storage_.closePackage();
    storage_.resetEnumeration();
    if (!state_) return;
    for (auto& slot : state_->annotation_slots)
        if (slot.payload) storage_.release(slot.payload);
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
tmap::Status TmapMapTileSource::prepare() const { return catalog(); }
const char* TmapMapTileSource::activePackagePath() const { return state_ && state_->active ? state_->active->path : "<unselected>"; }
void TmapMapTileSource::cancelPendingAnnotations()
{
    if (!state_) return;
    for (auto& slot : state_->annotation_slots) slot.used = false;
}
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
                for (size_t i = 0; i < s.count; ++i)
                    for (unsigned z = 0; z < 32; ++z)
                        if (s.entries[i]->package.zoom_mask & (UINT32_C(1) << z))
                            s.zoom_candidates[z][i / 32] |= UINT32_C(1) << (i % 32);
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
        status = s.reader.layerCoverage(s.pending_layers);
        if (status == tmap::Status::Busy || status == tmap::Status::More) return status;
        if (status != tmap::Status::Ok)
        {
            s.failed = true;
            return status;
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
        entry->layer_zooms = s.pending_layers;
        entry->has_pois = s.reader.poiCount() != 0;
        std::memcpy(entry->path, s.pending, sizeof(entry->path));
        s.entries[s.count++] = entry;
        s.pending[0] = 0;
    }
    return tmap::Status::More;
}
tmap::Status TmapMapTileSource::activate(size_t index) const
{
    auto& s = *state_;
    auto* entry = s.entries[index];
    if (s.active == entry) return tmap::Status::Ok;
    s.reader.close();
    s.active = nullptr;
    auto status = storage_.openPackage(entry->path);
    if (status == tmap::Status::Ok) status = s.reader.open(storage_);
    if (status != tmap::Status::Ok) return status;
    // Do not reuse offsets when a file was replaced without a media refresh.
    if (s.reader.package().build != entry->package.build) return tmap::Status::Invalid;
    s.active = entry;
    ++s.switches;
    const auto now = storage_.nowMs();
    if (s.switches == 1 || now - s.switch_log_ms >= 5000U)
    {
        s.switch_log_ms = now;
        std::printf("[TMAP][PACKAGE] activate path=%s switches=%lu\n", entry->path,
                    static_cast<unsigned long>(s.switches));
    }
    return tmap::Status::Ok;
}
tmap::Status TmapMapTileSource::select(const MapTileRef& ref, tmap::Tile& tile) const
{
    uint64_t key = 0;
    if (!tmap::Reader::tileKey(ref.z, ref.x, ref.y, key)) return tmap::Status::Invalid;
    auto status = catalog();
    if (status != tmap::Status::Ok) return status;
    auto& s = *state_;
    // Annotation availability is indexed independently of OSM pixel presence.
    const auto layer = ref.layer == MapTileLayer::Poi ? UINT32_C(0x80000000) : semantic(ref.layer);
    for (const auto& located : s.located)
    {
        if (!located.valid || located.key != key || located.semantic != layer) continue;
        if (located.missing) return tmap::Status::Missing;
        status = activate(located.package);
        if (status != tmap::Status::Ok) return status;
        tile = located.tile;
        return tmap::Status::Ok;
    }
    const auto bounds = tileBounds(ref);
    for (size_t word = 0; word < kMaxPackages / 32; ++word)
    {
        auto candidates = s.zoom_candidates[ref.z][word];
        while (candidates)
        {
            unsigned bit = 0;
            while (!(candidates & (UINT32_C(1) << bit))) ++bit;
            candidates &= ~(UINT32_C(1) << bit);
            const size_t i = word * 32 + bit;
            if (ref.layer == MapTileLayer::Poi)
            {
                if (!s.entries[i]->has_pois) continue;
            }
            else
            {
                const auto slot = static_cast<unsigned>(ref.layer);
                if (slot >= 13 || !(s.entries[i]->layer_zooms[slot] & (UINT32_C(1) << ref.z))) continue;
            }
            if (!s.entries[i]->package.bounds.intersects(bounds)) continue;
            status = activate(i);
            if (status != tmap::Status::Ok) return status;
            status = ref.layer == MapTileLayer::Poi ? s.reader.lookupAnnotations(ref.z, ref.x, ref.y)
                                                    : s.reader.lookupTile(layer, ref.z, ref.x, ref.y, tile);
            if (status == tmap::Status::Ok)
            {
                auto& located = s.located[s.next_location++ % 16];
                located = {key, tile, layer, static_cast<uint16_t>(i), true, false};
                return status;
            }
            if (status != tmap::Status::Missing) return status;
        }
    }
    auto& located = s.located[s.next_location++ % 16];
    located = {key, {}, layer, 0, true, true};
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
    auto& s = *state_;
    uint64_t key = 0;
    if (!tmap::Reader::tileKey(ref.z, ref.x, ref.y, key)) return result(tmap::Status::Invalid);
    State::AnnotationSlot* selected = nullptr;
    for (auto& slot : s.annotation_slots)
        if (slot.used && slot.key == key && slot.cursor.build == s.active->package.build) selected = &slot;
    if (!selected)
    {
        for (auto& slot : s.annotation_slots)
            if (!slot.used)
            {
                selected = &slot;
                break;
            }
        if (!selected)
        {
            // Admission backpressure must never destroy another tile's cursor.
            // A viewport generation change explicitly releases obsolete slots.
            auto pending = result(tmap::Status::Busy);
            pending.format = MapTileFormat::PoiRecords;
            return pending;
        }
        constexpr size_t bytes = sizeof(ui::map_poi::TileHeader) + ui::map_poi::TileHeader::kMaxRecords * sizeof(ui::map_poi::Record);
        if (!selected->payload)
        {
            selected->payload = static_cast<uint8_t*>(storage_.allocate(bytes, alignof(ui::map_poi::TileHeader)));
            if (!selected->payload) return result(tmap::Status::IoError);
            s.bytes += bytes;
        }
        const auto status = s.reader.beginAnnotations(ref.z, ref.x, ref.y, selected->cursor);
        if (status != tmap::Status::Ok) return result(status);
        new (selected->payload) ui::map_poi::TileHeader{};
        selected->published_count = 0;
        selected->published_ms = storage_.nowMs();
        selected->key = key;
        selected->used = true;
    }
    selected->age = ++s.annotation_age;
    auto* header = reinterpret_cast<ui::map_poi::TileHeader*>(selected->payload);
    // Coverage belongs to the package catalog, not to the UI's session policy.
    // A world-package response must not disable later country/province requests.
    header->policy.enabled_levels = UINT32_MAX;
    header->manifest_valid = true;
    AnnotationOutput context{header, selected->payload, std::min(ui::map_poi::TileHeader::kMaxRecords, (capacity - sizeof(*header)) / sizeof(ui::map_poi::Record)), ref};
    auto status = tmap::Status::More;
    const auto started = storage_.nowMs();
    for (unsigned stage = 0; stage < 64; ++stage)
    {
        status = s.reader.annotationStep(selected->cursor, 1, emitAnnotation, &context);
        if (status != tmap::Status::More || storage_.nowMs() - started >= 20U) break;
    }
    if (status == tmap::Status::Busy || status == tmap::Status::More)
    {
        // Publish the first complete record immediately, then coalesce updates
        // to four new records or one second. The cursor/accumulator stay owned.
        const auto now = storage_.nowMs();
        if (header->count > selected->published_count &&
            (!selected->published_count || header->count - selected->published_count >= 4 || now - selected->published_ms >= 1000U))
        {
            header->partial = true;
            auto snapshot = result(tmap::Status::Ok);
            snapshot.format = MapTileFormat::PoiRecords;
            snapshot.size = sizeof(*header) + header->count * sizeof(ui::map_poi::Record);
            std::memcpy(output, selected->payload, snapshot.size);
            selected->published_count = header->count;
            selected->published_ms = now;
            return snapshot;
        }
        static uint32_t last_progress_ms = 0;
        if (!last_progress_ms || now - last_progress_ms >= 5000U)
        {
            last_progress_ms = now;
            std::printf("[TMAP][POI][progress] z=%u x=%lu y=%lu stage=%u seen=%lu expected=%lu records=%u status=%s\n",
                        static_cast<unsigned>(ref.z), static_cast<unsigned long>(ref.x), static_cast<unsigned long>(ref.y),
                        static_cast<unsigned>(selected->cursor.stage), static_cast<unsigned long>(selected->cursor.seen),
                        static_cast<unsigned long>(selected->cursor.expected), static_cast<unsigned>(header->count),
                        status == tmap::Status::Busy ? "busy" : "more");
        }
        auto pending = result(status);
        pending.format = MapTileFormat::PoiRecords;
        if (status == tmap::Status::More) pending.error = -115; // Progress, not bus contention.
        return pending;
    }
    if (status == tmap::Status::Missing || (status == tmap::Status::Cancelled && header->truncated)) status = tmap::Status::Ok;
    auto r = result(status);
    r.format = MapTileFormat::PoiRecords;
    if (status == tmap::Status::Ok)
    {
        header->partial = false;
        r.size = sizeof(*header) + header->count * sizeof(ui::map_poi::Record);
        std::memcpy(output, selected->payload, r.size);
    }
    selected->used = false;
    return r;
}
} // namespace ui::map_tiles
