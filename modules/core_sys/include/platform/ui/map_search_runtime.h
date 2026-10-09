#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace platform::ui::map_search
{
constexpr size_t kMaxResults = 24;
constexpr size_t kQueryBytes = 193;
struct Result
{
    std::array<uint8_t, 16> id{};
    int32_t latitude_e7 = 0, longitude_e7 = 0;
    char name[161]{};
    float distance_m = 0;
    uint16_t importance = 0;
    uint8_t match = 2;
    uint8_t minimum_zoom = 0, maximum_zoom = 0;
    uint32_t zoom_mask = 0;
};
static_assert(sizeof(Result) <= 208, "Search display records must remain bounded");
enum class Status : uint8_t
{
    Idle,
    Searching,
    Ready,
    NoMaps,
    Error
};
struct Snapshot
{
    uint32_t revision = 0;
    uint16_t packages = 0;
    uint8_t count = 0;
    Status status = Status::Idle;
    bool limited = false, incomplete = false;
};
#if defined(ARDUINO_ARCH_ESP32)
bool submit(const char* query, double origin_lat, double origin_lon);
bool poll(Snapshot& snapshot, Result* results, size_t capacity);
void cancel();
#else
inline bool submit(const char*, double, double)
{
    return false;
}
inline bool poll(Snapshot&, Result*, size_t) { return false; }
inline void cancel() {}
#endif
} // namespace platform::ui::map_search
