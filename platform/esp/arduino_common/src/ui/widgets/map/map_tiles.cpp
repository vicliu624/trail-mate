/**
 * @file map_tiles.cpp
 * @brief Map tile management and rendering implementation
 */

#include "ui/widgets/map/map_tiles.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "platform/esp/arduino_common/map_tiles/lvgl_tile_image.h"
#include "platform/esp/arduino_common/map_tiles/map_tile_command_queue.h"
#include "platform/esp/arduino_common/map_tiles/map_tile_event_queue.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "src/draw/lv_image_decoder_private.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "sys/clock.h"
#include "ui/page/page_profile.h"
#include "ui/runtime/memory_profile.h"
#include "ui/screens/gps/gps_constants.h"
#include "ui/widgets/map/map_diagnostics.h"
#include "ui_map_runtime/map_tiles/filesystem_map_tile_source.h"
#include "ui_map_runtime/map_tiles/map_tile_async_runtime.h"
#include "ui_map_runtime/map_tiles/map_tile_decoder_cache.h"
#include "ui_map_runtime/map_tiles/map_tile_geometry.h"
#include "ui_map_runtime/map_tiles/map_tile_pipeline_metrics.h"
#include "ui_map_runtime/map_tiles/map_tile_types.h"
#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
#include "platform/esp/arduino_common/map_poi/cjson_poi_parser.h"
#include "ui_map_runtime/map_poi/annotation_layout.h"
#include "ui_map_runtime/map_poi/poi_tile_source.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring> // For memcpy
#include <esp_heap_caps.h>
#include <new>

#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
void MapPoiPayloadDeleter::operator()(uint8_t* data) const noexcept { heap_caps_free(data); }
#endif

// Use LVGL's decoder API to decode PNG images
// Cache the decoder's native pixel format to avoid re-decoding on every render.
// This approach uses LVGL's built-in decoder, avoiding direct lodepng dependency

// Debug logging control
#define GPS_DEBUG 0
#if GPS_DEBUG
#define GPS_LOG(...) std::printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif

#ifndef TRAIL_MATE_MAP_TILE_FLOW_LOG
#define TRAIL_MATE_MAP_TILE_FLOW_LOG 0
#endif

#if TRAIL_MATE_MAP_TILE_FLOW_LOG
#define GPS_FLOW_LOG(...)         \
    do                            \
    {                             \
        std::printf(__VA_ARGS__); \
        std::fflush(stdout);      \
    } while (0)
#else
#define GPS_FLOW_LOG(...) \
    do                    \
    {                     \
    } while (0)
#endif

static uint32_t g_cache_full_log_ms = 0;
static uint8_t g_requested_map_source = 0;

static void style_placeholder_card(lv_obj_t* card);
static void style_placeholder_text(lv_obj_t* label);

static bool use_non_touch_placeholder_cards()
{
    return !::ui::page_profile::current().large_touch_hitbox;
}

