#include "platform/ui/navigation_runtime.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/tmap_storage.h"
#include "ui_map_runtime/navigation/archive_calculation.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <sys/random.h>
#include <thread>

namespace platform::ui::navigation
{
namespace
{
namespace native = ::platform::linux_runtime;
constexpr const char* kDirectory = "/routes/navigation";
bool valid(const Endpoint& endpoint)
{
    return endpoint.latitude_e7 >= -900000000 && endpoint.latitude_e7 <= 900000000 &&
           endpoint.longitude_e7 >= -1800000000 && endpoint.longitude_e7 <= 1800000000 &&
           endpoint.name[0] && std::memchr(endpoint.name, 0, sizeof(endpoint.name));
}
std::string filename(const char* text)
{
    size_t size = std::min<size_t>(24, std::strlen(text));
    while (size && (uint8_t(text[size]) & 0xc0) == 0x80) --size;
    std::string result(text, size);
    for (auto& byte : result)
        if (uint8_t(byte) < 32 || std::strchr("/\\:*?\"<>|", byte)) byte = '_';
    while (!result.empty() && (result.back() == '.' || result.back() == ' ')) result.pop_back();
    return result.empty() ? "place" : result;
}
Status failure(tmap::Result value)
{
    switch (value)
    {
    case tmap::Result::NoPath:
        return Status::NoPath;
    case tmap::Result::OutsideCoverage:
    case tmap::Result::Incomplete:
        return Status::OutsideCoverage;
    case tmap::Result::BudgetExceeded:
        return Status::MemoryUnavailable;
    case tmap::Result::Missing:
    case tmap::Result::Unsupported:
    case tmap::Result::Invalid:
        return Status::NoRoutingData;
    case tmap::Result::Cancelled:
        return Status::Cancelled;
    default:
        return Status::StorageUnavailable;
    }
}
struct Job
{
    explicit Job(std::filesystem::path media) : root(std::move(media)), output(root), spool(root), map(root) {}
    ~Job()
    {
        if (output_owned && !saved) output.removeOwned();
        if (spool_owned) spool.removeOwned();
    }
    std::filesystem::path root;
    Request request;
    Snapshot snapshot;
    native::TmapFile output, spool, map;
    std::string pending, spool_path, destination;
    std::atomic_bool cancelled{false};
    bool running = true, output_owned = false, spool_owned = false, saved = false;
};
class Runtime
{
  public:
    ~Runtime()
    {
        if (job_) job_->cancelled = true;
        if (worker_.joinable()) worker_.join();
        job_.reset();
    }
    bool submit(const Request& request)
    {
        if (!valid(request.origin) || !valid(request.destination)) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        retire();
        if (job_) return false;
        try
        {
            auto next = std::make_unique<Job>(native::resolve_paths().sd_root);
            uint64_t random = 0;
            ssize_t bytes;
            do
            {
                bytes = getrandom(&random, sizeof(random), 0);
            } while (bytes < 0 && errno == EINTR);
            if (bytes != sizeof(random)) return false;
            char id[17];
            std::snprintf(id, sizeof(id), "%016llx", static_cast<unsigned long long>(random));
            next->request = request;
            next->pending = std::string(kDirectory) + "/.pending-" + id + ".tmroute";
            next->spool_path = std::string(kDirectory) + "/.spool-" + id;
            next->destination = std::string(kDirectory) + "/nav-" + filename(request.origin.name) + '-' + filename(request.destination.name) + '-' + id + ".tmroute";
            if (physical(*next, next->pending).size() >= sizeof(Snapshot::path) ||
                physical(*next, next->destination).size() >= sizeof(Snapshot::path)) return false;
            next->snapshot.status = Status::Calculating;
            next->snapshot.revision = ++revision_;
            job_ = std::move(next);
            try
            {
                worker_ = std::thread([this]
                                      { run(*job_); });
            }
            catch (...)
            {
                job_.reset();
                return false;
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    bool poll(Snapshot& snapshot)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!job_ || job_->snapshot.revision == snapshot.revision) return false;
        snapshot = job_->snapshot;
        return true;
    }
    void cancel()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (job_) job_->cancelled = true;
        retire();
    }
    bool save()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!job_ || job_->running || job_->cancelled || job_->snapshot.status != Status::Ready) return false;
        if (!job_->output.publish(job_->destination)) return false;
        job_->saved = true;
        job_->output.close();
        job_->snapshot.status = Status::Saved;
        const auto path = physical(*job_, job_->destination);
        std::memcpy(job_->snapshot.path, path.c_str(), path.size() + 1);
        job_->snapshot.revision = ++revision_;
        return true;
    }

