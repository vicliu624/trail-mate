#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/map_tiles/sd_tmap_storage.h"
#include "platform/ui/map_search_runtime.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <new>

namespace platform::ui::map_search
{
namespace
{
// All buffers, cursors, reader pages and result arrays are exclusively PSRAM.
// The lazily created task uses a bounded 4096-byte internal stack and exits
// after completion/cancellation. No LVGL object is touched by the worker.
struct State
{
    esp::map_tiles::SdTmapStorage storage;
    tmap::Workspace workspace{};
    tmap::Reader reader{workspace};
    tmap::SearchCursor cursor{};
    Result working[kMaxResults]{}, published[kMaxResults]{};
    Result candidate{};
    char pending_query[kQueryBytes]{}, query[kQueryBytes]{}, normalized[kQueryBytes]{};
    char path[192]{};
    Snapshot snapshot{};
    std::atomic<uint32_t> generation{0};
    bool running = false, cancelled = true;
    double pending_lat = 0, pending_lon = 0, latitude = 0, longitude = 0;
    uint8_t count = 0;
    uint16_t packages = 0;
    bool limited = false, incomplete = false;
};
State* s_state = nullptr;
SemaphoreHandle_t s_mutex = nullptr;

void copy_name(char* output, size_t capacity, const char* source)
{
    size_t n = std::min(std::strlen(source), capacity - 1);
    while (n && (static_cast<uint8_t>(source[n]) & 0xc0) == 0x80) --n;
    std::memcpy(output, source, n);
    output[n] = 0;
}
bool better(const Result& a, const Result& b)
{
    if (a.distance_m != b.distance_m) return a.distance_m < b.distance_m;
    if (a.match != b.match) return a.match < b.match;
    if (a.importance != b.importance) return a.importance > b.importance;
    return a.id < b.id;
}
bool accept(void* context, const tmap::Poi& poi, tmap::SearchMode match)
{
    auto& state = *static_cast<State*>(context);
    auto& candidate = state.candidate;
    candidate.id = poi.id;
    candidate.latitude_e7 = poi.latitude;
    candidate.longitude_e7 = poi.longitude;
    candidate.importance = poi.importance;
    candidate.match = static_cast<uint8_t>(match);
    candidate.minimum_zoom = 0;
    candidate.maximum_zoom = 0;
    const auto mask = state.reader.package().zoom_mask;
    candidate.zoom_mask = mask;
    for (uint8_t zoom = 0; zoom <= 18; ++zoom)
        if (mask & (uint32_t{1} << zoom))
        {
            if (!candidate.minimum_zoom && zoom) candidate.minimum_zoom = zoom;
            candidate.maximum_zoom = zoom;
        }
    if (mask & 1U) candidate.minimum_zoom = 0;
    copy_name(candidate.name, sizeof(candidate.name), poi.name);
    constexpr double radians = 3.14159265358979323846 / 180;
    const double latitude = poi.latitude / 1e7;
    const double longitude = poi.longitude / 1e7;
    const double a = std::sin((latitude - state.latitude) * radians / 2);
    const double b = std::sin((longitude - state.longitude) * radians / 2);
    const double h = a * a + std::cos(latitude * radians) * std::cos(state.latitude * radians) * b * b;
    candidate.distance_m = static_cast<float>(12742000 * std::asin(std::sqrt(std::clamp(h, 0.0, 1.0))));
    size_t slot = state.count;
    for (size_t i = 0; i < state.count; ++i)
        if (state.working[i].id == poi.id)
        {
            if (candidate.match < state.working[i].match ||
                (candidate.match == state.working[i].match && candidate.maximum_zoom > state.working[i].maximum_zoom) ||
                (candidate.match == state.working[i].match && candidate.maximum_zoom == state.working[i].maximum_zoom && better(candidate, state.working[i])))
                state.working[i] = candidate;
            return true;
        }
    if (slot == kMaxResults)
    {
        state.limited = true;
        slot = 0;
        for (size_t i = 1; i < state.count; ++i)
            if (better(state.working[slot], state.working[i])) slot = i;
        if (!better(candidate, state.working[slot])) return true;
    }
    else ++state.count;
    state.working[slot] = candidate;
    return true;
}
void publish(State& state, uint32_t generation, Status status)
{
    // A terminal snapshot must reach the UI before the task exits.
    const TickType_t wait = status == Status::Searching ? pdMS_TO_TICKS(10) : portMAX_DELAY;
    if (xSemaphoreTake(s_mutex, wait) != pdTRUE) return;
    if (state.generation.load() == generation && !state.cancelled)
    {
        std::memcpy(state.published, state.working, state.count * sizeof(Result));
        // Fixed 24-entry selection sort: no result-sized automatic temporaries.
        for (size_t i = 0; i < state.count; ++i)
        {
            size_t selected = i;
            for (size_t j = i + 1; j < state.count; ++j)
                if (better(state.published[j], state.published[selected])) selected = j;
            if (selected != i)
            {
                state.candidate = state.published[i];
                state.published[i] = state.published[selected];
                state.published[selected] = state.candidate;
            }
        }
        ++state.snapshot.revision;
        state.snapshot.count = state.count;
        state.snapshot.packages = state.packages;
        state.snapshot.status = status;
        state.snapshot.limited = state.limited;
        state.snapshot.incomplete = state.incomplete;
    }
    xSemaphoreGive(s_mutex);
}
void worker(void* context)
{
    auto& state = *static_cast<State*>(context);
    for (;;)
    {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (state.cancelled)
        {
            state.running = false;
            xSemaphoreGive(s_mutex);
            break;
        }
        const uint32_t generation = state.generation.load();
        std::strcpy(state.query, state.pending_query);
        state.latitude = state.pending_lat;
        state.longitude = state.pending_lon;
        xSemaphoreGive(s_mutex);
        state.count = 0;
        state.packages = 0;
        state.limited = state.incomplete = false;
        state.path[0] = 0;
        state.reader.close();
        state.storage.closePackage();
        state.storage.resetEnumeration();
        const uint32_t media = state.storage.session();
        uint32_t busy_since = 0, last_publish = 0;
        const uint32_t started = state.storage.nowMs();
        uint32_t last_diagnostic = started, busy_retries = 0;
        std::printf("[TMAP][SEARCH] begin generation=%lu media=%lu query=%s\n",
                    static_cast<unsigned long>(generation), static_cast<unsigned long>(media), state.query);
        uint16_t files = 0;
        bool active = false, finished = false;
        Status status = Status::Searching;
        while (!finished && generation == state.generation.load())
        {
            if (media != state.storage.session())
            {
                status = Status::Error;
                state.incomplete = true;
                break;
            }
            tmap::Status step = tmap::Status::Ok;
            if (!state.path[0])
            {
                step = state.storage.nextPackage(state.path, sizeof(state.path));
                if (step == tmap::Status::Ok)
                    std::printf("[TMAP][SEARCH] package generation=%lu path=%s\n", static_cast<unsigned long>(generation), state.path);
                if (step == tmap::Status::Missing)
                {
                    status = state.packages ? Status::Ready : state.incomplete ? Status::Error
                                                                               : Status::NoMaps;
                    finished = true;
                }
                else if (step == tmap::Status::Ok && ++files > 256)
                {
                    state.incomplete = true;
                    status = Status::Error;
                    finished = true;
                }
            }
            else if (!active)
            {
                step = state.storage.openPackage(state.path);
                if (step == tmap::Status::Ok) step = state.reader.open(state.storage);
                if (step == tmap::Status::Ok)
                {
                    if (!state.reader.poiCount())
                    {
                        state.path[0] = 0;
                        state.reader.close();
                        state.storage.closePackage();
                    }
                    else
                    {
                        step = state.reader.beginSearch(state.query, std::strlen(state.query), tmap::SearchMode::Substring, state.cursor);
                        if (step == tmap::Status::Ok)
                        {
                            active = true;
                            ++state.packages;
                        }
                    }
                }
            }
            else
            {
                step = state.reader.searchStep(state.cursor, 4, accept, &state);
                if (step == tmap::Status::Ok)
                {
                    std::printf("[TMAP][SEARCH] package_done generation=%lu path=%s candidates=%llu/%llu retained=%u reads=%llu\n",
                                static_cast<unsigned long>(generation), state.path,
                                static_cast<unsigned long long>(state.cursor.position), static_cast<unsigned long long>(state.cursor.posting.count),
                                state.count, static_cast<unsigned long long>(state.reader.pageReads()));
                    active = false;
                    state.path[0] = 0;
                    state.reader.close();
                    state.storage.closePackage();
                }
            }
            const uint32_t now = state.storage.nowMs();
            if (step == tmap::Status::Busy)
            {
                ++busy_retries;
                if (!busy_since) busy_since = now;
                if (now - busy_since >= 30000)
                {
                    state.incomplete = true;
                    status = Status::Error;
                    finished = true;
                }
            }
            else
            {
                busy_since = 0;
                if (step != tmap::Status::Ok && step != tmap::Status::More && step != tmap::Status::Missing)
                {
                    state.incomplete = true;
                    // An unreadable file must not hide results from other maps.
                    if (!state.path[0])
                    {
                        status = Status::Error;
                        finished = true;
                    }
                    active = false;
                    state.path[0] = 0;
                    state.reader.close();
                    state.storage.closePackage();
                }
            }
            if (finished || now - last_publish >= 250)
            {
                publish(state, generation, status);
                last_publish = now;
            }
            if (now - last_diagnostic >= 1000)
            {
                std::printf("[TMAP][SEARCH] progress generation=%lu phase=%s path=%s step=%u elapsed_ms=%lu initialized=%u query_offset=%u candidates=%llu/%llu retained=%u reads=%llu busy=%lu stack_min_bytes=%u\n",
                            static_cast<unsigned long>(generation), active ? "query" : state.path[0] ? "open"
                                                                                                     : "enumerate",
                            state.path, static_cast<unsigned>(step), static_cast<unsigned long>(now - started),
                            state.cursor.initialized, state.cursor.query_offset,
                            static_cast<unsigned long long>(state.cursor.position), static_cast<unsigned long long>(state.cursor.posting.count),
                            state.count, static_cast<unsigned long long>(state.reader.pageReads()), static_cast<unsigned long>(busy_retries),
                            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
                last_diagnostic = now;
            }
            vTaskDelay(pdMS_TO_TICKS(step == tmap::Status::Busy ? 20 : 5));
        }
        publish(state, generation, status);
        state.reader.close();
        state.storage.closePackage();
        state.storage.resetEnumeration();
        std::printf("[TMAP][SEARCH] generation=%lu packages=%u results=%u limited=%u incomplete=%u status=%u\n",
                    static_cast<unsigned long>(generation), state.packages, state.count, state.limited, state.incomplete,
                    static_cast<unsigned>(status));
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (generation == state.generation.load() || state.cancelled)
        {
            state.running = false;
            xSemaphoreGive(s_mutex);
            break;
        }
        xSemaphoreGive(s_mutex);
    }
    vTaskDelete(nullptr);
}
} // namespace

bool submit(const char* query, double origin_lat, double origin_lon)
{
    if (!query || !query[0] || std::strlen(query) >= kQueryBytes) return false;
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex || xSemaphoreTake(s_mutex, 0) != pdTRUE) return false;
    if (!s_state)
    {
        void* storage = heap_caps_aligned_alloc(alignof(State), sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (storage) s_state = new (storage) State{};
    }
    bool ok = s_state && tmap::Reader::normalizeName(query, std::strlen(query), s_state->normalized, sizeof(s_state->normalized)) && s_state->normalized[0];
    if (ok)
    {
        auto& state = *s_state;
        std::strcpy(state.pending_query, query);
        state.pending_lat = std::isfinite(origin_lat) ? origin_lat : 0;
        state.pending_lon = std::isfinite(origin_lon) ? origin_lon : 0;
        state.cancelled = false;
        ++state.generation;
        state.snapshot.count = 0;
        state.snapshot.packages = 0;
        state.snapshot.status = Status::Searching;
        state.snapshot.limited = state.snapshot.incomplete = false;
        ++state.snapshot.revision;
        std::printf("[TMAP][SEARCH] submit generation=%lu query_bytes=%u state_psram_bytes=%u stack_bytes=4096\n",
                    static_cast<unsigned long>(state.generation.load()), static_cast<unsigned>(std::strlen(query)), static_cast<unsigned>(sizeof(State)));
        if (!state.running)
        {
            state.running = true;
            if (xTaskCreate(worker, "tmap_search", 4096, &state, 1, nullptr) != pdPASS)
            {
                state.running = false;
                state.cancelled = true;
                state.snapshot.status = Status::Error;
                ok = false;
            }
        }
    }
    xSemaphoreGive(s_mutex);
    return ok;
}
bool poll(Snapshot& snapshot, Result* results, size_t capacity)
{
    if (!s_state || !results || capacity < kMaxResults || xSemaphoreTake(s_mutex, 0) != pdTRUE) return false;
    const bool changed = snapshot.revision != s_state->snapshot.revision;
    if (changed)
    {
        snapshot = s_state->snapshot;
        std::memcpy(results, s_state->published, snapshot.count * sizeof(Result));
    }
    xSemaphoreGive(s_mutex);
    return changed;
}
void cancel()
{
    if (!s_state || !s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    s_state->cancelled = true;
    ++s_state->generation;
    s_state->snapshot.count = 0;
    s_state->snapshot.status = Status::Idle;
    s_state->snapshot.limited = s_state->snapshot.incomplete = false;
    s_state->snapshot.packages = 0;
    ++s_state->snapshot.revision;
    xSemaphoreGive(s_mutex);
}
} // namespace platform::ui::map_search