template <typename T>
T* psram_preferred_static_instance()
{
    void* storage = heap_caps_malloc_prefer(sizeof(T),
                                            2,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (storage == nullptr)
    {
        storage = ::operator new(sizeof(T));
    }
    return new (storage) T();
}

static int fmt_tile_coord(int32_t value)
{
    // Tile coordinates are bounded by the zoom-domain we support, so narrowing
    // them for debug/UI formatting is intentional and safe.
    return static_cast<int>(value);
}

static void create_placeholder_tile_card(lv_obj_t* parent, MapTile& tile, int screen_x, int screen_y)
{
    tile.img_obj = lv_obj_create(parent);
    lv_obj_set_size(tile.img_obj, TILE_SIZE, TILE_SIZE);
    lv_obj_set_pos(tile.img_obj, screen_x, screen_y);
    style_placeholder_card(tile.img_obj);
    lv_obj_move_background(tile.img_obj);

    lv_obj_t* placeholder_label = lv_label_create(tile.img_obj);
    char placeholder_text[48];
    ui::map_tiles::formatMapTileCoordinateLabel(
        static_cast<std::uint8_t>(tile.z),
        static_cast<std::uint32_t>(fmt_tile_coord(tile.x)),
        static_cast<std::uint32_t>(fmt_tile_coord(tile.y)),
        placeholder_text,
        sizeof(placeholder_text));
    lv_label_set_text(placeholder_label, placeholder_text);
    style_placeholder_text(placeholder_label);
    lv_obj_center(placeholder_label);

    tile.has_png_file = false;
    tile.contour_obj = NULL;
    tile.contour_checked = false;
    tile.contour_loaded = false;
}

static bool g_requested_contour_enabled = false;
static uint8_t g_active_map_source = 0xFF;
static bool g_active_contour_enabled = false;
static bool g_missing_tile_notice_pending = false;
static bool g_missing_tile_notice_emitted = false;
static uint8_t g_missing_tile_notice_source = 0;
static uint32_t g_map_tile_runtime_generation = 1;

namespace
{
class PathOnlyMapTileFileSystem final : public ui::map_tiles::IMapTileFileSystem
{
  public:
    bool exists(const char* path) const override
    {
        (void)path;
        return false;
    }

    bool isDirectory(const char* path) const override
    {
        (void)path;
        return false;
    }

    ui::map_tiles::MapTileReadResult readFile(
        const char* path,
        uint8_t* buffer,
        std::size_t capacity) const override
    {
        (void)path;
        (void)buffer;
        (void)capacity;
        return {ui::map_tiles::MapTileReadStatus::Missing, 0, -1};
    }
};

#if defined(ARDUINO) || defined(ARDUINO_ARCH_ESP32)
class SdMapTileFileSystem final : public ui::map_tiles::IMapTileFileSystem
{
  public:
    bool exists(const char* path) const override
    {
        return ::platform::esp::arduino_common::storage::sd_exists(path);
    }

    bool isDirectory(const char* path) const override
    {
        return ::platform::esp::arduino_common::storage::sd_is_directory(path);
    }

    ui::map_tiles::MapTileReadResult readFile(
        const char* path,
        uint8_t* buffer,
        std::size_t capacity) const override
    {
        const auto result =
            ::platform::esp::arduino_common::storage::sd_read_file(
                path,
                buffer,
                capacity);
        ui::map_tiles::MapTileReadResult mapped{};
        mapped.size = result.bytes_read;
        mapped.error = result.error;
        mapped.timing = {result.lock_wait_ms, result.open_ms, result.read_ms, true};
        mapped.timing.block_calls = result.block_calls;
        mapped.timing.block_sectors = result.block_sectors;
        mapped.timing.block_max_sectors = result.block_max_sectors;
        mapped.timing.block_us = result.block_us;
        mapped.timing.block_available = result.block_timing_available;
        switch (result.status)
        {
        case ::platform::esp::arduino_common::storage::SdFileReadStatus::Ready:
            mapped.status = ui::map_tiles::MapTileReadStatus::Ready;
            break;
        case ::platform::esp::arduino_common::storage::SdFileReadStatus::Missing:
            mapped.status = ui::map_tiles::MapTileReadStatus::Missing;
            break;
        case ::platform::esp::arduino_common::storage::SdFileReadStatus::Busy:
            mapped.status = ui::map_tiles::MapTileReadStatus::RetryLater;
            break;
        case ::platform::esp::arduino_common::storage::SdFileReadStatus::Invalid:
            mapped.status = ui::map_tiles::MapTileReadStatus::Invalid;
            break;
        case ::platform::esp::arduino_common::storage::SdFileReadStatus::Unavailable:
        case ::platform::esp::arduino_common::storage::SdFileReadStatus::IoError:
        default:
            mapped.status = ui::map_tiles::MapTileReadStatus::Error;
            break;
        }
        return mapped;
    }
};
#endif

constexpr std::size_t kMapTileWorkerScratchBytes = 192U * 1024U;
constexpr std::size_t kMapTileWorkerTaskStackBytes = 4U * 1024U;
constexpr int kMapTileEventsPerUiDrain = 1;
constexpr int kMapTileRequestsPerUiStep = 2;
constexpr uint32_t kMapTileUiDrainBudgetMs = 4;
constexpr uint32_t kMapTileUiEventCooldownMs = 60;
constexpr uint32_t kMapTileLayerBusyBackoffMs = 900;
constexpr uint32_t kMapTileLayerTransientBackoffMs = 450;
constexpr uint32_t kMapTileLayerCacheBackoffMs = 350;
constexpr uint32_t kMapTileMissingCacheTtlMs = 5U * 60U * 1000U;
constexpr uint32_t kMapTileGenerationInitial = 1;
constexpr uint32_t kMapTileDiagnosticLogIntervalMs = 1000;
constexpr TickType_t kMapTileWorkerPostCommandYieldTicks = pdMS_TO_TICKS(32);

uint32_t g_map_tile_decode_log_ms = 0;
uint32_t g_map_tile_event_log_ms = 0;
uint32_t g_map_tile_next_event_drain_ms = 0;

bool should_log_map_tile_diagnostic(uint32_t& last_ms, uint32_t now_ms)
{
    if (last_ms == 0 || static_cast<uint32_t>(now_ms - last_ms) >= kMapTileDiagnosticLogIntervalMs)
    {
        last_ms = now_ms;
        return true;
    }
    return false;
}

const char* map_tile_format_name(ui::map_tiles::MapTileFormat format)
{
    switch (format)
    {
    case ui::map_tiles::MapTileFormat::Png:
        return "png";
    case ui::map_tiles::MapTileFormat::Jsonl:
        return "jsonl";
    case ui::map_tiles::MapTileFormat::PoiRecords:
        return "poi-records";
    case ui::map_tiles::MapTileFormat::Unknown:
    default:
        return "unknown";
    }
}

const char* map_tile_layer_name(ui::map_tiles::MapTileLayer layer)
{
    switch (layer)
    {
    case ui::map_tiles::MapTileLayer::Terrain:
        return "terrain";
    case ui::map_tiles::MapTileLayer::Satellite:
        return "satellite";
    case ui::map_tiles::MapTileLayer::Osm:
        return "osm";
    case ui::map_tiles::MapTileLayer::Poi:
        return "poi";
    case ui::map_tiles::MapTileLayer::ContourMajor500:
        return "contour-major-500";
    case ui::map_tiles::MapTileLayer::ContourMajor200:
        return "contour-major-200";
    case ui::map_tiles::MapTileLayer::ContourMajor100:
        return "contour-major-100";
    case ui::map_tiles::MapTileLayer::ContourMajor50:
        return "contour-major-50";
    case ui::map_tiles::MapTileLayer::ContourMajor25:
        return "contour-major-25";
    case ui::map_tiles::MapTileLayer::ContourMinor100:
        return "contour-minor-100";
    case ui::map_tiles::MapTileLayer::ContourMinor50:
        return "contour-minor-50";
    case ui::map_tiles::MapTileLayer::ContourMinor20:
        return "contour-minor-20";
    case ui::map_tiles::MapTileLayer::ContourMinor10:
        return "contour-minor-10";
    case ui::map_tiles::MapTileLayer::ContourMinor5:
        return "contour-minor-5";
    default:
        return "unknown";
    }
}

const char* map_tile_event_kind_name(ui::map_tiles::MapTileAsyncEventKind kind)
{
    switch (kind)
    {
    case ui::map_tiles::MapTileAsyncEventKind::Ready:
        return "ready";
    case ui::map_tiles::MapTileAsyncEventKind::Failed:
        return "failed";
    case ui::map_tiles::MapTileAsyncEventKind::RetryLater:
        return "busy";
    case ui::map_tiles::MapTileAsyncEventKind::Cancelled:
        return "cancelled";
    default:
        return "unknown";
    }
}

bool resolve_map_tile_log_path(const ui::map_tiles::MapTileRef& ref,
                               char* out_path,
                               std::size_t out_size)
{
    ui::map_tiles::MapTileResolver resolver("/");
    return resolver.resolvePath(ref, out_path, out_size);
}

lv_color_format_t lvgl_source_format_for_tile(ui::map_tiles::MapTileFormat format)
{
    switch (format)
    {
    case ui::map_tiles::MapTileFormat::Png:
        return LV_COLOR_FORMAT_RAW_ALPHA;
    case ui::map_tiles::MapTileFormat::Unknown:
    default:
        return LV_COLOR_FORMAT_UNKNOWN;
    }
}

void log_map_tile_decode_failure(const char* stage,
                                 const ui::map_tiles::MapTileRef& ref,
                                 ui::map_tiles::MapTileFormat format,
                                 std::size_t size,
                                 long error)
{
    const uint32_t now_ms = sys::millis_now();
    if (!should_log_map_tile_diagnostic(g_map_tile_decode_log_ms, now_ms))
    {
        return;
    }
    MAP_DIAG("[MAPD][decode-fail] t=%lu stage=%s layer=%u z=%u x=%lu y=%lu bytes=%u err=%ld\n",
             static_cast<unsigned long>(now_ms), stage, static_cast<unsigned>(ref.layer), static_cast<unsigned>(ref.z),
             static_cast<unsigned long>(ref.x), static_cast<unsigned long>(ref.y), static_cast<unsigned>(size), error);
    std::printf("[GPS][MAP][decode] fail stage=%s layer=%u z=%u x=%lu y=%lu fmt=%s size=%lu err=%ld\n",
                stage ? stage : "unknown",
                static_cast<unsigned>(ref.layer),
                static_cast<unsigned>(ref.z),
                static_cast<unsigned long>(ref.x),
                static_cast<unsigned long>(ref.y),
                map_tile_format_name(format),
                static_cast<unsigned long>(size),
                error);
    std::fflush(stdout);
}

void log_map_tile_event_failure(const char* stage,
                                const ui::map_tiles::MapTileAsyncEvent& event,
                                long error)
{
    const uint32_t now_ms = sys::millis_now();
    if (!should_log_map_tile_diagnostic(g_map_tile_event_log_ms, now_ms))
    {
        return;
    }
    char path[160]{};
    (void)resolve_map_tile_log_path(event.tile, path, sizeof(path));
    MAP_DIAG("[MAPD][event-fail] t=%lu stage=%s gen=%lu id=%lu kind=%u err=%ld path=%s\n",
             static_cast<unsigned long>(now_ms), stage, static_cast<unsigned long>(event.generation),
             static_cast<unsigned long>(event.command_id), static_cast<unsigned>(event.kind), error, path);
    std::printf("[GPS][MAP][event] fail stage=%s kind=%s layer=%s(%u) z=%u x=%lu y=%lu gen=%lu active_gen=%lu err=%ld path=%s\n",
                stage ? stage : "unknown",
                map_tile_event_kind_name(event.kind),
                map_tile_layer_name(event.tile.layer),
                static_cast<unsigned>(event.tile.layer),
                static_cast<unsigned>(event.tile.z),
                static_cast<unsigned long>(event.tile.x),
                static_cast<unsigned long>(event.tile.y),
                static_cast<unsigned long>(event.generation),
                static_cast<unsigned long>(g_map_tile_runtime_generation),
                error,
                path[0] != '\0' ? path : "<resolve-failed>");
    std::fflush(stdout);
}

uint8_t* allocate_tile_payload(std::size_t size)
{
    if (size == 0)
    {
        return nullptr;
    }
    return static_cast<uint8_t*>(
        heap_caps_malloc_prefer(size,
                                2,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                MALLOC_CAP_8BIT));
}

void release_tile_payload(ui::map_tiles::MapTileAsyncEvent& event)
{
    if (event.payload.data != nullptr)
    {
        heap_caps_free(const_cast<uint8_t*>(event.payload.data));
    }
    event.payload = {};
    event.payload_size = 0;
}

lv_image_dsc_t* decode_payload_to_image_desc(const ui::map_tiles::MapTileRef& ref,
                                             const ui::map_tiles::MapTilePayload& payload)
{
    if (payload.data == nullptr || payload.size == 0)
    {
        return nullptr;
    }

    const ui::map_tiles::MapTileFormat payload_format =
        payload.format == ui::map_tiles::MapTileFormat::Unknown
            ? ui::map_tiles::mapTileFormatForLayer(ref.layer)
            : payload.format;
    const lv_color_format_t source_format = lvgl_source_format_for_tile(payload_format);
    if (source_format == LV_COLOR_FORMAT_UNKNOWN)
    {
        log_map_tile_decode_failure("unsupported_format",
                                    ref,
                                    payload_format,
                                    payload.size,
                                    static_cast<long>(payload_format));
        return nullptr;
    }

    lv_image_dsc_t compressed{};
    compressed.header.magic = LV_IMAGE_HEADER_MAGIC;
    compressed.header.cf = source_format;
    compressed.header.flags = 0;
    compressed.data_size = static_cast<uint32_t>(payload.size);
    compressed.data = payload.data;

    lv_image_decoder_dsc_t decoder_dsc;
    std::memset(&decoder_dsc, 0, sizeof(decoder_dsc));

    // LVGL caches decoded images by source pointer. This descriptor is a stack
    // object whose address is commonly reused between tile decodes, so stale
    // cache hits would make different PNG tiles render with the same pixels.
    lv_image_cache_drop(&compressed);

    auto close_decoder = [&decoder_dsc, &compressed]()
    {
        lv_image_decoder_close(&decoder_dsc);
        lv_image_cache_drop(&compressed);
    };

    lv_image_decoder_args_t decoder_args{};
    decoder_args.stride_align = LV_DRAW_BUF_STRIDE_ALIGN != 1;
    decoder_args.no_cache = true;

    const lv_result_t decode_res = lv_image_decoder_open(&decoder_dsc, &compressed, &decoder_args);
    if (decode_res != LV_RESULT_OK || decoder_dsc.decoded == NULL)
    {
        log_map_tile_decode_failure("open",
                                    ref,
                                    payload_format,
                                    payload.size,
                                    static_cast<long>(decode_res));
        close_decoder();
        return nullptr;
    }

    const lv_draw_buf_t* decoded_buf = decoder_dsc.decoded;
    const uint32_t data_size = decoded_buf->data_size;
    if (decoded_buf->data == nullptr || data_size == 0 || decoded_buf->header.w == 0 ||
        decoded_buf->header.h == 0)
    {
        log_map_tile_decode_failure("decoded_buffer",
                                    ref,
                                    payload_format,
                                    payload.size,
                                    static_cast<long>(data_size));
        close_decoder();
        return nullptr;
    }

    // Session metadata must not turn a PSRAM optimization into internal heap
    // growth. ESP's lv_free ultimately calls heap_caps_free for this storage.
    lv_image_dsc_t* img_dsc = platform::esp::map_tiles::LvglTileImage::capture(
        decoder_dsc, [](size_t size) -> void*
        {
#if HAS_PSRAM
            return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
            return lv_malloc(size);
#endif
        });
    if (img_dsc == NULL)
    {
        log_map_tile_decode_failure("alloc_desc",
                                    ref,
                                    payload_format,
                                    payload.size,
                                    -12);
        close_decoder();
        return nullptr;
    }
    close_decoder();
    return img_dsc;
}

bool same_tile_ref(const ui::map_tiles::MapTileRef& lhs,
                   const ui::map_tiles::MapTileRef& rhs)
{
    return lhs.layer == rhs.layer &&
           lhs.z == rhs.z &&
           lhs.x == rhs.x &&
           lhs.y == rhs.y;
}

class MapTileAvailabilityMemory final
{
  public:
    MapTileAvailabilityMemory()
        : mutex_(xSemaphoreCreateMutex())
    {
    }

    bool knownMissing(const ui::map_tiles::MapTileRef& ref)
    {
        if (!ensureMutex() || xSemaphoreTake(mutex_, 0) != pdTRUE)
        {
            return false;
        }

        const uint32_t now_ms = sys::millis_now();
        bool missing = false;
        for (const Entry& entry : entries_)
        {
            if (!entry.used || !same_tile_ref(entry.ref, ref))
            {
                continue;
            }
            missing = static_cast<int32_t>(entry.expires_ms - now_ms) > 0;
            break;
        }
        xSemaphoreGive(mutex_);
        return missing;
    }

    void markMissing(const ui::map_tiles::MapTileRef& ref)
    {
        if (!ensureMutex() || xSemaphoreTake(mutex_, 0) != pdTRUE)
        {
            return;
        }

        const uint32_t now_ms = sys::millis_now();
        Entry* slot = nullptr;
        for (Entry& entry : entries_)
        {
            if (entry.used && same_tile_ref(entry.ref, ref))
            {
                slot = &entry;
                break;
            }
            if (!slot && (!entry.used || static_cast<int32_t>(entry.expires_ms - now_ms) <= 0))
            {
                slot = &entry;
            }
        }

        if (slot == nullptr)
        {
            slot = &entries_[next_replace_++ % kCapacity];
        }
        slot->used = true;
        slot->ref = ref;
        slot->expires_ms = now_ms + kMapTileMissingCacheTtlMs;
        xSemaphoreGive(mutex_);
    }

    void markAvailable(const ui::map_tiles::MapTileRef& ref)
    {
        if (!ensureMutex() || xSemaphoreTake(mutex_, 0) != pdTRUE)
        {
            return;
        }

        for (Entry& entry : entries_)
        {
            if (entry.used && same_tile_ref(entry.ref, ref))
            {
                entry.used = false;
                break;
            }
        }
        xSemaphoreGive(mutex_);
    }

  private:
    struct Entry
    {
        bool used = false;
        ui::map_tiles::MapTileRef ref{};
        uint32_t expires_ms = 0;
    };

    bool ensureMutex()
    {
        return mutex_ != nullptr;
    }

    static constexpr std::size_t kCapacity = 64;
    Entry entries_[kCapacity]{};
    std::size_t next_replace_ = 0;
    SemaphoreHandle_t mutex_ = nullptr;
};

MapTileAvailabilityMemory& map_tile_availability_memory()
{
    static MapTileAvailabilityMemory* memory =
        psram_preferred_static_instance<MapTileAvailabilityMemory>();
    return *memory;
}

using MapTileCommandQueue = platform::esp::arduino_common::map_tiles::MapTileCommandQueue;

ui::map_tiles::MapTileAsyncEvent copy_map_tile_event(const ui::map_tiles::MapTileAsyncEvent& event)
{
    ui::map_tiles::MapTileAsyncEvent owned = event;
    if (owned.kind == ui::map_tiles::MapTileAsyncEventKind::Ready &&
        event.payload.data != nullptr &&
        event.payload.size > 0)
    {
        uint8_t* payload = nullptr;
        int allocation_error = -12;
#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
        if (event.payload.format == ui::map_tiles::MapTileFormat::PoiRecords)
        {
            if (ui::map_poi::validPayload(event.payload.data, event.payload.size))
                payload = static_cast<uint8_t*>(heap_caps_aligned_alloc(alignof(ui::map_poi::TileHeader), event.payload.size,
                                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            else allocation_error = -22;
        }
        else
#endif
            payload = allocate_tile_payload(event.payload.size);
        if (payload == nullptr)
        {
            log_map_tile_event_failure("payload_alloc", owned, allocation_error);
            owned.kind = ui::map_tiles::MapTileAsyncEventKind::Failed;
            owned.error = allocation_error;
            owned.payload = {};
            owned.payload_size = 0;
        }
        else
        {
#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
            if (event.payload.format == ui::map_tiles::MapTileFormat::PoiRecords)
            {
                const auto* source_header = reinterpret_cast<const ui::map_poi::TileHeader*>(event.payload.data);
                new (payload) ui::map_poi::TileHeader(*source_header);
                const auto* records = ui::map_poi::payloadRecords(event.payload.data);
                for (std::size_t i = 0; i < source_header->count; ++i)
                    new (payload + sizeof(ui::map_poi::TileHeader) + i * sizeof(ui::map_poi::Record)) ui::map_poi::Record(records[i]);
            }
            else
#endif
                std::memcpy(payload, event.payload.data, event.payload.size);
            owned.payload.data = payload;
            owned.payload.size = event.payload.size;
            owned.payload.format = event.payload.format;
            owned.payload.ref = event.payload.ref;
        }
    }
    owned.published_ms = sys::millis_now();
    owned.timing_available = true;
    return owned;
}

using MapTileEventQueue = platform::esp::arduino_common::map_tiles::MapTileEventQueue;

class EspMapTileWorkerBackend final : public ui::map_tiles::IMapTileWorkerBackend
{
  public:
    explicit EspMapTileWorkerBackend(ui::map_tiles::IMapTileSource& source)
        : source_(source)
    {
    }

    ui::map_tiles::MapTileLookupResult lookup(
        const ui::map_tiles::MapTileRef& ref) override
    {
        return source_.lookup(ref);
    }

    ui::map_tiles::MapTileReadResult read(
        const ui::map_tiles::MapTileRef& ref,
        uint8_t* buffer,
        std::size_t capacity) override
    {
#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
        if (ref.layer == ui::map_tiles::MapTileLayer::Poi) return poi_source_.read(ref, buffer, capacity);
#endif
        ui::map_tiles::MapTileReadResult result{};
        result.format = ui::map_tiles::mapTileFormatForLayer(ref.layer);
        if (map_tile_availability_memory().knownMissing(ref))
        {
            result.error = -1;
            return result;
        }

        const ui::map_tiles::MapTileReadResult storage_result =
            source_.read(ref, buffer, capacity);
        result.format = storage_result.format;
        result.size = storage_result.size;
        result.error = storage_result.error;
        result.timing = storage_result.timing;
        switch (storage_result.status)
        {
        case ui::map_tiles::MapTileReadStatus::Ready:
            map_tile_availability_memory().markAvailable(ref);
            result.status = ui::map_tiles::MapTileReadStatus::Ready;
            result.error = 0;
            return result;
        case ui::map_tiles::MapTileReadStatus::RetryLater:
            result.status = ui::map_tiles::MapTileReadStatus::RetryLater;
            return result;
        case ui::map_tiles::MapTileReadStatus::Missing:
            map_tile_availability_memory().markMissing(ref);
            result.status = ui::map_tiles::MapTileReadStatus::Error;
            return result;
        case ui::map_tiles::MapTileReadStatus::Invalid:
        case ui::map_tiles::MapTileReadStatus::Error:
        default:
            result.status = ui::map_tiles::MapTileReadStatus::Error;
            return result;
        }
    }

    void resetMetadata()
    {
#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
        poi_source_.reset();
#endif
    }

  private:
    ui::map_tiles::IMapTileSource& source_;
#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
    SdMapTileFileSystem poi_files_;
    platform::esp::arduino_common::map_poi::CJsonPoiParser poi_parser_;
    ui::map_poi::PoiTileSource poi_source_{poi_files_, poi_parser_, "/"};
#endif
};

class LvglDecodedTileCache final : public ui::map_tiles::IMapTileDecoderCache
{
  public:
    static constexpr std::size_t kCapacity = 16;

    void clear() override
    {
        if (!initialized_)
        {
            return;
        }

        for (std::size_t i = 0; i < kCapacity; ++i)
        {
            freeSlot(slots_[i]);
            resetSlot(slots_[i]);
        }
    }

    bool hasDecoded(const ui::map_tiles::MapTileRef& ref) const override
    {
        return find(ref) != nullptr;
    }

    void initialize()
    {
        if (initialized_)
        {
            return;
        }

        for (std::size_t i = 0; i < kCapacity; ++i)
        {
            resetSlot(slots_[i]);
        }
        initialized_ = true;
        GPS_LOG("[GPS] Tile decode cache initialized (size=%d)\n", static_cast<int>(kCapacity));
    }

    DecodedTileCache* find(const ui::map_tiles::MapTileRef& ref) const
    {
        if (!initialized_)
        {
            return nullptr;
        }

        const uint8_t map_source = mapSourceFor(ref.layer);
        for (std::size_t i = 0; i < kCapacity; ++i)
        {
            if (slots_[i].x == static_cast<int32_t>(ref.x) &&
                slots_[i].y == static_cast<int32_t>(ref.y) &&
                slots_[i].z == static_cast<int32_t>(ref.z) &&
                slots_[i].layer == ref.layer &&
                slots_[i].map_source == map_source &&
                slots_[i].img_dsc != NULL)
            {
                slots_[i].last_used_ms = sys::millis_now();
                return &slots_[i];
            }
        }
        return nullptr;
    }

    DecodedTileCache* acquireSlot(std::size_t active_limit)
    {
        initialize();

        const int slot_count = std::max(1, std::min(static_cast<int>(active_limit), static_cast<int>(kCapacity)));
        uint32_t oldest_ms = UINT32_MAX;
        int lru_idx = -1;
        bool found_unused = false;

        for (int i = 0; i < slot_count; ++i)
        {
            if (slots_[i].img_dsc == NULL)
            {
                return &slots_[i];
            }
            if (slots_[i].lvgl_ref_count == 0)
            {
                found_unused = true;
                if (slots_[i].last_used_ms < oldest_ms)
                {
                    oldest_ms = slots_[i].last_used_ms;
                    lru_idx = i;
                }
            }
        }

        if (!found_unused || lru_idx == -1)
        {
            const uint32_t now_ms = sys::millis_now();
            if (now_ms - g_cache_full_log_ms >= 1000)
            {
                GPS_LOG("[GPS] All decoded tile cache slots are bound to LVGL objects\n");
                g_cache_full_log_ms = now_ms;
            }
            return nullptr;
        }

        GPS_LOG("[GPS] Evicting cached tile %d/%d/%d from decode cache\n",
                fmt_tile_coord(slots_[lru_idx].z),
                fmt_tile_coord(slots_[lru_idx].x),
                fmt_tile_coord(slots_[lru_idx].y));
        freeSlot(slots_[lru_idx]);
        resetSlot(slots_[lru_idx]);
        return &slots_[lru_idx];
    }

    void releaseUsage()
    {
        if (!initialized_)
        {
            return;
        }

        const uint32_t now_ms = sys::millis_now();
        for (std::size_t i = 0; i < kCapacity; ++i)
        {
            slots_[i].lvgl_ref_count = 0;
            if (slots_[i].img_dsc != NULL)
            {
                slots_[i].last_used_ms = now_ms;
            }
        }
    }

  private:
    static uint8_t mapSourceFor(ui::map_tiles::MapTileLayer layer)
    {
        switch (layer)
        {
        case ui::map_tiles::MapTileLayer::Terrain:
            return 1;
        case ui::map_tiles::MapTileLayer::Satellite:
            return 2;
        case ui::map_tiles::MapTileLayer::Osm:
        default:
            return 0;
        }
    }

    static void freeSlot(DecodedTileCache& slot)
    {
        if (slot.img_dsc != NULL)
        {
            platform::esp::map_tiles::LvglTileImage::destroy(slot.img_dsc);
            slot.img_dsc = NULL;
        }
    }

    static void resetSlot(DecodedTileCache& slot)
    {
        slot.x = -1;
        slot.y = -1;
        slot.z = -1;
        slot.map_source = 0;
        slot.layer = ui::map_tiles::MapTileLayer::Osm;
        slot.img_dsc = NULL;
        slot.last_used_ms = 0;
        slot.lvgl_ref_count = 0;
    }

    mutable DecodedTileCache slots_[kCapacity]{};
    bool initialized_ = false;
};

PathOnlyMapTileFileSystem& tile_file_system()
{
    static PathOnlyMapTileFileSystem fs;
    return fs;
}

LvglDecodedTileCache& decoded_tile_cache()
{
    static LvglDecodedTileCache* cache =
        psram_preferred_static_instance<LvglDecodedTileCache>();
    return *cache;
}

ui::map_tiles::FilesystemMapTileSource& tile_source()
{
    static ui::map_tiles::FilesystemMapTileSource source(tile_file_system(), "A:");
    return source;
}

ui::map_tiles::FilesystemMapTileSource& worker_tile_source()
{
#if defined(ARDUINO) || defined(ARDUINO_ARCH_ESP32)
    static SdMapTileFileSystem fs;
    static ui::map_tiles::FilesystemMapTileSource source(fs, "/");
#else
    static PathOnlyMapTileFileSystem fs;
    static ui::map_tiles::FilesystemMapTileSource source(fs, "A:");
#endif
    return source;
}

class MapTileAsyncHost final
{
  public:
    MapTileAsyncHost() = default;

    void acquire()
    {
        portENTER_CRITICAL(&lock_);
        ++lease_count_;
        portEXIT_CRITICAL(&lock_);
    }

    void release()
    {
        portENTER_CRITICAL(&lock_);
        if (lease_count_ > 0)
        {
            --lease_count_;
        }
        const bool last_viewport = lease_count_ == 0;
        portEXIT_CRITICAL(&lock_);
        // Wake a capacity-blocked producer even when the UI is gone. It will
        // acknowledge cancellation, then follow the normal worker shutdown.
        if (last_viewport) cancelGeneration(async_runtime_.activeGeneration());
    }

    ui::map_tiles::TileSubmitResult request(const ui::map_tiles::MapTileRef& ref,
                                            uint32_t generation,
                                            ui::map_tiles::MapTileInteractionMode mode)
    {
        if (!ensureStarted())
        {
            return {};
        }

        events_.activateGeneration(generation);
        const auto result = runtime_.requestTile(ref, generation, mode, sys::millis_now());
        MAP_DIAG("[MAPD][submit] t=%lu gen=%lu id=%lu status=%u layer=%u z=%u x=%lu y=%lu\n",
                 static_cast<unsigned long>(sys::millis_now()), static_cast<unsigned long>(generation),
                 static_cast<unsigned long>(result.handle.command_id), static_cast<unsigned>(result.status),
                 static_cast<unsigned>(ref.layer), static_cast<unsigned>(ref.z),
                 static_cast<unsigned long>(ref.x), static_cast<unsigned long>(ref.y));
        if (result.status == ui::map_tiles::TileSubmitStatus::Backpressured) ++metrics_.submission_backpressure;
        return result;
    }

    bool popEvent(ui::map_tiles::MapTileAsyncEvent& out)
    {
        if (!events_.pop(out)) return false;
        metrics_.consumed(out, sys::millis_now());
        commands_.complete({out.generation, out.command_id});
        return true;
    }

    template <typename Predicate>
    bool popEventIf(ui::map_tiles::MapTileAsyncEvent& out, Predicate eligible)
    {
        if (!events_.popIf(out, eligible)) return false;
        metrics_.consumed(out, sys::millis_now());
        commands_.complete({out.generation, out.command_id});
        return true;
    }

    bool acceptEvent(const ui::map_tiles::MapTileAsyncEvent& event,
                     ui::map_tiles::MapTileRenderQueue* render_queue)
    {
        (void)render_queue;
        ui::map_tiles::MapTileRenderQueue acceptance_queue;
        const bool accepted = runtime_.handle(event, acceptance_queue);
        MAP_DIAG("[MAPD][consume] t=%lu gen=%lu id=%lu kind=%u accepted=%d bytes=%u err=%ld\n",
                 static_cast<unsigned long>(sys::millis_now()), static_cast<unsigned long>(event.generation),
                 static_cast<unsigned long>(event.command_id), static_cast<unsigned>(event.kind), accepted,
                 static_cast<unsigned>(event.payload_size), static_cast<long>(event.error));
        return accepted;
    }

    void cancelGeneration(uint32_t generation)
    {
        MAP_DIAG("[MAPD][cancel-generation] t=%lu gen=%lu\n", static_cast<unsigned long>(sys::millis_now()), static_cast<unsigned long>(generation));
        events_.cancelGeneration(generation);
        (void)runtime_.cancelGeneration(generation);
    }

    ui::map_tiles::MapTilePipelineMetrics& metrics() { return metrics_; }

    void reportDiagnostics()
    {
#if TRAIL_MATE_MAP_DIAGNOSTICS
        std::size_t queued = 0, in_flight = 0;
        MapTileEventQueue::Statistics result;
        const bool command_ok = commands_.statistics(queued, in_flight);
        const bool result_ok = events_.statistics(result);
        MAP_DIAG("[MAPD][queues] t=%lu gen=%lu cmd_ok=%d queued=%u inflight=%u result_ok=%d occupied=%u high=%u pressure=%lu consumed=%lu rendered=%lu heap=%u psram=%u\n",
                 static_cast<unsigned long>(sys::millis_now()), static_cast<unsigned long>(async_runtime_.activeGeneration()),
                 command_ok, static_cast<unsigned>(queued), static_cast<unsigned>(in_flight), result_ok,
                 static_cast<unsigned>(result.occupied), static_cast<unsigned>(result.high_water), static_cast<unsigned long>(result.backpressure),
                 static_cast<unsigned long>(metrics_.consumed_count), static_cast<unsigned long>(metrics_.rendered_count),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
#endif
    }

    // Called only by the idle UI loader, never under queue/filesystem locks or
    // from gesture maintenance. Times are integer ms, each as average/max.
    void reportMetrics()
    {
        const auto now = sys::millis_now();
        if (now - last_metrics_ms_ < 5000 || metrics_.consumed_count == 0) return;
        MapTileEventQueue::Statistics queue;
        if (!events_.statistics(queue)) return;
        const auto& s = metrics_.stages;
        using Metrics = ui::map_tiles::MapTilePipelineMetrics;
        std::printf("[GPS][MAP][perf] consumed=%lu ready=%lu failed=%lu retry=%lu rendered=%lu "
                    "q=%u/%u reserve_bp=%lu submit_bp=%lu file_n=%lu ms(avg/max) "
                    "cmd=%lu/%lu worker=%lu/%lu lock=%lu/%lu open=%lu/%lu read=%lu/%lu "
                    "result=%lu/%lu decode=%lu/%lu apply=%lu/%lu\n",
                    static_cast<unsigned long>(metrics_.consumed_count),
                    static_cast<unsigned long>(metrics_.ready_count),
                    static_cast<unsigned long>(metrics_.failed_count),
                    static_cast<unsigned long>(metrics_.retry_count),
                    static_cast<unsigned long>(metrics_.rendered_count),
                    static_cast<unsigned>(queue.occupied), static_cast<unsigned>(queue.high_water),
                    static_cast<unsigned long>(queue.backpressure),
                    static_cast<unsigned long>(metrics_.submission_backpressure),
                    static_cast<unsigned long>(s[Metrics::FileLock].count),
                    static_cast<unsigned long>(s[Metrics::CommandWait].average()), static_cast<unsigned long>(s[Metrics::CommandWait].max_ms),
                    static_cast<unsigned long>(s[Metrics::Worker].average()), static_cast<unsigned long>(s[Metrics::Worker].max_ms),
                    static_cast<unsigned long>(s[Metrics::FileLock].average()), static_cast<unsigned long>(s[Metrics::FileLock].max_ms),
                    static_cast<unsigned long>(s[Metrics::FileOpen].average()), static_cast<unsigned long>(s[Metrics::FileOpen].max_ms),
                    static_cast<unsigned long>(s[Metrics::FileRead].average()), static_cast<unsigned long>(s[Metrics::FileRead].max_ms),
                    static_cast<unsigned long>(s[Metrics::ResultWait].average()), static_cast<unsigned long>(s[Metrics::ResultWait].max_ms),
                    static_cast<unsigned long>(s[Metrics::Decode].average()), static_cast<unsigned long>(s[Metrics::Decode].max_ms),
                    static_cast<unsigned long>(s[Metrics::Apply].average()), static_cast<unsigned long>(s[Metrics::Apply].max_ms));
        if (metrics_.block_samples)
            std::printf("[GPS][MAP][block] files=%lu calls=%llu sectors=%llu max_batch=%lu total_us=%llu\n",
                        static_cast<unsigned long>(metrics_.block_samples),
                        static_cast<unsigned long long>(metrics_.block_calls),
                        static_cast<unsigned long long>(metrics_.block_sectors),
                        static_cast<unsigned long>(metrics_.block_max_sectors),
                        static_cast<unsigned long long>(metrics_.block_us));
        metrics_ = {};
        last_metrics_ms_ = now;
    }

  private:
    static void taskThunk(void* self)
    {
        static_cast<MapTileAsyncHost*>(self)->taskLoop();
    }

    void taskLoop()
    {
        ui::map_tiles::LoadTileCommand command{};
        bool have_command = false;
        for (;;)
        {
            if (!have_command)
            {
                have_command = commands_.pop(sys::millis_now(), command);
                if (have_command)
                    MAP_DIAG("[MAPD][worker-start] t=%lu gen=%lu id=%lu layer=%u z=%u x=%lu y=%lu\n",
                             static_cast<unsigned long>(sys::millis_now()), static_cast<unsigned long>(command.runtime.generation),
                             static_cast<unsigned long>(command.runtime.command_id), static_cast<unsigned>(command.tile.layer),
                             static_cast<unsigned>(command.tile.z), static_cast<unsigned long>(command.tile.x), static_cast<unsigned long>(command.tile.y));
            }
            if (have_command)
            {
                if (worker_ != nullptr)
                {
                    const auto result = worker_->execute(command, sys::millis_now());
                    if (result == ui::map_tiles::MapTileExecutionStatus::Backpressured)
                    {
#if TRAIL_MATE_MAP_DIAGNOSTICS
                        static uint32_t last_pressure_ms = 0;
                        if (sys::millis_now() - last_pressure_ms >= 2000U)
                        {
                            last_pressure_ms = sys::millis_now();
                            MAP_DIAG("[MAPD][worker-capacity-wait] t=%lu id=%lu\n", static_cast<unsigned long>(last_pressure_ms), static_cast<unsigned long>(command.runtime.command_id));
                        }
#endif
                        // Retain the command; no SD lock is held while waiting.
                        events_.waitForCapacity(pdMS_TO_TICKS(20));
                        continue;
                    }
                    MAP_DIAG("[MAPD][worker-end] t=%lu gen=%lu id=%lu status=%u\n",
                             static_cast<unsigned long>(sys::millis_now()), static_cast<unsigned long>(command.runtime.generation),
                             static_cast<unsigned long>(command.runtime.command_id), static_cast<unsigned>(result));
                }
                have_command = false;
                vTaskDelay(kMapTileWorkerPostCommandYieldTicks);
                continue;
            }
            bool idle = false;
            portENTER_CRITICAL(&lock_);
            if (lease_count_ == 0)
            {
                stopping_ = true;
                idle = true;
            }
            portEXIT_CRITICAL(&lock_);
            if (idle)
            {
                events_.clear();
                delete worker_;
                worker_ = nullptr;
                if (scratch_ != nullptr)
                {
                    heap_caps_free(scratch_);
                    scratch_ = nullptr;
                }
                portENTER_CRITICAL(&lock_);
                task_ = nullptr;
                started_ = false;
                stopping_ = false;
                portEXIT_CRITICAL(&lock_);
                std::printf("[GPS][MAP][worker] stopped reason=no_viewports\n");
                vTaskDelete(nullptr);
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    bool ensureStarted()
    {
        if (!events_.available() || !commands_.available()) return false;
        portENTER_CRITICAL(&lock_);
        const bool started = started_;
        const bool stopping = stopping_;
        portEXIT_CRITICAL(&lock_);
        if (stopping)
        {
            return false;
        }
        if (started)
        {
            return true;
        }

        if (scratch_ == nullptr)
        {
#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
            // The worker also constructs typed annotation records. Ordinary
            // ESP heap allocation only guarantees the allocator's alignment,
            // which need not satisfy TileHeader's explicit 8-byte alignment.
            scratch_ = static_cast<uint8_t*>(heap_caps_aligned_alloc(alignof(ui::map_poi::TileHeader),
                                                                     kMapTileWorkerScratchBytes,
                                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
            scratch_ = allocate_tile_payload(kMapTileWorkerScratchBytes);
#endif
            if (scratch_ == nullptr)
            {
                if (!scratch_alloc_failed_logged_)
                {
                    std::printf("[GPS][MAP][worker] scratch_alloc_failed bytes=%u\n",
                                static_cast<unsigned>(kMapTileWorkerScratchBytes));
                    scratch_alloc_failed_logged_ = true;
                }
                return false;
            }
        }

        if (worker_ == nullptr)
        {
            backend_.resetMetadata();
            worker_ = new (std::nothrow)
                ui::map_tiles::MapTileWorker(backend_,
                                             events_,
                                             scratch_,
                                             kMapTileWorkerScratchBytes);
            if (worker_ == nullptr)
            {
                if (!task_start_failed_logged_)
                {
                    std::printf("[GPS][MAP][worker] worker_alloc_failed\n");
                    task_start_failed_logged_ = true;
                }
                return false;
            }
        }

        const BaseType_t ok = xTaskCreate(taskThunk,
                                          "map_tile_worker",
                                          kMapTileWorkerTaskStackBytes,
                                          this,
                                          1,
                                          &task_);
        if (ok != pdPASS)
        {
            if (!task_start_failed_logged_)
            {
                std::printf("[GPS][MAP][worker] task_start_failed rc=%ld "
                            "stack=dynamic_internal bytes=%u internal_free=%u "
                            "internal_largest=%u\n",
                            static_cast<long>(ok),
                            static_cast<unsigned>(kMapTileWorkerTaskStackBytes),
                            static_cast<unsigned>(
                                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                            static_cast<unsigned>(heap_caps_get_largest_free_block(
                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
                task_start_failed_logged_ = true;
            }
            return false;
        }
        portENTER_CRITICAL(&lock_);
        started_ = true;
        portEXIT_CRITICAL(&lock_);
        return true;
    }

    MapTileCommandQueue commands_{};
    ui::map_tiles::MapTilePipelineMetrics metrics_{};
    uint32_t last_metrics_ms_ = 0;
    MapTileEventQueue events_{copy_map_tile_event, release_tile_payload};
    EspMapTileWorkerBackend backend_{worker_tile_source()};
    uint8_t* scratch_ = nullptr;
    ui::map_tiles::MapTileWorker* worker_ = nullptr;
    ui::map_tiles::MapTileAsyncRuntime async_runtime_{commands_};
    ui::map_tiles::MapTileStateMachine state_machine_{};
    ui::map_tiles::MapTileRuntime runtime_{async_runtime_, state_machine_};
    TaskHandle_t task_ = nullptr;
    bool started_ = false;
    bool stopping_ = false;
    bool scratch_alloc_failed_logged_ = false;
    bool task_start_failed_logged_ = false;
    std::size_t lease_count_ = 0;
    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
};

MapTileAsyncHost& map_tile_async_host()
{
    static MapTileAsyncHost* host = psram_preferred_static_instance<MapTileAsyncHost>();
    return *host;
}

uint8_t clamp_tile_zoom(int z)
{
    if (z < 0)
    {
        return 0;
    }
    if (z > 255)
    {
        return 255;
    }
    return static_cast<uint8_t>(z);
}

ui::map_tiles::MapTileRef base_tile_ref(int z, int x, int y, uint8_t map_source)
{
    ui::map_tiles::MapTileRef ref{};
    ref.layer = ui::map_tiles::mapTileLayerFromBaseSource(sanitize_map_source(map_source));
    ref.z = clamp_tile_zoom(z);
    ref.x = static_cast<uint32_t>(x < 0 ? 0 : x);
    ref.y = static_cast<uint32_t>(y < 0 ? 0 : y);
    return ref;
}

uint8_t map_source_for_layer(ui::map_tiles::MapTileLayer layer)
{
    switch (layer)
    {
    case ui::map_tiles::MapTileLayer::Terrain:
        return 1;
    case ui::map_tiles::MapTileLayer::Satellite:
        return 2;
    case ui::map_tiles::MapTileLayer::Osm:
    default:
        return 0;
    }
}

bool contour_tile_ref(int z, int x, int y, ui::map_tiles::MapTileRef& out)
{
    bool supported = false;
    const auto layer = ui::map_tiles::mapTileContourLayerForZoom(z, &supported);
    if (!supported)
    {
        return false;
    }

    out.layer = layer;
    out.z = clamp_tile_zoom(z);
    out.x = static_cast<uint32_t>(x < 0 ? 0 : x);
    out.y = static_cast<uint32_t>(y < 0 ? 0 : y);
    return true;
}

ui::map_tiles::MapTileRef base_tile_ref_for_tile(const MapTile& tile)
{
    return base_tile_ref(tile.z,
                         static_cast<int>(tile.x),
                         static_cast<int>(tile.y),
                         g_active_map_source);
}

int16_t clamp_screen_coord(int value)
{
    if (value < -32768)
    {
        return -32768;
    }
    if (value > 32767)
    {
        return 32767;
    }
    return static_cast<int16_t>(value);
}

ui::map_tiles::MapTileRenderState tile_render_state(const MapTile& tile)
{
    if (tile.has_png_file)
    {
        return ui::map_tiles::MapTileRenderState::Ready;
    }
    if (tile.base_missing)
    {
        return ui::map_tiles::MapTileRenderState::Missing;
    }
    return ui::map_tiles::MapTileRenderState::Loading;
}

void rebuild_render_queue(TileContext& ctx)
{
    if (!ctx.render_queue)
    {
        return;
    }

    ctx.render_queue->clear();
    if (!ctx.tiles || !ctx.anchor || !ctx.anchor->valid)
    {
        return;
    }

    for (const auto& tile : *ctx.tiles)
    {
        if (!tile.visible)
        {
            continue;
        }

        int screen_x = 0;
        int screen_y = 0;
        if (!tile_screen_pos_xyz(ctx, tile.x, tile.y, tile.z, screen_x, screen_y))
        {
            continue;
        }

        ui::map_tiles::MapTileRenderRef item{};
        item.tile = base_tile_ref_for_tile(tile);
        item.rect.x = clamp_screen_coord(screen_x);
        item.rect.y = clamp_screen_coord(screen_y);
        item.rect.width = TILE_SIZE;
        item.rect.height = TILE_SIZE;
        item.state = tile_render_state(tile);
        ctx.render_queue->push(item);
    }
}

void summarize_visible_tiles(const TileContext& ctx,
                             int& visible_total,
                             int& visible_loaded,
                             int& visible_placeholder,
                             int& visible_unloaded)
{
    visible_total = 0;
    visible_loaded = 0;
    visible_placeholder = 0;
    visible_unloaded = 0;

    if (!ctx.tiles)
    {
        return;
    }

    for (const auto& tile : *ctx.tiles)
    {
        if (!tile.visible)
        {
            continue;
        }
        ++visible_total;
        if (tile.has_png_file)
        {
            ++visible_loaded;
        }
        else if (tile.img_obj != NULL)
        {
            ++visible_placeholder;
        }
        else
        {
            ++visible_unloaded;
        }
    }
}

void update_visible_map_data_flag(TileContext& ctx)
{
    if (!ctx.has_visible_map_data || !ctx.tiles)
    {
        return;
    }

    bool visible_png_found = false;
    for (auto& tile : *ctx.tiles)
    {
        if (tile.visible && tile.has_png_file)
        {
            visible_png_found = true;
            break;
        }
    }
    *ctx.has_visible_map_data = visible_png_found;
}

size_t tiles_covering_axis(lv_coord_t axis_px)
{
    const lv_coord_t clamped = std::max<lv_coord_t>(axis_px, 1);
    return static_cast<size_t>((clamped + TILE_SIZE - 1) / TILE_SIZE) + 1U;
}

size_t viewport_tile_capacity(const TileContext& ctx)
{
    if (!ctx.map_container)
    {
        return TILE_CACHE_LIMIT;
    }

    const size_t cols = tiles_covering_axis(lv_obj_get_width(ctx.map_container));
    const size_t rows = tiles_covering_axis(lv_obj_get_height(ctx.map_container));
    return cols * rows;
}

size_t tile_object_cache_limit(const TileContext& ctx)
{
    if (!ctx.map_container)
    {
        return TILE_CACHE_LIMIT;
    }

    const size_t cols = tiles_covering_axis(lv_obj_get_width(ctx.map_container));
    const size_t rows = tiles_covering_axis(lv_obj_get_height(ctx.map_container));
    const size_t visible_tiles = cols * rows;
    const size_t cushion = std::max(cols, rows);
    return std::max<size_t>(TILE_CACHE_LIMIT, visible_tiles + cushion);
}

size_t tile_record_limit(const TileContext& ctx)
{
    const size_t desired = std::max<size_t>(TILE_RECORD_LIMIT, viewport_tile_capacity(ctx) * 4U);
    return std::min<size_t>(desired, 160U);
}

size_t tile_decode_cache_limit(const TileContext& ctx)
{
    const size_t desired = std::max<size_t>(TILE_CACHE_LIMIT, tile_object_cache_limit(ctx));
    const size_t profile_limit =
        std::max<std::size_t>(1U, ::ui::runtime::current_memory_profile().max_map_decode_tiles);
    return std::min<size_t>(std::min<size_t>(desired, profile_limit),
                            static_cast<size_t>(LvglDecodedTileCache::kCapacity));
}

} // namespace

uint8_t sanitize_map_source(uint8_t map_source)
{
    return map_source <= 2 ? map_source : 0;
}

const char* map_source_label(uint8_t map_source)
{
    switch (sanitize_map_source(map_source))
    {
    case 1:
        return "Terrain";
    case 2:
        return "Satellite";
    case 0:
    default:
        return "OSM";
    }
}

bool build_base_tile_path(int z, int x, int y, uint8_t map_source, char* out_path, size_t out_size)
{
    return tile_source().resolvePath(base_tile_ref(z, x, y, map_source),
                                     out_path,
                                     out_size);
}

bool base_tile_available(int z, int x, int y, uint8_t map_source)
{
    const ui::map_tiles::MapTileRef ref = base_tile_ref(z, x, y, map_source);
    return !map_tile_availability_memory().knownMissing(ref);
}

bool build_contour_tile_path(int z, int x, int y, char* out_path, size_t out_size)
{
    ui::map_tiles::MapTileRef ref{};
    if (!contour_tile_ref(z, x, y, ref))
    {
        return false;
    }
    return tile_source().resolvePath(ref, out_path, out_size);
}

bool map_source_directory_available(uint8_t map_source)
{
#if defined(ARDUINO) || defined(ARDUINO_ARCH_ESP32)
    (void)map_source;
    return true;
#else
    return tile_source().layerDirectoryAvailable(
        ui::map_tiles::mapTileLayerFromBaseSource(sanitize_map_source(map_source)));
#endif
}

bool contour_directory_available()
{
#if defined(ARDUINO) || defined(ARDUINO_ARCH_ESP32)
    return true;
#else
    return tile_source().anyContourDirectoryAvailable();
#endif
}

bool take_missing_tile_notice(uint8_t* out_map_source)
{
    if (!g_missing_tile_notice_pending)
    {
        return false;
    }
    g_missing_tile_notice_pending = false;
    if (out_map_source != NULL)
    {
        *out_map_source = g_missing_tile_notice_source;
    }
    return true;
}

void set_map_render_options(uint8_t map_source, bool contour_enabled)
{
    g_requested_map_source = sanitize_map_source(map_source);
    g_requested_contour_enabled = contour_enabled;
}

static void clear_tile_decode_cache()
{
    decoded_tile_cache().clear();
}

static void release_tile_decode_cache_usage()
{
    decoded_tile_cache().releaseUsage();
}

/**
 * Find cached decoded tile image
 */
static DecodedTileCache* find_cached_tile(int x, int y, int z, uint8_t map_source)
{
    return decoded_tile_cache().find(base_tile_ref(z, x, y, map_source));
}

static DecodedTileCache* find_cached_tile_ref(const ui::map_tiles::MapTileRef& ref)
{
    return decoded_tile_cache().find(ref);
}

/**
 * Get least recently used cache slot.
 * Returns NULL if every slot is still referenced by a live LVGL image object.
 */
static DecodedTileCache* get_lru_cache_slot(size_t active_limit)
{
    return decoded_tile_cache().acquireSlot(active_limit);
}

static bool cache_matches_ref(const DecodedTileCache& cache,
                              const ui::map_tiles::MapTileRef& ref)
{
    return cache.x == static_cast<int32_t>(ref.x) &&
           cache.y == static_cast<int32_t>(ref.y) &&
           cache.z == static_cast<int32_t>(ref.z) &&
           cache.layer == ref.layer;
}

/**
 * Normalize tile coordinates to valid range (wrap x, clamp y)
 */
void normalize_tile(int z, int& x, int& y)
{
    ::ui::map_tiles::normalizeTile(z, x, y);
}

/**
 * Convert latitude/longitude to tile coordinates
 * Returns wrapped/clamped tile coordinates
 */
void latLngToTile(double lat, double lng, int zoom, int& tile_x, int& tile_y)
{
    ::ui::map_tiles::latLngToTile(lat, lng, zoom, tile_x, tile_y);
}

/**
 * Convert tile coordinates to latitude/longitude (inverse of latLngToTile)
 * This is used to calculate the center of the current map view
 */
void tileToLatLng(int tile_x, int tile_y, int zoom, double& lat, double& lng)
{
    double n = pow(2.0, zoom);

    // Convert tile coordinates to longitude (center of tile, not top-left corner)
    // Add 0.5 to get tile center instead of top-left corner
    lng = ((tile_x + 0.5) / n) * 360.0 - 180.0;

    // Convert tile coordinates to latitude (center of tile, not top-left corner)
    // Inverse of: tile_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * n
    // Add 0.5 to get tile center instead of top-left corner
    double y_ratio = (tile_y + 0.5) / n;
    double lat_rad = atan(sinh(M_PI * (1.0 - 2.0 * y_ratio)));
    lat = lat_rad * 180.0 / M_PI;

    // Clamp latitude to valid range
    const double MAX_LAT = 85.05112878;
    if (lat > MAX_LAT) lat = MAX_LAT;
    if (lat < -MAX_LAT) lat = -MAX_LAT;
}

/**
 * Calculate the latitude/longitude of the current screen center
 * Uses the current anchor and pan offsets to determine what's at screen center
 */
void get_screen_center_lat_lng(const TileContext& ctx, double& lat, double& lng)
{
    lat = 0.0;
    lng = 0.0;

    if (!ctx.map_container || !ctx.anchor || !ctx.anchor->valid)
    {
        return;
    }

    lv_coord_t w = lv_obj_get_width(ctx.map_container);
    lv_coord_t h = lv_obj_get_height(ctx.map_container);

    // CRITICAL FIX: Calculate pan from anchor
    // GPS point is placed at (w/2 + pan_x) on screen
    // So: pan_x = gps_tile_screen_x + gps_offset_x - w/2
    int32_t pan_x = (int32_t)(ctx.anchor->gps_tile_screen_x + ctx.anchor->gps_offset_x) - (int32_t)(w / 2);
    int32_t pan_y = (int32_t)(ctx.anchor->gps_tile_screen_y + ctx.anchor->gps_offset_y) - (int32_t)(h / 2);

    // Screen center corresponds to: GPS global pixel - pan
    // This is the correct geometric relationship from update_map_anchor()
    int64_t cx = (int64_t)ctx.anchor->gps_global_pixel_x - (int64_t)pan_x;
    int64_t cy = (int64_t)ctx.anchor->gps_global_pixel_y - (int64_t)pan_y;

    // World pixel width at current zoom level
    int32_t world_px = (int32_t)(ctx.anchor->n * TILE_SIZE);

    // X must wrap (world is a cylinder, longitude wraps)
    int64_t x = cx % world_px;
    if (x < 0) x += world_px;

    // Y must clamp (Mercator projection has bounds at poles)
    int64_t y = cy;
    if (y < 0) y = 0;
    if (y >= world_px) y = world_px - 1;

    // Convert world pixel coordinates to lat/lng (WebMercator inverse)
    lng = ((double)x / (double)world_px) * 360.0 - 180.0;

    double y_ratio = (double)y / (double)world_px;
    double lat_rad = atan(sinh(M_PI * (1.0 - 2.0 * y_ratio)));
    lat = lat_rad * 180.0 / M_PI;

    // Clamp latitude to valid range (safety check)
    const double MAX_LAT = 85.05112878;
    if (lat > MAX_LAT) lat = MAX_LAT;
    if (lat < -MAX_LAT) lat = -MAX_LAT;
}

/**
 * Convert tile coordinates to pixel coordinates
 */
void tileToPixel(int tile_x, int tile_y, int zoom, int& pixel_x, int& pixel_y)
{
    (void)zoom; // Unused but kept for API consistency
    // TILE_SIZE = 256 = 2^8, so tile_x * 256 = tile_x << 8
    pixel_x = tile_x << 8;
    pixel_y = tile_y << 8;
}

/**
 * Calculate screen position for tile by xyz coordinates
 */
bool tile_screen_pos_xyz(const TileContext& ctx, int x, int y, int z, int& sx, int& sy)
{
    if (!ctx.map_container) return false;

    lv_coord_t w = lv_obj_get_width(ctx.map_container);
    lv_coord_t h = lv_obj_get_height(ctx.map_container);

    if (!ctx.anchor || !ctx.anchor->valid)
    {
        // No GPS: center tile 0/0/0
        sx = (w - TILE_SIZE) / 2;
        sy = (h - TILE_SIZE) / 2;
        return true;
    }

    // CRITICAL FIX: Handle tile coordinate wrapping (normalize_tile can cause neighbor tiles to wrap)
    // Use shortest wrap distance to preserve neighbor relationships across date line
    int n = 1 << z; // Number of tiles at this zoom level

    // Calculate dx with shortest wrap (handles date line crossing)
    int dx = x - ctx.anchor->gps_tile_x;
    if (dx > n / 2) dx -= n;  // Wrap: if dx > n/2, go the other way
    if (dx < -n / 2) dx += n; // Wrap: if dx < -n/2, go the other way

    // For y, no wrapping needed (latitude is clamped, not wrapped)
    int dy = y - ctx.anchor->gps_tile_y;

    // Calculate tile pixel coordinates using wrapped dx/dy
    // This preserves neighbor relationships even when normalize_tile wraps x
    int tile_px = (ctx.anchor->gps_tile_x + dx) << 8;
    int tile_py = (ctx.anchor->gps_tile_y + dy) << 8;

    // Use cached anchor for screen position calculation
    sx = ctx.anchor->gps_tile_screen_x + (tile_px - ctx.anchor->gps_tile_pixel_x);
    sy = ctx.anchor->gps_tile_screen_y + (tile_py - ctx.anchor->gps_tile_pixel_y);
    return true;
}

/**
 * Calculate screen position for GPS coordinates (lat/lng)
 * Uses the same algorithm as update_map_anchor to ensure consistency
 */
bool gps_screen_pos(const TileContext& ctx, double lat, double lng, int& sx, int& sy)
{
    if (!ctx.map_container || !ctx.anchor || !ctx.anchor->valid)
    {
        return false;
    }

    lv_coord_t screen_width = lv_obj_get_width(ctx.map_container);
    lv_coord_t screen_height = lv_obj_get_height(ctx.map_container);
    int zoom = ctx.anchor->z;

    // Clamp latitude to WebMercator valid range
    double lat_clamped = lat;
    const double MAX_LAT = 85.05112878;
    if (lat_clamped > MAX_LAT) lat_clamped = MAX_LAT;
    if (lat_clamped < -MAX_LAT) lat_clamped = -MAX_LAT;

    // Wrap longitude to [-180, 180)
    double lng_wrapped = lng;
    while (lng_wrapped < -180.0) lng_wrapped += 360.0;
    while (lng_wrapped >= 180.0) lng_wrapped -= 360.0;

    // Calculate GPS global pixel coordinates (same as update_map_anchor)
    double n = pow(2.0, zoom);
    double lat_rad = lat_clamped * M_PI / 180.0;
    double gps_pixel_x = ((lng_wrapped + 180.0) / 360.0 * n) * TILE_SIZE;
    double gps_pixel_y = ((1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * n) * TILE_SIZE;

    int32_t gps_global_pixel_x = (int32_t)floor(gps_pixel_x);
    int32_t gps_global_pixel_y = (int32_t)floor(gps_pixel_y);

    // Calculate screen position relative to anchor
    // GPS position = anchor position + (GPS pixel - anchor pixel)
    int32_t dx = gps_global_pixel_x - ctx.anchor->gps_global_pixel_x;
    int32_t dy = gps_global_pixel_y - ctx.anchor->gps_global_pixel_y;

    // The map is horizontally periodic. Match tile_screen_pos_xyz() and use
    // the shortest route across the date line rather than spanning the world.
    const int32_t world_px = static_cast<int32_t>(n * TILE_SIZE);
    if (dx > world_px / 2)
    {
        dx -= world_px;
    }
    else if (dx < -world_px / 2)
    {
        dx += world_px;
    }

    sx = ctx.anchor->gps_tile_screen_x + ctx.anchor->gps_offset_x + dx;
    sy = ctx.anchor->gps_tile_screen_y + ctx.anchor->gps_offset_y + dy;

    return true;
}

/**
 * Unified visibility check
 */
bool tile_in_rect(int sx, int sy, int w, int h, int margin)
{
    return (sx + TILE_SIZE >= -margin && sx < w + margin &&
            sy + TILE_SIZE >= -margin && sy < h + margin);
}

/**
 * Find existing tile by coordinates
 */
static MapTile* find_tile(TileContext& ctx, int x, int y, int z)
{
    if (!ctx.tiles) return NULL;
    for (auto& tile : *ctx.tiles)
    {
        if (tile.x == x && tile.y == y && tile.z == z)
        {
            return &tile;
        }
    }
    return NULL;
}

/**
 * Ensure tile record exists and set fields
 */
static MapTile& ensure_tile(TileContext& ctx, int x, int y, int z, int priority)
{
    if (!ctx.tiles)
    {
        GPS_LOG("[GPS] ERROR: ctx.tiles is NULL in ensure_tile\n");
        static MapTile dummy;
        return dummy;
    }

    MapTile* existing = find_tile(ctx, x, y, z);
    if (existing != NULL)
    {
        existing->visible = true;
        existing->ever_visible = true;
        existing->last_used_ms = sys::millis_now();
        existing->priority = priority;
        existing->map_source = g_active_map_source;
        return *existing;
    }

    // Create new tile record
    MapTile t{};
    t.x = x;
    t.y = y;
    t.z = z;
    t.map_source = g_active_map_source;
    t.img_obj = NULL;
    t.contour_obj = NULL;
    t.visible = true;
    t.ever_visible = true;
    t.last_used_ms = sys::millis_now();
    t.obj_evicted_ms = 0;
    t.record_evicted = false;
    t.priority = priority;
    t.has_png_file = false;
    t.base_missing = false;
    t.base_request_pending = false;
    t.base_request_generation = 0;
    t.base_retry_not_before_ms = 0;
    t.contour_checked = false;
    t.contour_loaded = false;
    t.contour_request_pending = false;
    t.contour_request_generation = 0;
    t.contour_retry_not_before_ms = 0;
    t.cached_img = NULL; // No cached image initially
    t.contour_cached_img = NULL;
    ctx.tiles->push_back(std::move(t));
    return ctx.tiles->back();
}

/**
 * Style helper functions
 */
static void style_tile_obj(lv_obj_t* o)
{
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(o, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_all(o, 0, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_placeholder_card(lv_obj_t* card)
{
    style_tile_obj(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFF9F3), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0xEADFCF), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 4, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_placeholder_text(lv_obj_t* label)
{
    lv_obj_set_style_text_color(label, lv_color_hex(0x8A7A68), LV_PART_MAIN);
    lv_obj_set_style_text_opa(label, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
}

static void release_tile_decoded_cache(MapTile& tile)
{
    if (tile.cached_img != NULL)
    {
        if (tile.cached_img->lvgl_ref_count > 0)
        {
            --tile.cached_img->lvgl_ref_count;
        }
        tile.cached_img = NULL;
    }
}

static void release_contour_decoded_cache(MapTile& tile)
{
    if (tile.contour_cached_img != NULL)
    {
        if (tile.contour_cached_img->lvgl_ref_count > 0)
        {
            --tile.contour_cached_img->lvgl_ref_count;
        }
        tile.contour_cached_img = NULL;
    }
}

static void bind_tile_decoded_cache(MapTile& tile, DecodedTileCache& cache)
{
    release_tile_decoded_cache(tile);
    if (cache.lvgl_ref_count < UINT8_MAX)
    {
        ++cache.lvgl_ref_count;
    }
    cache.last_used_ms = sys::millis_now();
    tile.cached_img = &cache;
}

static void bind_contour_decoded_cache(MapTile& tile, DecodedTileCache& cache)
{
    release_contour_decoded_cache(tile);
    if (cache.lvgl_ref_count < UINT8_MAX)
    {
        ++cache.lvgl_ref_count;
    }
    cache.last_used_ms = sys::millis_now();
    tile.contour_cached_img = &cache;
}

static void touch_tile_decoded_cache(MapTile& tile)
{
    if (tile.cached_img == NULL)
    {
        if (tile.contour_cached_img == NULL)
        {
            return;
        }
    }

    if (tile.cached_img != NULL && tile.img_obj == NULL)
    {
        release_tile_decoded_cache(tile);
    }
    else if (tile.cached_img != NULL)
    {
        tile.cached_img->last_used_ms = sys::millis_now();
    }

    if (tile.contour_cached_img != NULL && tile.contour_obj == NULL)
    {
        release_contour_decoded_cache(tile);
    }
    else if (tile.contour_cached_img != NULL)
    {
        tile.contour_cached_img->last_used_ms = sys::millis_now();
    }
}

static void refresh_live_tile_decode_cache_usage(TileContext& ctx)
{
    if (!ctx.tiles)
    {
        return;
    }

    for (auto& tile : *ctx.tiles)
    {
        touch_tile_decoded_cache(tile);
    }
}

static void reset_tile_runtime(MapTile& tile)
{
    if (tile.img_obj != NULL)
    {
        lv_obj_del(tile.img_obj);
        tile.img_obj = NULL;
    }
    release_tile_decoded_cache(tile);
    release_contour_decoded_cache(tile);
    tile.contour_obj = NULL; // contour object is a child of img_obj and is deleted with it
    tile.has_png_file = false;
    tile.base_missing = false;
    tile.base_request_pending = false;
    tile.base_request_generation = 0;
    tile.base_retry_not_before_ms = 0;
    tile.contour_checked = false;
    tile.contour_loaded = false;
    tile.contour_request_pending = false;
    tile.contour_request_generation = 0;
    tile.contour_retry_not_before_ms = 0;
}

static bool evict_invisible_cached_tile_object(TileContext& ctx)
{
    if (!ctx.tiles)
    {
        return false;
    }

    size_t best_idx = ctx.tiles->size();
    uint32_t oldest_ms = UINT32_MAX;
    for (size_t i = 0; i < ctx.tiles->size(); ++i)
    {
        MapTile& candidate = (*ctx.tiles)[i];
        if (candidate.visible ||
            candidate.img_obj == NULL ||
            (candidate.cached_img == NULL && candidate.contour_cached_img == NULL))
        {
            continue;
        }

        if (candidate.last_used_ms <= oldest_ms)
        {
            oldest_ms = candidate.last_used_ms;
            best_idx = i;
        }
    }

    if (best_idx >= ctx.tiles->size())
    {
        return false;
    }

    MapTile& evicted = (*ctx.tiles)[best_idx];
    GPS_LOG("[GPS] Evicting invisible tile object %d/%d/%d to release decoded cache\n",
            evicted.z,
            fmt_tile_coord(evicted.x),
            fmt_tile_coord(evicted.y));
    reset_tile_runtime(evicted);
    evicted.obj_evicted_ms = sys::millis_now();
    return true;
}

static void reset_all_tiles_for_render_change(TileContext& ctx)
{
    if (!ctx.tiles)
    {
        return;
    }
    for (auto& tile : *ctx.tiles)
    {
        reset_tile_runtime(tile);
        tile.visible = false;
    }
    ctx.tiles->clear();
    if (ctx.has_map_data)
    {
        *ctx.has_map_data = false;
    }
    if (ctx.has_visible_map_data)
    {
        *ctx.has_visible_map_data = false;
    }
}

static void sync_render_settings(TileContext& ctx)
{
    uint8_t map_source = sanitize_map_source(g_requested_map_source);
    bool contour_enabled = g_requested_contour_enabled;

    if (map_source == g_active_map_source && contour_enabled == g_active_contour_enabled)
    {
        return;
    }

    bool source_changed = (map_source != g_active_map_source);
    const uint32_t previous_generation = g_map_tile_runtime_generation;
    g_active_map_source = map_source;
    g_active_contour_enabled = contour_enabled;
    ++g_map_tile_runtime_generation;
    if (g_map_tile_runtime_generation == 0)
    {
        g_map_tile_runtime_generation = kMapTileGenerationInitial;
    }
    map_tile_async_host().cancelGeneration(previous_generation);

    reset_all_tiles_for_render_change(ctx);
    if (source_changed)
    {
        g_missing_tile_notice_pending = false;
        g_missing_tile_notice_emitted = false;
        g_missing_tile_notice_source = map_source;
    }

    if (source_changed)
    {
        lv_image_cache_drop(NULL);
        clear_tile_decode_cache();
    }
}

static void mark_missing_base_tile(TileContext& ctx, MapTile& tile)
{
    if (!ctx.map_container)
    {
        return;
    }

    int screen_x = 0;
    int screen_y = 0;
    if (!tile_screen_pos_xyz(ctx, tile.x, tile.y, tile.z, screen_x, screen_y))
    {
        return;
    }

    if (tile.img_obj != NULL)
    {
        reset_tile_runtime(tile);
    }
    create_placeholder_tile_card(ctx.map_container, tile, screen_x, screen_y);
    tile.base_missing = true;
    tile.base_request_pending = false;
    tile.base_retry_not_before_ms = 0;

    if (!g_missing_tile_notice_emitted)
    {
        g_missing_tile_notice_emitted = true;
        g_missing_tile_notice_pending = true;
        g_missing_tile_notice_source = g_active_map_source;
    }
}

static DecodedTileCache* decode_payload_to_cache(TileContext& ctx,
                                                 const ui::map_tiles::MapTileRef& ref,
                                                 const ui::map_tiles::MapTilePayload& payload)
{
    if (payload.data == nullptr || payload.size == 0)
    {
        return nullptr;
    }

    DecodedTileCache* cached = find_cached_tile_ref(ref);
    if (cached != nullptr && cached->img_dsc != NULL)
    {
        return cached;
    }

    refresh_live_tile_decode_cache_usage(ctx);
    DecodedTileCache* cache_slot = get_lru_cache_slot(tile_decode_cache_limit(ctx));
    if (cache_slot == NULL && evict_invisible_cached_tile_object(ctx))
    {
        refresh_live_tile_decode_cache_usage(ctx);
        cache_slot = get_lru_cache_slot(tile_decode_cache_limit(ctx));
    }
    if (cache_slot == NULL)
    {
        return nullptr;
    }

    lv_image_dsc_t* img_dsc = decode_payload_to_image_desc(ref, payload);
    if (img_dsc == nullptr)
    {
        return nullptr;
    }

    cache_slot->img_dsc = img_dsc;
    cache_slot->x = static_cast<int32_t>(ref.x);
    cache_slot->y = static_cast<int32_t>(ref.y);
    cache_slot->z = static_cast<int32_t>(ref.z);
    cache_slot->layer = ref.layer;
    cache_slot->map_source = map_source_for_layer(ref.layer);
    cache_slot->last_used_ms = sys::millis_now();
    cache_slot->lvgl_ref_count = 0;
    return cache_slot;
}

static bool render_base_tile_from_cache(TileContext& ctx, MapTile& tile, DecodedTileCache& cache)
{
    if (!ctx.map_container || cache.img_dsc == NULL)
    {
        return false;
    }

    const ui::map_tiles::MapTileRef expected_ref = base_tile_ref_for_tile(tile);
    if (!cache_matches_ref(cache, expected_ref))
    {
        log_map_tile_decode_failure("cache_ref_mismatch",
                                    expected_ref,
                                    ui::map_tiles::MapTileFormat::Unknown,
                                    0,
                                    static_cast<long>(cache.layer));
        return false;
    }

    int screen_x = 0;
    int screen_y = 0;
    if (!tile_screen_pos_xyz(ctx, tile.x, tile.y, tile.z, screen_x, screen_y))
    {
        return false;
    }

    const lv_coord_t screen_width = lv_obj_get_width(ctx.map_container);
    const lv_coord_t screen_height = lv_obj_get_height(ctx.map_container);
    tile.visible = tile_in_rect(screen_x, screen_y, screen_width, screen_height, 0);
    if (!tile.visible)
    {
        return false;
    }

    if (tile.img_obj != NULL)
    {
        reset_tile_runtime(tile);
    }

    tile.img_obj = lv_image_create(ctx.map_container);
    lv_obj_set_size(tile.img_obj, TILE_SIZE, TILE_SIZE);
    lv_obj_set_pos(tile.img_obj, screen_x, screen_y);
    style_tile_obj(tile.img_obj);
    lv_obj_move_background(tile.img_obj);
    lv_image_set_src(tile.img_obj, cache.img_dsc);
    bind_tile_decoded_cache(tile, cache);

    tile.map_source = g_active_map_source;
    tile.has_png_file = true;
    tile.base_missing = false;
    tile.base_request_pending = false;
    tile.base_retry_not_before_ms = 0;
    tile.contour_checked = false;
    tile.contour_loaded = false;
    if (ctx.has_map_data)
    {
        *ctx.has_map_data = true;
    }
    tile.last_used_ms = sys::millis_now();
    tile.obj_evicted_ms = 0;
    tile.record_evicted = false;
    return true;
}

static bool render_contour_from_cache(MapTile& tile, DecodedTileCache& cache)
{
    if (!g_active_contour_enabled || tile.img_obj == NULL || cache.img_dsc == NULL)
    {
        return false;
    }

    ui::map_tiles::MapTileRef expected_ref{};
    if (!contour_tile_ref(tile.z, static_cast<int>(tile.x), static_cast<int>(tile.y), expected_ref) ||
        !cache_matches_ref(cache, expected_ref))
    {
        log_map_tile_decode_failure("contour_cache_ref_mismatch",
                                    expected_ref,
                                    ui::map_tiles::MapTileFormat::Unknown,
                                    0,
                                    static_cast<long>(cache.layer));
        return false;
    }

    if (tile.contour_obj == NULL)
    {
        tile.contour_obj = lv_image_create(tile.img_obj);
        lv_obj_set_size(tile.contour_obj, TILE_SIZE, TILE_SIZE);
        lv_obj_set_pos(tile.contour_obj, 0, 0);
        style_tile_obj(tile.contour_obj);
    }
    lv_image_set_src(tile.contour_obj, cache.img_dsc);
    lv_obj_clear_flag(tile.contour_obj, LV_OBJ_FLAG_HIDDEN);
    bind_contour_decoded_cache(tile, cache);
    tile.contour_checked = true;
    tile.contour_loaded = true;
    tile.contour_request_pending = false;
    tile.contour_retry_not_before_ms = 0;
    return true;
}

static bool request_base_tile_async(MapTile& tile)
{
    if (tile.base_request_pending)
    {
        return true;
    }
    const uint32_t now_ms = sys::millis_now();
    if (tile.base_retry_not_before_ms != 0 &&
        static_cast<int32_t>(tile.base_retry_not_before_ms - now_ms) > 0)
    {
        return false;
    }

    const ui::map_tiles::MapTileRef ref = base_tile_ref_for_tile(tile);
    if (map_tile_availability_memory().knownMissing(ref))
    {
        tile.base_missing = true;
        tile.base_request_pending = false;
        tile.base_request_generation = 0;
        tile.base_retry_not_before_ms = 0;
        return false;
    }

    if (const auto submitted = map_tile_async_host().request(ref,
                                                             g_map_tile_runtime_generation,
                                                             ui::map_tiles::MapTileInteractionMode::InteractiveDrag))
    {
        tile.base_request_pending = true;
        tile.base_request_generation = g_map_tile_runtime_generation;
        tile.base_request_id = submitted.handle.command_id;
        return true;
    }
    tile.base_retry_not_before_ms = now_ms + kMapTileLayerBusyBackoffMs;
    return false;
}

static bool request_contour_tile_async(MapTile& tile)
{
    if (tile.contour_request_pending)
    {
        return true;
    }
    const uint32_t now_ms = sys::millis_now();
    if (tile.contour_retry_not_before_ms != 0 &&
        static_cast<int32_t>(tile.contour_retry_not_before_ms - now_ms) > 0)
    {
        return false;
    }

    ui::map_tiles::MapTileRef ref{};
    if (!contour_tile_ref(tile.z, static_cast<int>(tile.x), static_cast<int>(tile.y), ref))
    {
        tile.contour_checked = true;
        tile.contour_loaded = false;
        return false;
    }

    if (map_tile_availability_memory().knownMissing(ref))
    {
        tile.contour_checked = true;
        tile.contour_loaded = false;
        tile.contour_request_pending = false;
        tile.contour_request_generation = 0;
        tile.contour_retry_not_before_ms = 0;
        return false;
    }

    if (DecodedTileCache* cached = find_cached_tile_ref(ref))
    {
        if (render_contour_from_cache(tile, *cached))
        {
            return true;
        }
    }

    if (const auto submitted = map_tile_async_host().request(ref,
                                                             g_map_tile_runtime_generation,
                                                             ui::map_tiles::MapTileInteractionMode::InteractiveDrag))
    {
        tile.contour_request_pending = true;
        tile.contour_request_generation = g_map_tile_runtime_generation;
        tile.contour_request_id = submitted.handle.command_id;
        return true;
    }
    tile.contour_retry_not_before_ms = now_ms + kMapTileLayerBusyBackoffMs;
    return false;
}

#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
static bool apply_poi_tile_event(TileContext& ctx, ui::map_tiles::MapTileAsyncEvent& event)
{
    MapTile* tile = find_tile(ctx, static_cast<int>(event.tile.x), static_cast<int>(event.tile.y), event.tile.z);
    if (tile && (!tile->poi_pending || tile->poi_request_generation != event.generation ||
                 tile->poi_request_id != event.command_id))
    {
        release_tile_payload(event);
        return false;
    }
    if (!tile || !tile->visible || !ctx.anchor || event.tile.z != ctx.anchor->z)
    {
        if (tile)
        {
            tile->poi_pending = false;
            tile->poi_retry_not_before_ms = 0;
        }
        release_tile_payload(event);
        return false;
    }
    tile->poi_pending = false;
    tile->poi_retry_not_before_ms = 0;
    if (event.kind == ui::map_tiles::MapTileAsyncEventKind::RetryLater)
    {
        tile->poi_retry_not_before_ms = sys::millis_now() + kMapTileLayerBusyBackoffMs;
        release_tile_payload(event);
        return false;
    }
    tile->poi_checked = true;
    ++ctx.poi_revision;
    if (event.kind != ui::map_tiles::MapTileAsyncEventKind::Ready ||
        event.payload.format != ui::map_tiles::MapTileFormat::PoiRecords ||
        !ui::map_poi::validPayload(event.payload.data, event.payload.size))
    {
        if (event.error == -12)
        {
            tile->poi_checked = false;
            tile->poi_retry_not_before_ms = sys::millis_now() + kMapTileLayerTransientBackoffMs;
        }
        log_map_tile_event_failure("poi_payload", event, event.error);
        release_tile_payload(event);
        return false;
    }
    const auto* header = reinterpret_cast<const ui::map_poi::TileHeader*>(event.payload.data);
    // Failed reads must not discard a previously valid tile payload. Replace
    // only after a new typed payload has passed validation (including empty).
    tile->poi.reset();
    const uint16_t count = header->count;
    ctx.poi_policy_known = true;
    ctx.poi_policy = header->policy;
    ctx.poi_available = header->manifest_valid;
    if (count && ctx.poi_available && ctx.poi_policy.enabled(event.tile.z))
    {
        // Transfer the event's exact-size PSRAM allocation to the tile record.
        // No second allocation, fixed 200-record cache, or internal-RAM fallback.
        tile->poi.reset(const_cast<uint8_t*>(event.payload.data));
        event.payload.data = nullptr;
        event.payload.size = 0;
    }
    std::printf("[MapViewport][POI] z=%u x=%lu y=%lu available=%d enabled=%d records=%u\n",
                static_cast<unsigned>(event.tile.z), static_cast<unsigned long>(event.tile.x),
                static_cast<unsigned long>(event.tile.y), ctx.poi_available, ctx.poi_policy.enabled(event.tile.z),
                tile->poi ? count : 0);
    release_tile_payload(event);
    return true;
}

static void request_visible_poi_tile(TileContext& ctx)
{
    if (!ctx.tiles || !ctx.anchor || !ctx.anchor->valid) return;
    if (ctx.poi_policy_known && (!ctx.poi_available || !ctx.poi_policy.enabled(ctx.anchor->z))) return;
    MapTile* best = nullptr;
    const uint32_t now = sys::millis_now();
    for (auto& tile : *ctx.tiles)
    {
        if (!tile.visible || tile.z != ctx.anchor->z || tile.map_source != g_active_map_source ||
            tile.poi_checked || tile.poi_pending ||
            (tile.poi_retry_not_before_ms && static_cast<int32_t>(tile.poi_retry_not_before_ms - now) > 0)) continue;
        if (!best || tile.priority < best->priority) best = &tile;
    }
    if (!best) return;
    ui::map_tiles::MapTileRef ref{ui::map_tiles::MapTileLayer::Poi, static_cast<uint8_t>(best->z),
                                  static_cast<uint32_t>(best->x), static_cast<uint32_t>(best->y)};
    const auto submitted = map_tile_async_host().request(ref, g_map_tile_runtime_generation, ui::map_tiles::MapTileInteractionMode::Idle);
    best->poi_pending = static_cast<bool>(submitted);
    best->poi_request_generation = submitted.handle.generation;
    best->poi_request_id = submitted.handle.command_id;
    best->poi_retry_not_before_ms = submitted ? 0 : now + kMapTileLayerBusyBackoffMs;
}
#endif

static bool apply_map_tile_event(TileContext& ctx, ui::map_tiles::MapTileAsyncEvent& event)
{
    if (event.generation != g_map_tile_runtime_generation)
    {
        log_map_tile_event_failure("stale_generation", event, 0);
        release_tile_payload(event);
        return false;
    }

#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
    if (event.tile.layer == ui::map_tiles::MapTileLayer::Poi) return apply_poi_tile_event(ctx, event);
#endif
    const bool is_contour = ui::map_tiles::mapTileLayerIsContour(event.tile.layer);
    if (!is_contour && map_source_for_layer(event.tile.layer) != g_active_map_source)
    {
        log_map_tile_event_failure("source_mismatch", event, static_cast<long>(g_active_map_source));
        release_tile_payload(event);
        return false;
    }
    if (is_contour && !g_active_contour_enabled)
    {
        release_tile_payload(event);
        return false;
    }

    MapTile* tile = find_tile(ctx,
                              static_cast<int>(event.tile.x),
                              static_cast<int>(event.tile.y),
                              static_cast<int>(event.tile.z));
    if (tile == nullptr)
    {
        log_map_tile_event_failure("tile_not_visible", event, 0);
        release_tile_payload(event);
        return false;
    }

    uint32_t& retry_not_before =
        is_contour ? tile->contour_retry_not_before_ms : tile->base_retry_not_before_ms;
    bool& pending = is_contour ? tile->contour_request_pending : tile->base_request_pending;
    const ui::map_tiles::TileRequestHandle request{
        is_contour ? tile->contour_request_generation : tile->base_request_generation,
        is_contour ? tile->contour_request_id : tile->base_request_id};
    if (!pending || !request.matches(event.generation, event.command_id))
    {
        release_tile_payload(event);
        return false;
    }
    pending = false;
    MAP_DIAG("[MAPD][pending-clear] gen=%lu id=%lu visible=%d kind=%u\n",
             static_cast<unsigned long>(event.generation), static_cast<unsigned long>(event.command_id), tile->visible, static_cast<unsigned>(event.kind));

    const uint32_t now_ms = sys::millis_now();
    if (event.kind == ui::map_tiles::MapTileAsyncEventKind::RetryLater)
    {
        retry_not_before = now_ms + kMapTileLayerBusyBackoffMs;
        log_map_tile_event_failure("resource_busy", event, event.error);
        release_tile_payload(event);
        return false;
    }

    if (event.kind != ui::map_tiles::MapTileAsyncEventKind::Ready)
    {
        log_map_tile_event_failure("worker", event, event.error);
        const bool confirmed_missing = map_tile_availability_memory().knownMissing(event.tile);
        if (is_contour)
        {
            if (confirmed_missing)
            {
                tile->contour_checked = true;
                tile->contour_loaded = false;
                retry_not_before = 0;
            }
            else
            {
                retry_not_before = now_ms + kMapTileLayerTransientBackoffMs;
            }
        }
        else
        {
            if (confirmed_missing)
            {
                mark_missing_base_tile(ctx, *tile);
            }
            else
            {
                retry_not_before = now_ms + kMapTileLayerTransientBackoffMs;
            }
        }
        release_tile_payload(event);
        update_visible_map_data_flag(ctx);
        rebuild_render_queue(ctx);
        return true;
    }

    using Metrics = ui::map_tiles::MapTilePipelineMetrics;
    auto& metrics = map_tile_async_host().metrics();
    const auto decode_start = sys::millis_now();
    DecodedTileCache* cache = decode_payload_to_cache(ctx, event.tile, event.payload);
    metrics.stages[Metrics::Decode].add(sys::millis_now() - decode_start);
    if (cache == nullptr)
    {
        retry_not_before = now_ms + kMapTileLayerCacheBackoffMs;
        log_map_tile_event_failure("decode", event, event.error);
        release_tile_payload(event);
        return false;
    }

    const auto apply_start = sys::millis_now();
    const bool rendered = is_contour ? render_contour_from_cache(*tile, *cache)
                                     : render_base_tile_from_cache(ctx, *tile, *cache);
    if (!rendered)
    {
        retry_not_before = now_ms + kMapTileLayerCacheBackoffMs;
        log_map_tile_event_failure("render", event, 0);
    }

    release_tile_payload(event);
    update_visible_map_data_flag(ctx);
    rebuild_render_queue(ctx);
    metrics.stages[Metrics::Apply].add(sys::millis_now() - apply_start);
    if (rendered) ++metrics.rendered_count;
    MAP_DIAG("[MAPD][render] t=%lu id=%lu ok=%d obj=%p hidden=%d refs=%u decode_ms=%lu\n",
             static_cast<unsigned long>(sys::millis_now()), static_cast<unsigned long>(event.command_id), rendered,
             tile->img_obj, tile->img_obj ? lv_obj_has_flag(tile->img_obj, LV_OBJ_FLAG_HIDDEN) : -1,
             static_cast<unsigned>(cache->lvgl_ref_count), static_cast<unsigned long>(apply_start - decode_start));
    return rendered;
}

static bool current_tile_request(const MapTile& tile, const ui::map_tiles::MapTileAsyncEvent& event)
{
#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
    if (event.tile.layer == ui::map_tiles::MapTileLayer::Poi)
        return tile.poi_pending && tile.poi_request_generation == event.generation && tile.poi_request_id == event.command_id;
#endif
    const bool contour = ui::map_tiles::mapTileLayerIsContour(event.tile.layer);
    return (contour ? tile.contour_request_pending : tile.base_request_pending) &&
           (contour ? tile.contour_request_generation : tile.base_request_generation) == event.generation &&
           (contour ? tile.contour_request_id : tile.base_request_id) == event.command_id;
}

static void map_tile_diagnostic_snapshot(const TileContext& ctx, const char* stage)
{
#if TRAIL_MATE_MAP_DIAGNOSTICS
    if (!ctx.tiles) return;
    unsigned visible = 0, loaded = 0, pending = 0, objects = 0;
    for (const auto& tile : *ctx.tiles)
    {
        objects += tile.img_obj != nullptr;
        pending += tile.base_request_pending;
        if (!tile.visible) continue;
        ++visible;
        loaded += tile.has_png_file;
        int sx = 0, sy = 0;
        const bool projected = tile_screen_pos_xyz(ctx, tile.x, tile.y, tile.z, sx, sy);
        MAP_DIAG("[MAPD][tile] stage=%s z=%d x=%d y=%d source=%u loaded=%d missing=%d pending=%d gen=%lu id=%lu retry=%lu obj=%p hidden=%d pos=%d,%d projected=%d at=%d,%d refs=%u\n",
                 stage, tile.z, static_cast<int>(tile.x), static_cast<int>(tile.y), static_cast<unsigned>(tile.map_source),
                 tile.has_png_file, tile.base_missing, tile.base_request_pending, static_cast<unsigned long>(tile.base_request_generation),
                 static_cast<unsigned long>(tile.base_request_id), static_cast<unsigned long>(tile.base_retry_not_before_ms), tile.img_obj,
                 tile.img_obj ? lv_obj_has_flag(tile.img_obj, LV_OBJ_FLAG_HIDDEN) : -1,
                 tile.img_obj ? lv_obj_get_x(tile.img_obj) : 0, tile.img_obj ? lv_obj_get_y(tile.img_obj) : 0, projected, sx, sy,
                 tile.cached_img ? static_cast<unsigned>(tile.cached_img->lvgl_ref_count) : 0);
    }
    MAP_DIAG("[MAPD][viewport] stage=%s t=%lu gen=%lu records=%u visible=%u loaded=%u pending=%u objects=%u anchor=%d z=%d size=%d,%d\n",
             stage, static_cast<unsigned long>(sys::millis_now()), static_cast<unsigned long>(g_map_tile_runtime_generation),
             static_cast<unsigned>(ctx.tiles->size()), visible, loaded, pending, objects, ctx.anchor && ctx.anchor->valid,
             ctx.anchor ? ctx.anchor->z : -1, ctx.map_container ? lv_obj_get_width(ctx.map_container) : 0, ctx.map_container ? lv_obj_get_height(ctx.map_container) : 0);
#else
    (void)ctx;
    (void)stage;
#endif
}

void tile_loader_maintenance(TileContext& ctx)
{
#if TRAIL_MATE_MAP_DIAGNOSTICS
    static uint32_t last_snapshot_ms = 0;
    if (sys::millis_now() - last_snapshot_ms >= 2000U)
    {
        last_snapshot_ms = sys::millis_now();
        map_tile_diagnostic_snapshot(ctx, "heartbeat");
        map_tile_async_host().reportDiagnostics();
    }
#endif
    if (!ctx.runtime_acquired || !ctx.tiles) return;
    const auto start_ms = sys::millis_now();
    ui::map_tiles::MapTileAsyncEvent event;
    // At most the fixed queue capacity, additionally bounded by wall time.
    for (unsigned i = 0; i < 16; ++i)
    {
        const bool found = map_tile_async_host().popEventIf(event, [&](const auto& candidate)
                                                            {
            if (candidate.generation != g_map_tile_runtime_generation) return true;
            const auto* tile = find_tile(ctx, candidate.tile.x, candidate.tile.y, candidate.tile.z);
            return !tile || !tile->visible || !current_tile_request(*tile, candidate); });
        if (!found) break;
        auto* tile = find_tile(ctx, event.tile.x, event.tile.y, event.tile.z);
        MAP_DIAG("[MAPD][discard] t=%lu gen=%lu active=%lu id=%lu exists=%d visible=%d matches=%d\n",
                 static_cast<unsigned long>(sys::millis_now()), static_cast<unsigned long>(event.generation),
                 static_cast<unsigned long>(g_map_tile_runtime_generation), static_cast<unsigned long>(event.command_id),
                 tile != nullptr, tile ? tile->visible : 0, tile ? current_tile_request(*tile, event) : 0);
        if (tile && current_tile_request(*tile, event))
        {
#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
            if (event.tile.layer == ui::map_tiles::MapTileLayer::Poi) tile->poi_pending = false;
            else
#endif
                if (ui::map_tiles::mapTileLayerIsContour(event.tile.layer))
                tile->contour_request_pending = false;
            else tile->base_request_pending = false;
        }
        release_tile_payload(event);
        if (static_cast<uint32_t>(sys::millis_now() - start_ms) >= kMapTileUiDrainBudgetMs) break;
    }
}

static void drain_map_tile_events(TileContext& ctx, uint32_t start_ms, uint32_t budget_ms)
{
    struct ReportOnExit
    {
        ~ReportOnExit() { map_tile_async_host().reportMetrics(); }
    } report;
    tile_loader_maintenance(ctx);
    // Failure/cancellation bookkeeping must not consume a PNG decode slot or
    // incur image cooldown. Stop between events when the UI budget is spent.
    ui::map_tiles::MapTileAsyncEvent control;
    while (static_cast<uint32_t>(sys::millis_now() - start_ms) < budget_ms &&
           map_tile_async_host().popEventIf(control, [](const auto& event)
                                            { return event.kind != ui::map_tiles::MapTileAsyncEventKind::Ready; }))
    {
        if (map_tile_async_host().acceptEvent(control, ctx.render_queue)) (void)apply_map_tile_event(ctx, control);
        else release_tile_payload(control);
    }
    if (static_cast<uint32_t>(sys::millis_now() - start_ms) >= budget_ms) return;
    const uint32_t now_ms = sys::millis_now();
    if (g_map_tile_next_event_drain_ms != 0 &&
        static_cast<int32_t>(g_map_tile_next_event_drain_ms - now_ms) > 0)
    {
        return;
    }

    ui::map_tiles::MapTileAsyncEvent event{};
    int drained = 0;
    while (drained < kMapTileEventsPerUiDrain && map_tile_async_host().popEvent(event))
    {
        const bool accepted = map_tile_async_host().acceptEvent(event, ctx.render_queue);
        if (accepted)
        {
            (void)apply_map_tile_event(ctx, event);
        }
        else
        {
            release_tile_payload(event);
        }
        ++drained;
        event = {};
        g_map_tile_next_event_drain_ms = sys::millis_now() + kMapTileUiEventCooldownMs;
        if (static_cast<uint32_t>(sys::millis_now() - start_ms) >= budget_ms)
        {
            break;
        }
    }
}

/**
 * Calculate and cache map anchor (GPS pixel coordinates)
 */
void update_map_anchor(TileContext& ctx, double lat, double lng, int zoom, int pan_x, int pan_y, bool has_fix)
{
    if (!ctx.map_container || !ctx.anchor)
    {
        if (ctx.anchor) ctx.anchor->valid = false;
        return;
    }

    lv_coord_t screen_width = lv_obj_get_width(ctx.map_container);
    lv_coord_t screen_height = lv_obj_get_height(ctx.map_container);

    // Even without GPS fix, if lat/lng are provided (e.g., default London location),
    // calculate anchor to support rendering at that location
    // Use epsilon comparison for floating point
    const double EPSILON = 0.0001;
    if (!has_fix && (fabs(lat) < EPSILON && fabs(lng) < EPSILON))
    {
        // Only skip calculation if coordinates are truly zero (no default location set)
        GPS_LOG("[GPS] update_map_anchor: No GPS fix and coordinates are zero, skipping anchor calculation\n");
        ctx.anchor->valid = false;
        return;
    }

    GPS_LOG("[GPS] update_map_anchor: Calculating anchor (has_fix=%d, lat=%.6f, lng=%.6f, zoom=%d)\n",
            has_fix, lat, lng, zoom);

    // Use latLngToTile to ensure consistency with tile coordinate calculation
    // This ensures the same algorithm is used everywhere
    int gps_tile_x = 0;
    int gps_tile_y = 0;
    latLngToTile(lat, lng, zoom, gps_tile_x, gps_tile_y);
    ctx.anchor->gps_tile_x = gps_tile_x;
    ctx.anchor->gps_tile_y = gps_tile_y;

    // Calculate GPS global pixel coordinates for positioning
    ctx.anchor->n = pow(2.0, zoom);

    // Clamp latitude to WebMercator valid range before pixel calculation
    double lat_clamped = lat;
    const double MAX_LAT = 85.05112878;
    if (lat_clamped > MAX_LAT) lat_clamped = MAX_LAT;
    if (lat_clamped < -MAX_LAT) lat_clamped = -MAX_LAT;

    // Wrap longitude to [-180, 180)
    double lng_wrapped = lng;
    while (lng_wrapped < -180.0) lng_wrapped += 360.0;
    while (lng_wrapped >= 180.0) lng_wrapped -= 360.0;

    // Calculate GPS global pixel coordinates (expensive operations - done once)
    double lat_rad = lat_clamped * M_PI / 180.0;
    double gps_pixel_x = ((lng_wrapped + 180.0) / 360.0 * ctx.anchor->n) * TILE_SIZE;
    double gps_pixel_y = ((1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * ctx.anchor->n) * TILE_SIZE;

    // CRITICAL: Cache global pixel coordinates (0..world_px-1)
    // This is used by get_screen_center_lat_lng() to correctly calculate screen center
    ctx.anchor->gps_global_pixel_x = (int32_t)floor(gps_pixel_x);
    ctx.anchor->gps_global_pixel_y = (int32_t)floor(gps_pixel_y);

    // Calculate GPS tile pixel coordinates
    ctx.anchor->gps_tile_pixel_x = ctx.anchor->gps_tile_x << 8;
    ctx.anchor->gps_tile_pixel_y = ctx.anchor->gps_tile_y << 8;

    // Calculate GPS offset within tile
    // Use floor() instead of (int) truncation to avoid 1px jitter at boundaries
    ctx.anchor->gps_offset_x = ctx.anchor->gps_global_pixel_x - ctx.anchor->gps_tile_pixel_x;
    ctx.anchor->gps_offset_y = ctx.anchor->gps_global_pixel_y - ctx.anchor->gps_tile_pixel_y;

    // Calculate GPS tile screen position
    ctx.anchor->gps_tile_screen_x = screen_width / 2 - ctx.anchor->gps_offset_x + pan_x;
    ctx.anchor->gps_tile_screen_y = screen_height / 2 - ctx.anchor->gps_offset_y + pan_y;

    ctx.anchor->z = zoom;
    ctx.anchor->valid = true;
}

/**
 * Stage 1: Mark all tiles invisible
 * Aggressively delete tile objects from different zoom levels to free memory immediately
 */
static void mark_all_invisible(TileContext& ctx, int target_zoom)
{
    if (!ctx.tiles) return;

    // First pass: immediately delete objects from different zoom levels
    // This frees memory immediately when zoom changes, preventing accumulation
    for (auto& tile : *ctx.tiles)
    {
        tile.visible = false;
        // Delete tile objects that don't match target zoom level immediately
        // This prevents memory buildup when switching zoom levels frequently
        if (tile.img_obj != NULL && tile.z != target_zoom)
        {
            reset_tile_runtime(tile);
            tile.obj_evicted_ms = sys::millis_now(); // Mark as evicted for record cleanup protection
        }
    }

    if (ctx.has_visible_map_data)
    {
        *ctx.has_visible_map_data = false;
    }
}

/**
 * Stage 2: Collect required tiles
 */
static void collect_required_tiles(TileContext& ctx, double lat, double lng, int zoom, int pan_x, int pan_y, bool has_fix)
{
    if (!ctx.map_container || !ctx.tiles || !ctx.anchor)
    {
        GPS_LOG("[GPS] collect_required_tiles: Invalid context\n");
        return;
    }

    lv_coord_t screen_width = lv_obj_get_width(ctx.map_container);
    lv_coord_t screen_height = lv_obj_get_height(ctx.map_container);

    GPS_LOG("[GPS] collect_required_tiles: has_fix=%d, zoom=%d, lat=%.6f, lng=%.6f, screen=%dx%d\n",
            has_fix, zoom, lat, lng, screen_width, screen_height);

    // Check if anchor is valid (either from GPS fix or from default location like London)
    if (!ctx.anchor->valid)
    {
        // If anchor is invalid and no GPS fix, fall back to world map tile 0/0/zoom
        // Use the current zoom level, not hardcoded 0
        if (!has_fix)
        {
            GPS_FLOW_LOG("[GPS][MAP][fallback] anchor_invalid_no_fix world_tile zoom=%d lat=%.6f lng=%.6f\n",
                         zoom,
                         lat,
                         lng);
            GPS_LOG("[GPS] No GPS fix and invalid anchor: rendering world map tile 0/0/%d\n", zoom);
            ensure_tile(ctx, 0, 0, zoom, 0); // Priority 0 = center tile, use current zoom
            return;
        }
        else
        {
            GPS_FLOW_LOG("[GPS][MAP][fallback] anchor_invalid_with_fix zoom=%d lat=%.6f lng=%.6f\n",
                         zoom,
                         lat,
                         lng);
            GPS_LOG("[GPS] ERROR: cached_anchor invalid in collect_required_tiles (has_fix=true)\n");
            return;
        }
    }

    int gps_tile_x = ctx.anchor->gps_tile_x;
    int gps_tile_y = ctx.anchor->gps_tile_y;

    // Ensure GPS center tile exists
    ensure_tile(ctx, gps_tile_x, gps_tile_y, zoom, 0); // Priority 0 = center

    // Dynamic tile collection based on screen viewport
    // Calculate which tiles are needed to cover the entire screen (no preloading)
    // Start from screen corners and work inward to find all tiles that intersect the viewport

    // Calculate tile range needed to cover screen
    // Convert screen coordinates to tile coordinates
    // For each possible tile position, check if it intersects the screen

    // Start from GPS tile and expand outward until we cover the entire screen
    // Use a reasonable maximum range (e.g., 10 tiles in each direction)
    const int MAX_TILE_RANGE = 10;

    // Collect all tiles that intersect the viewport
    for (int dy = -MAX_TILE_RANGE; dy <= MAX_TILE_RANGE; dy++)
    {
        for (int dx = -MAX_TILE_RANGE; dx <= MAX_TILE_RANGE; dx++)
        {
            int tile_x = gps_tile_x + dx;
            int tile_y = gps_tile_y + dy;

            // Normalize tile coordinates
            normalize_tile(zoom, tile_x, tile_y);

            // Calculate screen position for this tile
            int screen_x, screen_y;
            if (!tile_screen_pos_xyz(ctx, tile_x, tile_y, zoom, screen_x, screen_y))
            {
                continue; // Skip if position calculation fails
            }

            // Check if tile intersects viewport (no preloading)
            if (tile_in_rect(screen_x, screen_y, screen_width, screen_height, 0))
            {
                // Calculate priority based on screen center (pixels)
                int center_x = screen_width / 2;
                int center_y = screen_height / 2;
                int tile_center_x = screen_x + (TILE_SIZE / 2);
                int tile_center_y = screen_y + (TILE_SIZE / 2);
                int dx_px = tile_center_x - center_x;
                int dy_px = tile_center_y - center_y;
                if (dx_px < 0) dx_px = -dx_px;
                if (dy_px < 0) dy_px = -dy_px;
                int priority = dx_px + dy_px;
                ensure_tile(ctx, tile_x, tile_y, zoom, priority);
            }
        }
    }
}

/**
 * Stage 3: Layout loaded tile objects
 */
static void layout_loaded_tile_objects(TileContext& ctx)
{
    if (!ctx.map_container || !ctx.tiles || !ctx.anchor) return;

    lv_coord_t screen_width = lv_obj_get_width(ctx.map_container);
    lv_coord_t screen_height = lv_obj_get_height(ctx.map_container);
    int current_zoom = ctx.anchor->z; // Current zoom level

    for (auto& tile : *ctx.tiles)
    {
        // CRITICAL: Skip tiles from different zoom levels or stale source records
        if (tile.z != current_zoom || tile.map_source != g_active_map_source)
        {
            if (tile.img_obj != NULL)
            {
                lv_obj_add_flag(tile.img_obj, LV_OBJ_FLAG_HIDDEN);
            }
            if (tile.contour_obj != NULL)
            {
                lv_obj_add_flag(tile.contour_obj, LV_OBJ_FLAG_HIDDEN);
            }
            tile.visible = false;
            continue;
        }

        // Calculate screen position
        int screen_x, screen_y;
        if (!tile_screen_pos_xyz(ctx, tile.x, tile.y, tile.z, screen_x, screen_y))
        {
            // If position calculation fails, hide the tile
            if (tile.img_obj != NULL)
            {
                lv_obj_add_flag(tile.img_obj, LV_OBJ_FLAG_HIDDEN);
            }
            if (tile.contour_obj != NULL)
            {
                lv_obj_add_flag(tile.contour_obj, LV_OBJ_FLAG_HIDDEN);
            }
            tile.visible = false;
            continue;
        }

        // Check strict visibility (margin = 0 for actual display)
        bool is_visible = tile_in_rect(screen_x, screen_y, screen_width, screen_height, 0);
        tile.visible = is_visible;

        // Decoded cache entries stay protected for as long as an LVGL image
        // object can redraw them. Visibility alone is not a lifetime boundary.
        touch_tile_decoded_cache(tile);

        if (is_visible)
        {
            // Restore main-branch behavior for non-touch PIO layouts so missing
            // tiles stay behind the control overlays. Large touch layouts keep
            // unloaded tiles object-free to reduce full-screen churn.
            if (tile.img_obj == NULL && use_non_touch_placeholder_cards())
            {
                create_placeholder_tile_card(ctx.map_container, tile, screen_x, screen_y);
            }

            if (tile.img_obj != NULL)
            {
                lv_obj_set_pos(tile.img_obj, screen_x, screen_y);
                lv_obj_clear_flag(tile.img_obj, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_background(tile.img_obj);
            }
            if (tile.contour_obj != NULL)
            {
                if (g_active_contour_enabled)
                {
                    lv_obj_clear_flag(tile.contour_obj, LV_OBJ_FLAG_HIDDEN);
                }
                else
                {
                    lv_obj_add_flag(tile.contour_obj, LV_OBJ_FLAG_HIDDEN);
                }
            }
            tile.last_used_ms = sys::millis_now();
        }
        else
        {
            // Hide invisible tiles
            if (tile.img_obj != NULL)
            {
                lv_obj_add_flag(tile.img_obj, LV_OBJ_FLAG_HIDDEN);
            }
            if (tile.contour_obj != NULL)
            {
                lv_obj_add_flag(tile.contour_obj, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // Check if any visible tile has PNG file
    bool visible_png_found = false;
    int visible_count = 0;
    int visible_with_png = 0;
    for (auto& tile : *ctx.tiles)
    {
        if (tile.visible)
        {
            visible_count++;
            if (tile.has_png_file)
            {
                visible_png_found = true;
                visible_with_png++;
            }
        }
    }
    if (ctx.has_visible_map_data)
    {
        bool old_value = *ctx.has_visible_map_data;
        *ctx.has_visible_map_data = visible_png_found;
        if (old_value != visible_png_found)
        {
            GPS_LOG("[GPS] has_visible_map_data changed: %d -> %d (visible=%d, with_png=%d)\n",
                    old_value, visible_png_found, visible_count, visible_with_png);
        }
    }
}

/**
 * Stage 4: Evict cache (two-tier LRU)
 */
static void evict_cache(TileContext& ctx)
{
    if (!ctx.tiles) return;
#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
    for (auto& tile : *ctx.tiles)
    {
        if (!tile.visible && tile.poi)
        {
            tile.poi.reset();
            tile.poi_checked = false;
            ++ctx.poi_revision;
        }
    }
#endif

    const size_t obj_limit = tile_object_cache_limit(ctx);
    const size_t record_limit = tile_record_limit(ctx);

    // Tier 1: Limit lv_obj count
    size_t obj_count = 0;
    for (const auto& tile : *ctx.tiles)
    {
        if (tile.img_obj != NULL) obj_count++;
    }

    if (obj_count > obj_limit)
    {
        // Collect invisible tiles with objects, sorted by last_used_ms (oldest first)
        // Priority: different zoom level tiles are already deleted in mark_all_invisible,
        // so we only need to handle same-zoom invisible tiles here
        std::vector<std::pair<uint32_t, size_t>> obj_candidates;
        int current_zoom = ctx.anchor && ctx.anchor->valid ? ctx.anchor->z : -1;
        for (size_t i = 0; i < ctx.tiles->size(); i++)
        {
            // Only consider invisible tiles from current zoom level
            // (different zoom tiles are already deleted in mark_all_invisible)
            if (!(*ctx.tiles)[i].visible &&
                (*ctx.tiles)[i].img_obj != NULL &&
                (*ctx.tiles)[i].z == current_zoom)
            {
                obj_candidates.push_back({(*ctx.tiles)[i].last_used_ms, i});
            }
        }

        // Sort by last_used_ms (oldest first)
        std::sort(obj_candidates.begin(), obj_candidates.end(),
                  [](const std::pair<uint32_t, size_t>& a, const std::pair<uint32_t, size_t>& b)
                  {
                      return a.first < b.first;
                  });

        // Delete oldest invisible tiles until under limit
        size_t to_delete = obj_count - obj_limit;
        for (size_t i = 0; i < to_delete && i < obj_candidates.size(); i++)
        {
            size_t idx = obj_candidates[i].second;
            if ((*ctx.tiles)[idx].img_obj != NULL)
            {
                MAP_DIAG("[MAPD][evict-object] z=%d x=%d y=%d visible=%d pending=%d\n", (*ctx.tiles)[idx].z,
                         static_cast<int>((*ctx.tiles)[idx].x), static_cast<int>((*ctx.tiles)[idx].y), (*ctx.tiles)[idx].visible, (*ctx.tiles)[idx].base_request_pending);
                reset_tile_runtime((*ctx.tiles)[idx]);
                (*ctx.tiles)[idx].obj_evicted_ms = sys::millis_now();
            }
        }
    }

    // Tier 2: Limit tile record count
    if (ctx.tiles->size() > record_limit)
    {
        // Collect candidates for record eviction
        std::vector<std::pair<uint32_t, size_t>> record_candidates_never;
        std::vector<std::pair<uint32_t, size_t>> record_candidates_ever;

        uint32_t now = sys::millis_now();
        for (size_t i = 0; i < ctx.tiles->size(); i++)
        {
            if ((*ctx.tiles)[i].img_obj == NULL && !(*ctx.tiles)[i].record_evicted)
            {
                // Protect recently obj_evicted tiles (within 3 seconds)
                if ((*ctx.tiles)[i].obj_evicted_ms > 0 && (now - (*ctx.tiles)[i].obj_evicted_ms) < 3000)
                {
                    continue;
                }

                if (!(*ctx.tiles)[i].ever_visible)
                {
                    record_candidates_never.push_back({(*ctx.tiles)[i].last_used_ms, i});
                }
                else
                {
                    record_candidates_ever.push_back({(*ctx.tiles)[i].last_used_ms, i});
                }
            }
        }

        // Sort by last_used_ms (oldest first)
        std::sort(record_candidates_never.begin(), record_candidates_never.end(),
                  [](const std::pair<uint32_t, size_t>& a, const std::pair<uint32_t, size_t>& b)
                  {
                      return a.first < b.first;
                  });
        std::sort(record_candidates_ever.begin(), record_candidates_ever.end(),
                  [](const std::pair<uint32_t, size_t>& a, const std::pair<uint32_t, size_t>& b)
                  {
                      return a.first < b.first;
                  });

        // Mark records for deletion: first never-visible, then ever-visible
        size_t to_delete = ctx.tiles->size() - record_limit;
        size_t deleted = 0;

        for (const auto& candidate : record_candidates_never)
        {
            if (deleted >= to_delete) break;
            (*ctx.tiles)[candidate.second].record_evicted = true;
            MAP_DIAG("[MAPD][evict-record] z=%d x=%d y=%d visible=%d pending=%d\n", (*ctx.tiles)[candidate.second].z,
                     static_cast<int>((*ctx.tiles)[candidate.second].x), static_cast<int>((*ctx.tiles)[candidate.second].y),
                     (*ctx.tiles)[candidate.second].visible, (*ctx.tiles)[candidate.second].base_request_pending);
            deleted++;
        }

        for (const auto& candidate : record_candidates_ever)
        {
            if (deleted >= to_delete) break;
            (*ctx.tiles)[candidate.second].record_evicted = true;
            MAP_DIAG("[MAPD][evict-record] z=%d x=%d y=%d visible=%d pending=%d\n", (*ctx.tiles)[candidate.second].z,
                     static_cast<int>((*ctx.tiles)[candidate.second].x), static_cast<int>((*ctx.tiles)[candidate.second].y),
                     (*ctx.tiles)[candidate.second].visible, (*ctx.tiles)[candidate.second].base_request_pending);
            deleted++;
        }

        // Remove evicted records
        ctx.tiles->erase(std::remove_if(ctx.tiles->begin(), ctx.tiles->end(),
                                        [](const MapTile& t)
                                        { return t.record_evicted; }),
                         ctx.tiles->end());
    }
}

/**
 * Calculate which tiles are needed to fill the screen
 * 4-stage pipeline
 */
void calculate_required_tiles(TileContext& ctx, double lat, double lng, int zoom, int pan_x, int pan_y, bool has_fix)
{
    if (!ctx.map_container || !ctx.tiles || !ctx.anchor)
    {
        GPS_LOG("[GPS] calculate_required_tiles: Invalid context\n");
        return;
    }

    sync_render_settings(ctx);

    GPS_LOG("[GPS] calculate_required_tiles: has_fix=%d, zoom=%d, lat=%.6f, lng=%.6f\n",
            has_fix, zoom, lat, lng);

    // Mark all tiles invisible and hide objects from different zoom levels
    mark_all_invisible(ctx, zoom);

    update_map_anchor(ctx, lat, lng, zoom, pan_x, pan_y, has_fix);

    collect_required_tiles(ctx, lat, lng, zoom, pan_x, pan_y, has_fix);

    layout_loaded_tile_objects(ctx);

    map_tile_diagnostic_snapshot(ctx, "layout");
    evict_cache(ctx);
    map_tile_diagnostic_snapshot(ctx, "evicted");
    rebuild_render_queue(ctx);

    // Count tiles to load for logging
    // Count visible tiles that don't have PNG loaded yet (may have placeholder)
    int tiles_to_load = 0;
    if (ctx.tiles)
    {
        for (auto& tile : *ctx.tiles)
        {
            if (tile.visible && !tile.has_png_file)
            {
                tiles_to_load++;
            }
        }
    }

    GPS_LOG("[GPS] Finished calculating tiles: to_load=%d, total=%d\n",
            tiles_to_load, ctx.tiles ? ctx.tiles->size() : 0);
}

/**
 * Load a few tiles (called by timer, not in calculate_required_tiles)
 */
void tile_loader_step(TileContext& ctx)
{
    if (!ctx.map_container || !ctx.tiles)
    {
        return;
    }

    const uint32_t start_ms = sys::millis_now();
    const uint32_t budget_ms = kMapTileUiDrainBudgetMs;
    sync_render_settings(ctx);
    drain_map_tile_events(ctx, start_ms, budget_ms);
    update_visible_map_data_flag(ctx);
    if (static_cast<uint32_t>(sys::millis_now() - start_ms) >= budget_ms)
    {
        return;
    }
    const int max_tiles_per_step = kMapTileRequestsPerUiStep;
    MapTile* attempted[max_tiles_per_step] = {NULL};
    int attempted_count = 0;

    while (attempted_count < max_tiles_per_step)
    {
        // Find visible unloaded tiles with minimum priority
        // Look for tiles that are visible but don't have PNG loaded yet
        // (they may have a placeholder label, but not the actual image)
        MapTile* best = nullptr;
        for (auto& tile : *ctx.tiles)
        {
            if (tile.visible &&
                tile.map_source == g_active_map_source &&
                !tile.has_png_file &&
                !tile.base_missing &&
                !tile.base_request_pending &&
                (tile.base_retry_not_before_ms == 0 ||
                 static_cast<int32_t>(tile.base_retry_not_before_ms - start_ms) <= 0))
            {
                bool already_attempted = false;
                for (int i = 0; i < attempted_count; i++)
                {
                    if (attempted[i] == &tile)
                    {
                        already_attempted = true;
                        break;
                    }
                }
                if (already_attempted)
                {
                    continue;
                }

                if (best == nullptr ||
                    tile.priority < best->priority ||
                    (tile.priority == best->priority && tile.last_used_ms < best->last_used_ms))
                {
                    best = &tile;
                }
            }
        }

        if (best == nullptr)
        {
            break;
        }

        attempted[attempted_count++] = best;

        int before_visible_total = 0;
        int before_visible_loaded = 0;
        int before_visible_placeholder = 0;
        int before_visible_unloaded = 0;
        summarize_visible_tiles(ctx,
                                before_visible_total,
                                before_visible_loaded,
                                before_visible_placeholder,
                                before_visible_unloaded);
        GPS_FLOW_LOG("[GPS][MAP][loader] pick z=%d x=%d y=%d prio=%d vis=%d loaded=%d placeholder=%d unloaded=%d\n",
                     best->z,
                     fmt_tile_coord(best->x),
                     fmt_tile_coord(best->y),
                     best->priority,
                     before_visible_total,
                     before_visible_loaded,
                     before_visible_placeholder,
                     before_visible_unloaded);

        // Save old object position for invalidation
        lv_obj_t* old_obj = best->img_obj;
        int old_screen_x = 0, old_screen_y = 0;
        if (old_obj != NULL)
        {
            old_screen_x = lv_obj_get_x(old_obj);
            old_screen_y = lv_obj_get_y(old_obj);
        }

        bool rendered_now = false;
        if (DecodedTileCache* cached = find_cached_tile_ref(base_tile_ref_for_tile(*best)))
        {
            rendered_now = render_base_tile_from_cache(ctx, *best, *cached);
        }
        else
        {
            (void)request_base_tile_async(*best);
        }

        int after_visible_total = 0;
        int after_visible_loaded = 0;
        int after_visible_placeholder = 0;
        int after_visible_unloaded = 0;
        summarize_visible_tiles(ctx,
                                after_visible_total,
                                after_visible_loaded,
                                after_visible_placeholder,
                                after_visible_unloaded);
        GPS_FLOW_LOG("[GPS][MAP][loader] done z=%d x=%d y=%d file=%d obj=%d vis=%d loaded=%d placeholder=%d unloaded=%d\n",
                     best->z,
                     fmt_tile_coord(best->x),
                     fmt_tile_coord(best->y),
                     best->has_png_file,
                     best->img_obj != NULL,
                     after_visible_total,
                     after_visible_loaded,
                     after_visible_placeholder,
                     after_visible_unloaded);

        // Invalidate only the tile area, not the entire container
        if (rendered_now && best->img_obj != NULL)
        {
            int new_screen_x = lv_obj_get_x(best->img_obj);
            int new_screen_y = lv_obj_get_y(best->img_obj);

            // Invalidate old position (if placeholder was at different location)
            if (old_obj != NULL && (old_screen_x != new_screen_x || old_screen_y != new_screen_y))
            {
                lv_area_t old_area;
                old_area.x1 = old_screen_x;
                old_area.y1 = old_screen_y;
                old_area.x2 = old_screen_x + TILE_SIZE - 1;
                old_area.y2 = old_screen_y + TILE_SIZE - 1;
                lv_obj_invalidate_area(ctx.map_container, &old_area);
            }

            // Invalidate new position (just the tile, not entire container)
            lv_obj_invalidate(best->img_obj);
        }

        // After loading a tile, update has_visible_map_data flag
        // This ensures the flag is updated immediately when tiles are loaded
        if (ctx.has_visible_map_data)
        {
            bool old_value = *ctx.has_visible_map_data;
            update_visible_map_data_flag(ctx);
            if (old_value != *ctx.has_visible_map_data)
            {
                GPS_LOG("[GPS] tile_loader_step: has_visible_map_data changed: %d -> %d\n",
                        old_value, *ctx.has_visible_map_data);
            }
        }

        if ((int32_t)(sys::millis_now() - start_ms) >= (int32_t)budget_ms)
        {
            break;
        }
    }

    if (g_active_contour_enabled && (int32_t)(sys::millis_now() - start_ms) < (int32_t)budget_ms)
    {
        MapTile* contour_target = nullptr;
        for (auto& tile : *ctx.tiles)
        {
            if (!tile.visible || !tile.has_png_file || tile.map_source != g_active_map_source)
            {
                continue;
            }
            if (tile.contour_checked)
            {
                continue;
            }
            if (tile.contour_request_pending)
            {
                continue;
            }
            const uint32_t now_ms = sys::millis_now();
            if (tile.contour_retry_not_before_ms != 0 &&
                static_cast<int32_t>(tile.contour_retry_not_before_ms - now_ms) > 0)
            {
                continue;
            }
            if (contour_target == nullptr ||
                tile.priority < contour_target->priority ||
                (tile.priority == contour_target->priority && tile.last_used_ms < contour_target->last_used_ms))
            {
                contour_target = &tile;
            }
        }
        if (contour_target != nullptr)
        {
            ui::map_tiles::MapTileRef ref{};
            bool rendered_now = false;
            if (contour_tile_ref(contour_target->z,
                                 static_cast<int>(contour_target->x),
                                 static_cast<int>(contour_target->y),
                                 ref))
            {
                if (DecodedTileCache* cached = find_cached_tile_ref(ref))
                {
                    rendered_now = render_contour_from_cache(*contour_target, *cached);
                }
                else
                {
                    (void)request_contour_tile_async(*contour_target);
                }
            }
            else
            {
                contour_target->contour_checked = true;
                contour_target->contour_loaded = false;
            }
            if (rendered_now && contour_target->contour_obj != NULL)
            {
                lv_obj_invalidate(contour_target->contour_obj);
            }
        }
    }
#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
    if (static_cast<int32_t>(sys::millis_now() - start_ms) < static_cast<int32_t>(budget_ms)) request_visible_poi_tile(ctx);
#endif
    update_visible_map_data_flag(ctx);
    rebuild_render_queue(ctx);
}

#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
void map_poi_snapshot(TileContext& ctx, ui::map::MapPoiSnapshot& out)
{
    out.header.valid = ctx.poi_policy_known && ctx.poi_available;
    out.header.version = 1;
    out.header.generated_at_ms = sys::millis_now();
    out.labels = ctx.poi_policy.labels;
    out.enabled = out.header.valid && ctx.anchor && ctx.anchor->valid && ctx.poi_policy.enabled(ctx.anchor->z);
    out.truncated = false;
    out.candidate_count = 0;
    out.item_count = 0;
    out.loading = false;
    out.layout_ready = false;
    if (!out.enabled || !ctx.tiles || !ctx.map_container) return;
    out.width = static_cast<int16_t>(lv_obj_get_width(ctx.map_container));
    out.height = static_cast<int16_t>(lv_obj_get_height(ctx.map_container));
    const int32_t origin_x = ctx.anchor->gps_tile_screen_x + ctx.anchor->gps_offset_x - ctx.anchor->gps_global_pixel_x;
    const int32_t origin_y = ctx.anchor->gps_tile_screen_y + ctx.anchor->gps_offset_y - ctx.anchor->gps_global_pixel_y;
    out.view_key = (uint64_t{static_cast<uint32_t>(origin_x)} << 32) | static_cast<uint32_t>(origin_y);
    out.compatibility_key = (uint64_t{g_map_tile_runtime_generation} << 16) |
                            (uint64_t{static_cast<uint8_t>(ctx.anchor->z)} << 8) | g_active_map_source;
    for (const auto& tile : *ctx.tiles)
    {
        if (!tile.visible || tile.z != ctx.anchor->z) continue;
        out.loading = out.loading || tile.poi_pending || !tile.poi_checked;
        if (!tile.poi) continue;
        const auto* header = reinterpret_cast<const ui::map_poi::TileHeader*>(tile.poi.get());
        out.candidate_count += header->count;
        out.truncated = out.truncated || header->truncated;
    }
}

void visit_map_annotations(TileContext& ctx, ui::map_poi::AnnotationConsumer consume, void* user)
{
    if (!consume || !ctx.tiles || !ctx.anchor || !ctx.anchor->valid) return;
    for (const auto& tile : *ctx.tiles)
    {
        if (!tile.visible || tile.z != ctx.anchor->z || !tile.poi) continue;
        const auto* header = reinterpret_cast<const ui::map_poi::TileHeader*>(tile.poi.get());
        const auto* records = ui::map_poi::payloadRecords(tile.poi.get());
        int tile_x = 0, tile_y = 0;
        if (!tile_screen_pos_xyz(ctx, tile.x, tile.y, tile.z, tile_x, tile_y)) continue;
        for (std::size_t i = 0; i < header->count; ++i)
        {
            const auto& record = records[i];
            int x = 0, y = 0;
            if (!gps_screen_pos(ctx, record.lat, record.lon, x, y)) continue;
            ui::map_poi::AnnotationCandidate candidate;
            candidate.key = record.key;
            candidate.feature_key = record.feature_key;
            candidate.name = ctx.poi_policy.labels ? record.name : "";
            candidate.category = record.category;
            candidate.kind = record.kind;
            candidate.priority = record.priority;
            candidate.x = static_cast<int16_t>(std::clamp(x, -32768, 32767));
            candidate.y = static_cast<int16_t>(std::clamp(y, -32768, 32767));
            candidate.path_points = record.path_points;
            for (unsigned p = 0; p < record.path_points; ++p)
            {
                candidate.path[p * 2] = static_cast<int16_t>(std::clamp(tile_x + record.path[p * 2], -32768, 32767));
                candidate.path[p * 2 + 1] = static_cast<int16_t>(std::clamp(tile_y + record.path[p * 2 + 1], -32768, 32767));
            }
            consume(user, candidate);
        }
    }
}
#endif

/**
 * Initialize tile context
 */
void init_tile_context(TileContext& ctx, lv_obj_t* map_container, MapAnchor* anchor,
                       std::vector<MapTile>* tiles,
                       ui::map_tiles::MapTileRenderQueue* render_queue,
                       bool* has_map_data, bool* has_visible_map_data)
{
    if (!ctx.runtime_acquired)
    {
        map_tile_async_host().acquire();
        ctx.runtime_acquired = true;
    }
    ctx.map_container = map_container;
    ctx.anchor = anchor;
    ctx.tiles = tiles;
    ctx.render_queue = render_queue;
    ctx.has_map_data = has_map_data;
    ctx.has_visible_map_data = has_visible_map_data;
    if (ctx.render_queue)
    {
        ctx.render_queue->clear();
    }
}

/**
 * Cleanup tiles
 */
void cleanup_tiles(TileContext& ctx)
{
    if (!ctx.tiles) return;
#if defined(TRAIL_MATE_MAP_POI_AVAILABLE)
    ctx.poi_policy_known = false;
    ctx.poi_available = false;
    ++ctx.poi_revision;
#endif

    for (auto& tile : *ctx.tiles)
    {
        reset_tile_runtime(tile);
    }
    ctx.tiles->clear();
    if (::ui::runtime::current_memory_profile().retain_map_decode_cache_on_page_exit)
    {
        // Extended-memory boards can keep decoded tiles hot across page exits.
        release_tile_decode_cache_usage();
    }
    else
    {
        clear_tile_decode_cache();
    }
    g_active_map_source = 0xFF;
    g_active_contour_enabled = false;
    {
        const uint32_t previous_generation = g_map_tile_runtime_generation;
        ++g_map_tile_runtime_generation;
        if (g_map_tile_runtime_generation == 0)
        {
            g_map_tile_runtime_generation = kMapTileGenerationInitial;
        }
        map_tile_async_host().cancelGeneration(previous_generation);
    }
    g_missing_tile_notice_pending = false;
    g_missing_tile_notice_emitted = false;
    g_missing_tile_notice_source = 0;
    if (ctx.has_map_data)
    {
        *ctx.has_map_data = false;
    }
    if (ctx.has_visible_map_data)
    {
        *ctx.has_visible_map_data = false;
    }
    if (ctx.render_queue)
    {
        ctx.render_queue->clear();
    }
}

void release_tile_context(TileContext& ctx)
{
    if (!ctx.runtime_acquired)
    {
        return;
    }
    ctx.runtime_acquired = false;
    map_tile_async_host().release();
}
