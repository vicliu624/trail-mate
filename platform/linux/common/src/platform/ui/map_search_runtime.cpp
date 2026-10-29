#include "platform/ui/map_search_runtime.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/tmap_storage.h"
#include "tmap/administrative_path.h"
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace platform::ui::map_search
{
namespace
{
namespace native = ::platform::linux_runtime;
void copy(char* destination, size_t capacity, const char* source)
{
    size_t size = std::min(std::strlen(source), capacity - 1);
    while (size && (uint8_t(source[size]) & 0xc0) == 0x80) --size;
    std::memcpy(destination, source, size);
    destination[size] = 0;
}
bool better(const Result& first, const Result& second)
{
    if (first.distance_m != second.distance_m) return first.distance_m < second.distance_m;
    if (first.match != second.match) return first.match < second.match;
    if (first.importance != second.importance) return first.importance > second.importance;
    return first.id < second.id;
}
struct Request
{
    std::string query;
    std::filesystem::path root;
    std::shared_ptr<native::UploadIo> media;
    double latitude = 0, longitude = 0;
    uint32_t generation = 0;
    bool region = false;
};
struct Work
{
    Work(const Request& request, const std::atomic<uint32_t>& generation)
        : request(request), generation(generation), file(request.root), reader(workspace) {}
    bool current() const { return generation.load() == request.generation; }
    static bool accept(void* context, const tmap::Poi& poi, tmap::SearchMode match)
    {
        auto& work = *static_cast<Work*>(context);
        if (!work.current()) return false;
        Result candidate;
        candidate.id = poi.id;
        candidate.latitude_e7 = poi.latitude;
        candidate.longitude_e7 = poi.longitude;
        candidate.importance = poi.importance;
        candidate.match = static_cast<uint8_t>(match);
        candidate.zoom_mask = work.reader.package().zoom_mask;
        bool minimum = false;
        for (uint8_t zoom = 0; zoom <= 18; ++zoom)
            if (candidate.zoom_mask & (uint32_t{1} << zoom))
            {
                if (!minimum)
                {
                    candidate.minimum_zoom = zoom;
                    minimum = true;
                }
                candidate.maximum_zoom = zoom;
            }
        if (work.request.region)
        {
            uint8_t levels = 0;
            work.region_io = work.reader.administrativeLocation(poi.row, candidate.administrative_path,
                                                                sizeof(candidate.administrative_path), levels, candidate.administrative_flags);
            if (work.region_io != tmap::Status::Ok) return false;
            if (!candidate.administrative_path[0] || (candidate.administrative_flags & 2)) return true;
            copy(candidate.name, sizeof(candidate.name), poi.name);
        }
        else
        {
            copy(candidate.name, sizeof(candidate.name), work.reader.searchMatchedName());
            copy(candidate.administrative_path, sizeof(candidate.administrative_path), work.reader.searchAdministrativePath());
            candidate.administrative_flags = work.reader.searchAdministrativeFlags();
        }
        constexpr double radians = 0.017453292519943295;
        const double latitude = poi.latitude / 1e7, longitude = poi.longitude / 1e7;
        const double a = std::sin((latitude - work.request.latitude) * radians / 2);
        const double b = std::sin((longitude - work.request.longitude) * radians / 2);
        const double h = a * a + std::cos(latitude * radians) * std::cos(work.request.latitude * radians) * b * b;
        candidate.distance_m = float(12742000 * std::asin(std::sqrt(std::clamp(h, 0.0, 1.0))));
        if (work.request.region && candidate.distance_m > 30000) return true;
        const size_t capacity = work.request.region ? 4 : kMaxResults;
        for (size_t index = 0; index < work.snapshot.count; ++index)
            if (work.results[index].id == candidate.id)
            {
                const auto& previous = work.results[index];
                if (candidate.match < previous.match ||
                    (candidate.match == previous.match && candidate.maximum_zoom > previous.maximum_zoom) ||
                    (candidate.match == previous.match && candidate.maximum_zoom == previous.maximum_zoom && better(candidate, previous)))
                    work.results[index] = candidate;
                return true;
            }
        size_t slot = work.snapshot.count;
        if (slot == capacity)
        {
            work.snapshot.limited = true;
            slot = 0;
            for (size_t index = 1; index < capacity; ++index)
                if (better(work.results[slot], work.results[index])) slot = index;
            if (!better(candidate, work.results[slot])) return true;
        }
        else ++work.snapshot.count;
        work.results[slot] = candidate;
        return true;
    }
    static bool acceptRegion(void* context, const tmap::Poi& poi)
    {
        auto& work = *static_cast<Work*>(context);
        if (!work.current()) return false;
        if (++work.region_visited > 256)
        {
            work.snapshot.limited = true;
            return false;
        }
        return accept(context, poi, tmap::SearchMode::Exact);
    }
    Request request;
    const std::atomic<uint32_t>& generation;
    native::TmapFile file;
    tmap::Workspace workspace;
    tmap::Reader reader;
    tmap::SearchCursor cursor;
    std::array<Result, kMaxResults> results{};
    Snapshot snapshot;
    uint32_t region_visited = 0;
    tmap::Status region_io = tmap::Status::Ok;
};
class Runtime
{
  public:
    ~Runtime()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            ++generation_;
        }
        changed_.notify_one();
        if (worker_.joinable()) worker_.join();
    }
    uint32_t submit(const char* query, double latitude, double longitude, bool region)
    {
        if (!std::isfinite(latitude) || !std::isfinite(longitude) || std::abs(latitude) > 90 || std::abs(longitude) > 180) return 0;
        if (!query || strnlen(query, kQueryBytes) >= kQueryBytes) return 0;
        char normalized[kQueryBytes];
        if (!region && (!query[0] || !tmap::Reader::normalizeName(query, std::strlen(query), normalized, sizeof(normalized)) || !normalized[0])) return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        try
        {
            Request request;
            request.query = query;
            request.root = native::resolve_paths().sd_root;
            request.media = std::make_shared<native::UploadIo>(request.root);
            request.latitude = latitude;
            request.longitude = longitude;
            request.region = region;
            if (!worker_.joinable()) worker_ = std::thread([this]
                                                           { run(); });
            request.generation = ++generation_;
            if (!request.generation) request.generation = ++generation_;
            active_ = request;
            pending_ = std::move(request);
            snapshot_ = {};
            snapshot_.status = Status::Searching;
            snapshot_.revision = ++revision_;
            changed_.notify_one();
            return active_.generation;
        }
        catch (...)
        {
            return 0;
        }
    }
    bool poll(Snapshot& snapshot, Result* results, size_t capacity)
    {
        if (!results || capacity < kMaxResults) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (snapshot.revision == snapshot_.revision) return false;
        snapshot = snapshot_;
        std::copy_n(results_.begin(), snapshot_.count, results);
        return true;
    }
    bool region(uint32_t token, RegionResult& output)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!token || token != generation_ || !active_.region) return false;
        output = {};
        output.status = snapshot_.status;
        if (output.status == Status::Ready && (!active_.media || !active_.media->current())) output.status = Status::Error;
        if (output.status == Status::Ready && snapshot_.count)
        {
            copy(output.path, sizeof(output.path), results_[0].administrative_path);
            for (size_t index = 1; index < snapshot_.count && output.path[0]; ++index)
                tmap::commonAdministrativePath(output.path, results_[index].administrative_path);
        }
        return true;
    }
    void cancel()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        pending_.reset();
        active_ = {};
        snapshot_ = {};
        snapshot_.revision = ++revision_;
    }

  private:
    void publish(Work& work, Status status)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!work.current() || stopping_) return;
        std::sort(work.results.begin(), work.results.begin() + work.snapshot.count, better);
        results_ = work.results;
        snapshot_ = work.snapshot;
        snapshot_.status = status;
        snapshot_.revision = ++revision_;
    }
    Status search(Work& work)
    {
        native::TmapCatalog catalog(work.request.root);
        const auto& media = work.request.media;
        if (!media || !media->current()) return Status::Error;
        std::string path;
        while (work.current())
        {
            if (!media->current()) return Status::Error;
            const auto next = catalog.next(path);
            if (next == tmap::Status::More)
            {
                std::this_thread::yield();
                continue;
            }
            if (next == tmap::Status::Missing) return work.snapshot.packages ? Status::Ready : work.snapshot.incomplete ? Status::Error
                                                                                                                        : Status::NoMaps;
            if (next != tmap::Status::Ok)
            {
                work.snapshot.incomplete = true;
                return Status::Error;
            }
            if (!work.file.open(path) || work.reader.open(work.file) != tmap::Status::Ok)
            {
                work.snapshot.incomplete = true;
                work.reader.close();
                work.file.close();
                continue;
            }
            if (!work.reader.poiCount())
            {
                work.reader.close();
                work.file.close();
                continue;
            }
            ++work.snapshot.packages;
            tmap::Status step;
            if (work.request.region)
            {
                constexpr double radians = 0.017453292519943295;
                const double delta = std::min(180.0, 0.27 / std::max(0.01, std::cos(work.request.latitude * radians)));
                const bool wraps = work.request.longitude - delta < -180 || work.request.longitude + delta >= 180;
                tmap::Bounds bounds;
                bounds.south = int32_t(std::max(-90.0, work.request.latitude - 0.27) * 1e7);
                bounds.north = int32_t(std::min(90.0, work.request.latitude + 0.27) * 1e7);
                bounds.west = int32_t((wraps ? -180.0 : work.request.longitude - delta) * 1e7);
                bounds.east = int32_t((wraps ? 179.9999999 : work.request.longitude + delta) * 1e7);
                work.region_visited = 0;
                step = work.reader.queryBounds(bounds, Work::acceptRegion, &work);
                if (step == tmap::Status::Cancelled && work.current())
                    step = work.region_io != tmap::Status::Ok ? work.region_io : work.snapshot.limited ? tmap::Status::Ok
                                                                                                       : step;
            }
            else
            {
                step = work.reader.beginSearch(work.request.query.c_str(), work.request.query.size(), tmap::SearchMode::Substring, work.cursor);
                if (step == tmap::Status::Ok)
                    do
                    {
                        step = work.reader.searchStep(work.cursor, 4, Work::accept, &work);
                        std::this_thread::yield();
                    } while (work.current() && (step == tmap::Status::More || step == tmap::Status::Busy));
            }
            if (step != tmap::Status::Ok) work.snapshot.incomplete = true;
            work.reader.close();
            work.file.close();
            publish(work, Status::Searching);
        }
        return Status::Idle;
    }
    void run()
    {
        for (;;)
        {
            Request request;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait(lock, [this]
                              { return stopping_ || pending_.has_value(); });
                if (stopping_) return;
                request = std::move(*pending_);
                pending_.reset();
            }
            try
            {
                auto work = std::make_unique<Work>(request, generation_);
                const auto status = search(*work);
                publish(*work, status);
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation_ == request.generation)
                {
                    snapshot_.status = Status::Error;
                    snapshot_.incomplete = true;
                    snapshot_.revision = ++revision_;
                }
            }
        }
    }
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    std::optional<Request> pending_;
    Request active_;
    std::atomic<uint32_t> generation_{0};
    Snapshot snapshot_;
    std::array<Result, kMaxResults> results_{};
    uint32_t revision_ = 0;
    bool stopping_ = false;
};
Runtime& runtime()
{
    static Runtime value;
    return value;
}
} // namespace
bool submit(const char* query, double latitude, double longitude) { return runtime().submit(query, latitude, longitude, false) != 0; }
bool poll(Snapshot& snapshot, Result* results, size_t capacity) { return runtime().poll(snapshot, results, capacity); }
void cancel() { runtime().cancel(); }
uint32_t submitRegion(double latitude, double longitude) { return runtime().submit("", latitude, longitude, true); }
bool pollRegion(uint32_t request, RegionResult& output) { return runtime().region(request, output); }
} // namespace platform::ui::map_search
