#include "platform/linux/tmap_creation.h"
#include "platform/linux/tmap_job_workspace.h"
#include "platform/linux/hostlink_upload_io.h"
#include "platform/linux/route_image_source_io.h"
#include "platform/linux/runtime_paths.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char** environ;

namespace platform::linux_runtime
{
namespace tc = ::platform::ui::tmap_creation;
namespace
{
std::string json(const std::string& value)
{
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value)
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c) << std::dec;
        else out << c;
    out << '"';
    return out.str();
}
bool component(const std::string& value)
{
    return !value.empty() && value.size() <= 96 && value != "." && value != ".." &&
           value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") == std::string::npos;
}
bool regular(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)) && !error;
}
std::string output_filename(const std::string& filename, const std::string& style)
{
    auto stem = std::filesystem::path(filename).stem().string();
    for (const auto* existing : {"osm", "terrain", "satellite"})
    {
        const std::string tag = '-' + std::string(existing);
        const auto at = stem.find(tag + '-');
        if (at != std::string::npos) return stem.substr(0, at) + '-' + style + stem.substr(at + tag.size()) + ".tmap";
        if (stem.size() >= tag.size() && stem.substr(stem.size() - tag.size()) == tag) return stem.substr(0, stem.size() - tag.size()) + '-' + style + ".tmap";
    }
    const auto zoom = stem.rfind("-z");
    return zoom != std::string::npos && zoom + 2 < stem.size() && stem[zoom + 2] >= '0' && stem[zoom + 2] <= '9'
               ? stem.substr(0, zoom) + '-' + style + stem.substr(zoom) + ".tmap"
               : stem + '-' + style + ".tmap";
}
std::string plan_json(const tc::Plan& p, const std::filesystem::path& output, const std::filesystem::path& cache)
{
    std::ostringstream out;
    out << std::setprecision(15)
        << "{\"Name\":" << json(p.name) << ",\"Area\":{\"Name\":" << json(p.name)
        << ",\"Bounds\":{\"West\":" << p.west << ",\"South\":" << p.south << ",\"East\":" << p.east << ",\"North\":" << p.north << "}},"
        << "\"BaseLayers\":{\"IncludeOsm\":" << (p.osm ? "true" : "false") << ",\"IncludeTerrain\":" << (p.terrain ? "true" : "false")
        << ",\"IncludeSatellite\":" << (p.satellite ? "true" : "false") << ",\"MinimumZoom\":" << unsigned(p.min_zoom) << ",\"MaximumZoom\":" << unsigned(p.max_zoom) << "},"
        << "\"Poi\":{\"PbfPath\":\"/proc/self/fd/198\",\"EnablePoiSeparation\":true},\"Annotations\":{},\"OutputDirectory\":" << json(output.string())
        << ",\"Tmap\":{\"FileName\":" << json(p.filename) << ",\"PackageKey\":" << json(p.package_key)
        << ",\"CountryCode\":" << json(p.country) << ",\"AdminCode\":" << json(p.admin)
        << ",\"Tier\":\"Custom\",\"Revision\":" << p.revision << ",\"MaximumOutputBytes\":" << p.maximum_bytes
        << ",\"SourceCacheDirectory\":" << json(cache.string()) << "}}";
    return out.str();
}
} // namespace
struct LinuxTmapCreation::Impl
{
    explicit Impl(TmapCreationOptions options) : options(std::move(options)), worker([this]
                                                                                     { run(); }) {}
    ~Impl()
    {
        stopping = true;
        cancelled = true;
        changed.notify_all();
        if (worker.joinable()) worker.join();
    }
    void scan()
    {
        std::vector<tc::Source> found;
        std::vector<std::filesystem::path> roots{options.storage / "map-sources", options.storage / "maps/sources"};
        std::error_code preparation;
        if (std::filesystem::is_directory(options.storage, preparation)) std::filesystem::create_directories(roots.front(), preparation);
        if (const auto* value = std::getenv("TRAIL_MATE_TMAP_SOURCE_ROOT")) roots.emplace_back(value);
        for (const auto& root : roots)
        {
            std::error_code error;
            std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error), end;
            for (; !error && it != end && found.size() < 256; it.increment(error))
            {
                if (it.depth() >= 3) it.disable_recursion_pending();
                const auto name = it->path().filename().string();
                if (name.size() > 4 && name.substr(name.size() - 4) == ".pbf" && regular(it->path()))
                    found.push_back({it->path().string(), name, it->file_size(error)});
            }
        }
        std::error_code error;
        const auto space = std::filesystem::space(options.storage, error);
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b)
                  { return a.path < b.path; });
        std::lock_guard<std::mutex> lock(mutex);
        inputs = std::move(found);
        available = error ? 0 : space.available;
        state.source_directory = roots.front().string();
        state.source_revision = ++source_revision;
        state.producer_ready = ::access(options.producer.c_str(), X_OK) == 0;
    }
    tc::Estimate estimate(const tc::Plan& p)
    {
        tc::Estimate result;
        auto invalid = [&](const char* text)
        { result.error = text; return result; };
        if (!component(p.filename) || p.filename.size() < 6 || p.filename.substr(p.filename.size() - 5) != ".tmap" || !component(p.package_key)) return invalid("Choose a simple .tmap filename and package key.");
        if (p.name.empty() || p.name.size() > 96 || !p.revision || p.country.size() > 16 || p.admin.size() > 64) return invalid("Enter a map name and valid package metadata.");
        if (!std::isfinite(p.west) || !std::isfinite(p.east) || !std::isfinite(p.south) || !std::isfinite(p.north) ||
            p.west < -180 || p.east > 180 || p.south < -85.051128 || p.north > 85.051128 || p.west >= p.east || p.south >= p.north) return invalid("Enter explicit WGS84 bounds; split areas crossing the date line.");
        if (p.min_zoom > p.max_zoom || p.max_zoom > 18 || (!p.osm && !p.terrain && !p.satellite)) return invalid("Select a base layer and a zoom range from 0 to 18.");
        uint64_t source_bytes = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto source = std::find_if(inputs.begin(), inputs.end(), [&](const auto& value)
                                             { return value.path == p.source; });
            if (source == inputs.end()) return invalid("Select a local .osm.pbf file from map-sources.");
            source_bytes = source->bytes;
            result.available_bytes = available;
        }
        const unsigned layers = unsigned(p.osm) + unsigned(p.terrain) + unsigned(p.satellite);
        for (unsigned z = p.min_zoom; z <= p.max_zoom; ++z)
        {
            const double n = double(uint64_t(1) << z);
            const auto x = [&](double lon)
            { return std::clamp<int64_t>(int64_t(std::floor((lon + 180) / 360 * n)), 0, int64_t(n) - 1); };
            const auto y = [&](double lat)
            { const double rad = lat * 3.14159265358979323846 / 180; return std::clamp<int64_t>(int64_t(std::floor((1 - std::asinh(std::tan(rad)) / 3.14159265358979323846) * .5 * n)), 0, int64_t(n) - 1); };
            result.tiles += uint64_t(x(p.east) - x(p.west) + 1) * uint64_t(y(p.south) - y(p.north) + 1) * layers;
        }
        result.raster_bytes = result.tiles * 131072ULL;
        if (!p.maximum_bytes || p.maximum_bytes > 8ULL * 1024 * 1024 * 1024 || result.raster_bytes > p.maximum_bytes) return invalid("Tile payload exceeds the output budget; reduce area or maximum zoom.");
        if (source_bytes > (UINT64_MAX - result.raster_bytes * 3 - 64 * 1024 * 1024) / 3) return invalid("Source exceeds the working-space budget.");
        result.working_bytes = result.raster_bytes * 3 + source_bytes * 3 + 64 * 1024 * 1024;
        if (!result.available_bytes || result.working_bytes > result.available_bytes) return invalid("Insufficient free storage for source processing and map staging.");
        result.valid = true;
        return result;
    }
    void update(tc::Phase phase, const std::string& stage)
    {
        std::lock_guard<std::mutex> lock(mutex);
        state.phase = phase;
        state.stage = stage;
    }
    void check()
    {
        if (cancelled || stopping) throw std::runtime_error("Cancelled");
    }
    bool process(const std::vector<std::string>& arguments, int source = -1, int lease = -1)
    {
        int descriptors[2];
        if (pipe2(descriptors, O_CLOEXEC) != 0) throw std::runtime_error("Cannot open producer output pipe.");
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, descriptors[0]);
        posix_spawn_file_actions_addclose(&actions, descriptors[1]);
        if (source >= 0) posix_spawn_file_actions_adddup2(&actions, source, 198);
        if (lease >= 0) posix_spawn_file_actions_adddup2(&actions, lease, 197);
        posix_spawnattr_t attributes;
        posix_spawnattr_init(&attributes);
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attributes, 0);
        std::vector<std::string> owned{options.producer.string()};
        owned.insert(owned.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        for (auto& value : owned) argv.push_back(value.data());
        argv.push_back(nullptr);
        pid_t pid = -1;
        const int started = posix_spawn(&pid, owned[0].c_str(), &actions, &attributes, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
        ::close(descriptors[1]);
        if (started)
        {
            ::close(descriptors[0]);
            throw std::runtime_error("Cannot start installed TMAP producer.");
        }
        std::string line, tail;
        bool exited = false, eof = false;
        int status = 0;
        auto interrupted = std::chrono::steady_clock::time_point{};
        while (!exited || !eof)
        {
            if ((cancelled || stopping) && !exited)
            {
                const auto now = std::chrono::steady_clock::now();
                if (interrupted == std::chrono::steady_clock::time_point{})
                {
                    kill(-pid, SIGINT);
                    interrupted = now;
                }
                else if (now - interrupted > std::chrono::seconds(5)) kill(-pid, SIGKILL);
                else if (now - interrupted > std::chrono::seconds(3)) kill(-pid, SIGTERM);
            }
            pollfd input{descriptors[0], POLLIN | POLLHUP, 0};
            if (!eof && poll(&input, 1, 100) > 0)
            {
                char chunk[1024];
                const auto bytes = ::read(descriptors[0], chunk, sizeof(chunk));
                if (bytes <= 0) eof = true;
                else
                    for (ssize_t i = 0; i < bytes; ++i)
                    {
                        if (chunk[i] == '\n')
                        {
                            if (!line.empty()) tail = line;
                            const auto colon = line.find(": ");
                            if (colon != std::string::npos)
                            {
                                unsigned long long done = 0, total = 0;
                                if (std::sscanf(line.c_str() + colon + 2, "%llu/%llu", &done, &total) == 2)
                                {
                                    std::lock_guard<std::mutex> lock(mutex);
                                    state.stage = line.substr(0, colon);
                                    state.completed = done;
                                    state.total = total;
                                    state.percent = total ? int(std::min<uint64_t>(99, done * 100 / total)) : -1;
                                }
                            }
                            line.clear();
                        }
                        else if (line.size() < 2048) line += chunk[i];
                    }
            }
            if (!exited)
            {
                const auto result = waitpid(pid, &status, WNOHANG);
                if (result == pid) exited = true;
                else if (result < 0 && errno != EINTR)
                {
                    exited = true;
                    status = 1 << 8;
                }
            }
        }
        ::close(descriptors[0]);
        check();
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) throw std::runtime_error(tail.empty() ? "TMAP producer failed." : tail);
        return true;
    }
    void build(const tc::Plan& p)
    {
        const auto checked = estimate(p);
        if (!checked.valid) throw std::runtime_error(checked.error);
        std::filesystem::create_directories(options.cache);
        TmapJobWorkspace workspace(options.storage);
        workspace.create();
        const auto work = workspace.path(), out = work / "output";
        UploadIo destination(options.storage);
        if (!destination.current()) throw std::runtime_error("Map storage is unavailable.");
        for (const auto* style : {"osm", "terrain", "satellite"})
        {
            if (std::string(style) == "terrain" && !p.terrain) continue;
            if (std::string(style) == "satellite" && !p.satellite) continue;
            const std::string final = "/maps/tmap/" + std::string(style) + '/' + output_filename(p.filename, style);
            const auto existing = destination.open(final.c_str(), UploadIo::Mode::Read);
            if (existing.handle) destination.close(existing.handle);
            if (existing.status != UploadIo::OpenStatus::Missing) throw std::runtime_error("Output already exists or is unavailable; choose a new filename.");
        }
        const int source = ::open(p.source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (source < 0) throw std::runtime_error("Cannot open selected PBF source.");
        struct SourceLease
        {
            int fd;
            ~SourceLease() { ::close(fd); }
        } source_lease{source};
        struct stat source_info
        {
        };
        if (fstat(source, &source_info) || !S_ISREG(source_info.st_mode) || source_info.st_nlink != 1 || source_info.st_size <= 0) throw std::runtime_error("PBF source must be a nonempty regular file.");
        std::filesystem::create_directories(out);
        const auto plan = work / "plan.json";
        {
            std::ofstream file(plan);
            file << plan_json(p, out, work / "sources");
            if (!file) throw std::runtime_error("Cannot persist TMAP production plan.");
        }
        update(tc::Phase::Building, "Importing PBF and rendering tiles");
        process({"build", plan.string()}, source, workspace.lease());
        check();
        struct stat after
        {
        };
        if (fstat(source, &after) || after.st_size != source_info.st_size || after.st_mtim.tv_sec != source_info.st_mtim.tv_sec || after.st_mtim.tv_nsec != source_info.st_mtim.tv_nsec) throw std::runtime_error("PBF changed during map creation.");
        std::vector<std::filesystem::path> files;
        unsigned maps = 0;
        uint64_t bytes = 0;
        update(tc::Phase::Verifying, "Checking completed TMAP files");
        for (const auto& entry : std::filesystem::recursive_directory_iterator(out))
        {
            check();
            if (entry.is_symlink()) throw std::runtime_error("Producer output contains a symbolic link.");
            if (!entry.is_regular_file()) continue;
            const auto relative = std::filesystem::relative(entry.path(), out).generic_string();
            // Center also emits authoring reports. Only runtime map payloads
            // and their font packages belong in device storage.
            if (relative.rfind("maps/", 0) == 0 && entry.path().extension() != ".tmap") continue;
            if (files.size() >= 4096) throw std::runtime_error("Producer output has too many files.");
            bytes += entry.file_size();
            if (bytes > p.maximum_bytes + 64 * 1024 * 1024) throw std::runtime_error("Completed output exceeds the map and font budget.");
            if (entry.path().extension() == ".tmap")
            {
                process({"verify", entry.path().string()}, -1, workspace.lease());
                ++maps;
            }
            files.push_back(entry.path());
        }
        if (!maps) throw std::runtime_error("Producer did not create a TMAP file.");
        if (!destination.current() || !destination.lock()) throw std::runtime_error("Map storage is busy or has changed.");
        update(tc::Phase::Installing, "Installing verified map and font files");
        struct Installed
        {
            std::string temporary, final;
            bool published = false;
        };
        std::vector<Installed> installed;
        try
        {
            std::array<uint8_t, 32768> buffer{};
            for (const auto& file : files)
            {
                check();
                const auto relative = std::filesystem::relative(file, out).generic_string();
                if (relative.rfind("maps/", 0) != 0 && relative.rfind("trailmate/packs/fonts/", 0) != 0) throw std::runtime_error("Unexpected producer output location.");
                const std::string final = '/' + relative, temporary = workspace.relative() + "/publish/" + relative;
                if (!destination.mkdirs(std::filesystem::path(final).parent_path().c_str())) throw std::runtime_error("Cannot prepare map output directory.");
                if (!destination.mkdirs(std::filesystem::path(temporary).parent_path().c_str())) throw std::runtime_error("Cannot prepare private map staging directory.");
                auto existing = destination.open(final.c_str(), UploadIo::Mode::Read);
                if (existing.handle)
                {
                    bool equal = relative.rfind("trailmate/packs/fonts/", 0) == 0;
                    uint64_t size = 0;
                    equal = equal && destination.size(existing.handle, size) && size == std::filesystem::file_size(file);
                    std::ifstream input(file, std::ios::binary);
                    std::array<char, 32768> prior{};
                    while (equal && input)
                    {
                        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
                        const auto n = input.gcount();
                        if (n && (destination.read(existing.handle, reinterpret_cast<uint8_t*>(prior.data()), n) != size_t(n) || std::memcmp(prior.data(), buffer.data(), n))) equal = false;
                    }
                    destination.close(existing.handle);
                    if (equal && input.eof()) continue;
                    throw std::runtime_error("Output already exists; choose a new filename.");
                }
                if (existing.status != UploadIo::OpenStatus::Missing) throw std::runtime_error("Cannot inspect map destination.");
                const auto opened = destination.open(temporary.c_str(), UploadIo::Mode::Exclusive);
                if (!opened.handle) throw std::runtime_error("Map staging file already exists or storage is unavailable.");
                installed.push_back({temporary, final});
                std::ifstream input(file, std::ios::binary);
                while (input)
                {
                    check();
                    input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
                    const auto n = input.gcount();
                    if (n && destination.write(opened.handle, buffer.data(), n) != size_t(n)) throw std::runtime_error("Map storage write failed.");
                }
                if (!input.eof() || !destination.sync(opened.handle) || !destination.close(opened.handle)) throw std::runtime_error("Map storage sync failed.");
            }
            // Fonts precede the maps that reference them. No existing files
            // are replaced; cancellation can roll back only these new files.
            std::stable_sort(installed.begin(), installed.end(), [](const auto& a, const auto& b)
                             { return a.final.rfind("/trailmate/", 0) == 0 && b.final.rfind("/trailmate/", 0) != 0; });
            for (auto& file : installed)
            {
                check();
                if (!destination.publish(file.temporary.c_str(), file.final.c_str())) throw std::runtime_error("Cannot publish completed TMAP output.");
                file.published = true;
            }
        }
        catch (...)
        {
            destination.close(nullptr);
            for (const auto& file : installed)
                if (destination.current()) destination.remove((file.published ? file.final : file.temporary).c_str());
            throw;
        }
        std::lock_guard<std::mutex> lock(mutex);
        state.output = (options.storage / "maps/tmap").string();
        state.percent = 100;
    }
    void run()
    {
        const auto recover = [&]
        {
            try
            {
                TmapJobWorkspace workspace(options.storage);
                if (!workspace.ready()) return false;
                workspace.recover();
                return true;
            }
            catch (const std::exception& error)
            {
                std::lock_guard<std::mutex> lock(mutex);
                state.error = error.what();
                return false;
            }
        };
        bool recovery_pending = !recover();
        scan();
        for (;;)
        {
            std::optional<tc::Plan> plan;
            {
                std::unique_lock<std::mutex> lock(mutex);
                const auto ready = [&] { return stopping || pending.has_value() || scan_requested; };
                if (recovery_pending) changed.wait_for(lock, std::chrono::seconds(2), ready);
                else changed.wait(lock, ready);
                if (stopping) return;
                plan = std::move(pending);
                pending.reset();
                scan_requested = false;
            }
            if (!plan)
            {
                if (recovery_pending) recovery_pending = !recover();
                scan();
                continue;
            }
            try
            {
                build(*plan);
                update(tc::Phase::Succeeded, "Map created and available in Map layers");
            }
            catch (const std::exception& error)
            {
                std::lock_guard<std::mutex> lock(mutex);
                state.phase = cancelled || stopping ? tc::Phase::Cancelled : tc::Phase::Failed;
                state.error = state.phase == tc::Phase::Cancelled ? "" : error.what();
                state.stage = state.phase == tc::Phase::Cancelled ? "Creation cancelled" : "Creation failed";
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                state.busy = false;
            }
            scan();
        }
    }
    TmapCreationOptions options;
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<tc::Source> inputs;
    uint64_t available = 0;
    uint32_t source_revision = 0;
    tc::Snapshot state;
    std::optional<tc::Plan> pending;
    bool scan_requested = false;
    std::atomic<bool> stopping{false}, cancelled{false};
    std::thread worker;
};
LinuxTmapCreation::LinuxTmapCreation(TmapCreationOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
LinuxTmapCreation::~LinuxTmapCreation() = default;
std::vector<tc::Source> LinuxTmapCreation::sources()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->scan_requested = true;
    impl_->changed.notify_all();
    return impl_->inputs;
}
tc::Estimate LinuxTmapCreation::estimate(const tc::Plan& plan) { return impl_->estimate(plan); }
bool LinuxTmapCreation::start(const tc::Plan& plan, std::string& error)
{
    const auto checked = impl_->estimate(plan);
    if (!checked.valid)
    {
        error = checked.error;
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state.busy)
    {
        error = "A map is already being created.";
        return false;
    }
    if (!impl_->state.producer_ready)
    {
        error = "The TMAP producer is not installed.";
        return false;
    }
    impl_->cancelled = false;
    impl_->state = {};
    impl_->state.source_directory = (impl_->options.storage / "map-sources").string();
    impl_->state.source_revision = impl_->source_revision;
    impl_->state.producer_ready = true;
    impl_->state.busy = true;
    impl_->state.phase = tc::Phase::Preparing;
    impl_->pending = plan;
    impl_->changed.notify_all();
    return true;
}
tc::Snapshot LinuxTmapCreation::snapshot()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state;
}
void LinuxTmapCreation::cancel() { impl_->cancelled = true; }
void configure_tmap_creation(bool enabled)
{
    static std::unique_ptr<LinuxTmapCreation> service;
    tc::bind(nullptr);
    service.reset();
    if (!enabled) return;
    const auto paths = resolve_paths();
    TmapCreationOptions options{paths.sd_root, paths.cache_root};
    if (const auto* value = std::getenv("TRAIL_MATE_TMAP_PRODUCER")) options.producer = value;
    service = std::make_unique<LinuxTmapCreation>(std::move(options));
    tc::bind(service.get());
}
} // namespace platform::linux_runtime