  private:
    static std::string physical(const Job& job, const std::string& logical) { return (job.root / logical.substr(1)).string(); }
    void retire()
    {
        if (!job_ || job_->running || !job_->cancelled) return;
        if (worker_.joinable()) worker_.join();
        job_.reset();
    }
    Status calculate(Job& job, Snapshot& snapshot)
    {
        if (!job.output.io().mkdirs(kDirectory)) return Status::StorageUnavailable;
        job.output_owned = job.output.open(job.pending, true);
        if (!job.output_owned) return Status::StorageUnavailable;
        job.spool_owned = job.spool.open(job.spool_path, true);
        if (!job.spool_owned) return Status::StorageUnavailable;
        native::TmapCatalog catalog(job.root, true);
        native::TmapMemory memory;
        ::navigation::ArchiveCalculation calculation(memory, tmap::MemoryPolicy::InternalOnly);
        Status status = Status::NoRoutingData;
        std::string path;
        while (!job.cancelled)
        {
            if (!job.output.current() || !job.spool.current()) return Status::StorageUnavailable;
            const auto next = catalog.next(path);
            if (next == tmap::Status::More)
            {
                std::this_thread::yield();
                continue;
            }
            if (next == tmap::Status::Missing) return status;
            if (next != tmap::Status::Ok || !job.map.open(path)) return Status::StorageUnavailable;
            if (!job.output.clear() || !job.spool.clear()) return Status::StorageUnavailable;
            auto result = calculation.begin(job.map, job.spool, job.output,
                                            {job.request.origin.latitude_e7, job.request.origin.longitude_e7},
                                            {job.request.destination.latitude_e7, job.request.destination.longitude_e7}, {});
            auto phase = calculation.phase();
            while (!job.cancelled && ::navigation::ArchiveCalculation::pending(result))
            {
                phase = calculation.phase();
                result = calculation.step(256);
                std::this_thread::yield();
            }
            if (job.cancelled) return Status::Cancelled;
            if (result == tmap::Result::Ok)
            {
                result = calculation.beginExport();
                while (!job.cancelled && ::navigation::ArchiveCalculation::pending(result))
                {
                    result = calculation.step(4);
                    std::this_thread::yield();
                }
                if (job.cancelled) return Status::Cancelled;
                if (result != tmap::Result::Ok || !job.output.flush()) return failure(result == tmap::Result::Ok ? tmap::Result::IoError : result);
                snapshot.points = static_cast<uint32_t>(calculation.pointCount());
                snapshot.distance_m = calculation.lengthCm() / 100.0;
                const auto output = physical(job, job.pending);
                std::memcpy(snapshot.path, output.c_str(), output.size() + 1);
                return Status::Ready;
            }
            // The integer router reports Missing when neither endpoint snaps
            // to a road within the configured radius. This is different from
            // an absent graph section during package initialization.
            const auto reason = result == tmap::Result::Missing && phase == ::navigation::ArchiveCalculation::Phase::Searching
                                    ? Status::OutsideCoverage
                                    : failure(result);
            if (reason == Status::StorageUnavailable || reason == Status::MemoryUnavailable) return reason;
            if (reason != Status::NoRoutingData) status = reason;
            calculation.reset();
            job.map.close();
        }
        return Status::Cancelled;
    }
    void run(Job& job)
    {
        Snapshot snapshot;
        Status status;
        try
        {
            status = calculate(job, snapshot);
        }
        catch (const std::bad_alloc&)
        {
            status = Status::MemoryUnavailable;
        }
        catch (...)
        {
            status = Status::StorageUnavailable;
        }
        job.map.close();
        if (job.spool_owned) job.spool.removeOwned();
        job.spool_owned = false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (job.cancelled) status = Status::Cancelled;
        else if (status == Status::Ready && !job.output.current()) status = Status::StorageUnavailable;
        if (status != Status::Ready)
        {
            if (job.output_owned) job.output.removeOwned();
            job.output_owned = false;
            snapshot = {};
        }
        snapshot.status = status;
        snapshot.revision = ++revision_;
        job.snapshot = snapshot;
        job.running = false;
    }
    std::mutex mutex_;
    std::unique_ptr<Job> job_;
    std::thread worker_;
    uint32_t revision_ = 0;
};
Runtime& runtime()
{
    static Runtime value;
    return value;
}
bool supported_name(const std::string& name)
{
    if (name.empty() || name.front() == '.' || name.size() >= 128 || name.find_first_of("/\\") != std::string::npos) return false;
    auto extension = std::filesystem::path(name).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char byte)
                   { return char(std::tolower(byte)); });
    return extension == ".tmroute" || extension == ".kml";
}
} // namespace
bool supported() { return true; }
bool submit(const Request& request) { return runtime().submit(request); }
bool poll(Snapshot& snapshot) { return runtime().poll(snapshot); }
bool save() { return runtime().save(); }
void cancel() { runtime().cancel(); }
const char* directory()
{
    static thread_local std::string path;
    path = (native::resolve_paths().sd_root / "routes/navigation").string();
    return path.c_str();
}
bool list(std::vector<std::string>& names, size_t maximum)
{
    names.clear();
    native::UploadIo io(native::resolve_paths().sd_root);
    if (!io.mkdirs(kDirectory)) return false;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory(), error), end; !error && it != end && names.size() < maximum; it.increment(error))
    {
        if (it->is_symlink(error) || error || !it->is_regular_file(error) || error || it->hard_link_count(error) != 1 || error) continue;
        const auto name = it->path().filename().string();
        if (supported_name(name)) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return !error && io.current();
}
bool remove(const char* name)
{
    if (!name || !supported_name(name)) return false;
    native::UploadIo io(native::resolve_paths().sd_root);
    const auto logical = std::string(kDirectory) + '/' + name;
    const auto file = io.open(logical.c_str(), native::UploadIo::Mode::Read);
    return file.handle && io.lock_open(file.handle) && io.remove(logical.c_str());
}
} // namespace platform::ui::navigation
