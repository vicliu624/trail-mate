#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace platform::ui::tmap_creation
{
struct Source
{
    std::string path, name;
    uint64_t bytes = 0;
};
struct Plan
{
    std::string source, name, filename, package_key, country, admin;
    double west = 0, south = 0, east = 0, north = 0;
    uint8_t min_zoom = 13, max_zoom = 14;
    bool osm = true, terrain = false, satellite = false;
    uint64_t revision = 1, maximum_bytes = 512ULL * 1024 * 1024;
};
enum class Phase : uint8_t
{
    Idle,
    Preparing,
    Building,
    Verifying,
    Installing,
    Succeeded,
    Cancelled,
    Failed
};
struct Estimate
{
    bool valid = false;
    uint64_t tiles = 0, raster_bytes = 0, working_bytes = 0, available_bytes = 0;
    std::string error;
};
struct Snapshot
{
    Phase phase = Phase::Idle;
    bool busy = false, producer_ready = false;
    int percent = -1;
    uint64_t completed = 0, total = 0;
    uint32_t source_revision = 0;
    std::string source_directory;
    std::string stage, error, output;
};
// Composition registers a capable producer. Unsupported targets carry no job,
// thread or producer dependency. Views own their bytes across UI callbacks.
class Service
{
  public:
    virtual ~Service() = default;
    virtual std::vector<Source> sources() = 0;
    virtual Estimate estimate(const Plan&) = 0;
    virtual bool start(const Plan&, std::string& error) = 0;
    virtual Snapshot snapshot() = 0;
    virtual void cancel() = 0;
};
inline Service*& registered_service()
{
    static Service* value = nullptr;
    return value;
}
inline void bind(Service* value) { registered_service() = value; }
inline Service* service() { return registered_service(); }
inline bool supported() { return service() != nullptr; }
} // namespace platform::ui::tmap_creation
