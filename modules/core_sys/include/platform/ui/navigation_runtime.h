#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform::ui::navigation
{
struct Endpoint
{
    int32_t latitude_e7 = 0, longitude_e7 = 0;
    char name[161]{};
};
struct Request
{
    Endpoint origin{}, destination{};
};
enum class Status : uint8_t
{
    Idle,
    Calculating,
    Ready,
    Saved,
    NoRoutingData,
    NoPath,
    OutsideCoverage,
    MemoryUnavailable,
    StorageUnavailable,
    Cancelled
};
struct Snapshot
{
    Status status = Status::Idle;
    uint32_t revision = 0, points = 0;
    double distance_m = 0;
    char path[160]{};
};
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM) || defined(__linux__)
bool supported();
bool submit(const Request& request);
bool poll(Snapshot& snapshot);
bool save();
void cancel();
bool list(std::vector<std::string>& names, size_t maximum = 64);
bool remove(const char* name);
const char* directory();
#else
inline bool supported()
{
    return false;
}
inline bool submit(const Request&) { return false; }
inline bool poll(Snapshot&) { return false; }
inline bool save() { return false; }
inline void cancel() {}
inline bool list(std::vector<std::string>&, size_t = 64) { return false; }
inline bool remove(const char*) { return false; }
inline const char* directory() { return "/routes/navigation"; }
#endif
} // namespace platform::ui::navigation
