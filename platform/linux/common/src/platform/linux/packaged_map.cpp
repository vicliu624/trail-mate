#include "platform/linux/packaged_map.h"
#include "platform/linux/tmap_storage.h"
#include "ui_map_runtime/map_poi/poi_types.h"
#include "ui_map_runtime/map_tiles/native_pixel_buffer.h"
#include "ui_map_runtime/map_tiles/tmap_map_tile_source.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <thread>

namespace platform::linux_runtime
{
namespace
{
namespace tiles = ::ui::map_tiles;
std::atomic<size_t> pixel_bytes{0};
uint32_t milliseconds()
{
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct Stamp
{
    uint64_t device = 0, inode = 0, bytes = 0, modified = 0, changed = 0;
    bool operator==(const Stamp& other) const { return device == other.device && inode == other.inode && bytes == other.bytes && modified == other.modified && changed == other.changed; }
};
Stamp stamp(const std::filesystem::path& path, bool root = false)
{
    struct stat value
    {
    };
    if ((root ? ::stat(path.c_str(), &value) : ::lstat(path.c_str(), &value)) != 0) return {};
    if (S_ISDIR(value.st_mode)) return {uint64_t(value.st_dev), uint64_t(value.st_ino), 0, 0, 0};
    return {uint64_t(value.st_dev), uint64_t(value.st_ino), uint64_t(value.st_size),
            uint64_t(value.st_mtim.tv_sec) * 1000000000 + uint64_t(value.st_mtim.tv_nsec),
            uint64_t(value.st_ctim.tv_sec) * 1000000000 + uint64_t(value.st_ctim.tv_nsec)};
}
class Storage final : public tiles::TmapStorage
{
  public:
    Storage(std::filesystem::path root, uint32_t session) : root_(std::move(root)), file_(root_), catalog_(root_), media_(root_), session_(session)
    {
        for (const auto* path : {"", "/maps", "/maps/tmap", "/maps/tmap/osm", "/maps/tmap/terrain", "/maps/tmap/satellite"})
        {
            watched_[path] = stamp(physical(path), !path[0]);
            if (notify_.fd >= 0)
                (void)inotify_add_watch(notify_.fd, physical(path).c_str(),
                                        IN_ONLYDIR | (path[0] ? IN_DONT_FOLLOW : 0) | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF | IN_ATTRIB);
        }
    }
    void* allocate(size_t bytes, size_t alignment) override
    {
        void* output = nullptr;
        return posix_memalign(&output, std::max(alignment, sizeof(void*)), bytes) == 0 ? output : nullptr;
    }
    void release(void* pointer) override { std::free(pointer); }
    uint32_t session() const override { return session_; }
    uint32_t nowMs() const override { return milliseconds(); }
    uint64_t size() const override { return file_.size(); }
    tmap::Status readAt(uint64_t offset, uint8_t* data, size_t bytes) override { return file_.readAt(offset, data, bytes); }
    bool valid() const { return media_.current() && notify_.fd >= 0; }
    bool changed() const
    {
        if (notify_.fd < 0) return true;
        alignas(inotify_event) uint8_t events[4096];
        const auto bytes = ::read(notify_.fd, events, sizeof(events));
        if (bytes < 0 && errno != EAGAIN && errno != EINTR) return true;
        for (size_t at = 0; bytes > 0 && at + sizeof(inotify_event) <= size_t(bytes);)
        {
            const auto* event = reinterpret_cast<const inotify_event*>(events + at);
            if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED | IN_Q_OVERFLOW | IN_ISDIR)) return true;
            const auto length = event->len ? strnlen(event->name, event->len) : 0;
            if (length >= 5)
            {
                const auto* suffix = event->name + length - 5;
                if (suffix[0] == '.' && (suffix[1] | 32) == 't' && (suffix[2] | 32) == 'm' && (suffix[3] | 32) == 'a' && (suffix[4] | 32) == 'p') return true;
            }
            at += sizeof(inotify_event) + event->len;
        }
        for (const auto& item : watched_)
            if (!(item.second == stamp(physical(item.first), item.first.empty()))) return true;
        return false;
    }
    tmap::Status nextPackage(char* path, size_t capacity) override
    {
        if (!valid()) return tmap::Status::IoError;
        std::string logical;
        const auto status = catalog_.next(logical);
        if (status != tmap::Status::Ok) return status;
        if (logical.size() >= capacity) return tmap::Status::Invalid;
        std::memcpy(path, logical.c_str(), logical.size() + 1);
        return tmap::Status::Ok;
    }
    tmap::Status openPackage(const char* path) override
    {
        if (!path || !valid()) return tmap::Status::IoError;
        if (active_ == path && file_.current()) return tmap::Status::Ok;
        if (!file_.open(path)) return tmap::Status::IoError;
        active_ = path;
        if (!watched_.count(path))
        {
            watched_[path] = stamp(physical(path));
            if (!workspace_) workspace_ = std::make_unique<tmap::Workspace>();
            tmap::Reader reader(*workspace_);
            auto status = reader.open(file_);
            if (status != tmap::Status::Ok) return status;
            std::array<uint32_t, 13> covered{};
            status = reader.layerCoverage(covered);
            if (status != tmap::Status::Ok) return status;
            for (size_t index = 0; index < covered.size(); ++index) coverage[index] |= covered[index];
        }
        return tmap::Status::Ok;
    }
    void closePackage() override
    {
        file_.close();
        active_.clear();
    }
    void resetEnumeration() override { catalog_ = TmapCatalog(root_); }
    std::array<uint32_t, 13> coverage{};

  private:
    std::filesystem::path physical(const std::string& path) const { return path.empty() ? root_ : root_ / path.substr(1); }
    std::filesystem::path root_;
    TmapFile file_;
    TmapCatalog catalog_;
    UploadIo media_;
    uint32_t session_;
    std::string active_;
    std::map<std::string, Stamp> watched_;
    std::unique_ptr<tmap::Workspace> workspace_;
    struct Notify
    {
        int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        ~Notify()
        {
            if (fd >= 0) ::close(fd);
        }
    } notify_;
};
uint64_t key(const tiles::MapTileRef& ref)
{
    return uint64_t(ref.layer) << 56 | uint64_t(ref.z) << 48 | uint64_t(ref.x) << 24 | ref.y;
}
bool valid(const tiles::MapTileRef& ref)
{
    return unsigned(ref.layer) <= unsigned(tiles::MapTileLayer::Poi) && ref.z <= 18 && ref.x < (uint32_t{1} << ref.z) && ref.y < (uint32_t{1} << ref.z);
}
} // namespace
PackagedMapPixels::~PackagedMapPixels()
{
    if (data) tiles::NativePixelBuffer::release(data);
}
size_t PackagedMapSession::pixelBytes() { return pixel_bytes.load(); }

struct PackagedMapSession::Impl
{
    explicit Impl(std::filesystem::path root, size_t limit) : root(std::move(root)), pixel_limit(std::min(limit, PackagedMapSession::kPixelBudget)), worker([this]
                                                                                                                                                            { run(); }) {}
    ~Impl()
    {
        stopping = true;
        ++generation;
        changed.notify_one();
        if (worker.joinable()) worker.join();
    }
    struct Entry
    {
        tiles::MapTileRef ref;
        std::shared_ptr<const PackagedMapTile> result;
        uint64_t age = 0;
        uint32_t retry = 0;
        bool queued = false;
    };
    std::shared_ptr<const PackagedMapTile> request(const tiles::MapTileRef& ref)
    {
        if (!valid(ref))
        {
            auto result = std::make_shared<PackagedMapTile>();
            result->ref = ref;
            result->result.status = tiles::MapTileReadStatus::Invalid;
            return result;
        }
        std::lock_guard<std::mutex> lock(mutex);
        if (metadata.mode != PackagedMapMode::Tmap) return nullptr;
        const auto identity = key(ref);
        auto found = entries.find(identity);
        if (found == entries.end())
        {
            if (entries.size() == kEntries)
            {
                auto oldest = entries.end();
                for (auto it = entries.begin(); it != entries.end(); ++it)
                    if (!it->second.queued && (oldest == entries.end() || it->second.age < oldest->second.age)) oldest = it;
                if (oldest == entries.end()) return nullptr;
                entries.erase(oldest);
            }
            found = entries.emplace(identity, Entry{ref, {}, 0, 0, false}).first;
        }
        auto& entry = found->second;
        entry.age = ++age;
        if (entry.result && entry.result->result.status != tiles::MapTileReadStatus::RetryLater) return entry.result;
        if (!entry.queued && (!entry.retry || int32_t(milliseconds() - entry.retry) >= 0))
        {
            entry.result.reset();
            entry.queued = true;
            queue.push_back(identity);
            changed.notify_one();
        }
        return nullptr;
    }
    void cancel()
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++generation;
        queue.clear();
        entries.clear();
    }
    std::shared_ptr<PackagedMapTile> read(tiles::TmapMapTileSource& source, const tiles::MapTileRef& ref, uint32_t expected)
    {
        auto output = std::make_shared<PackagedMapTile>();
        output->ref = ref;
        if (ref.layer == tiles::MapTileLayer::Poi)
        {
            constexpr size_t bytes = sizeof(ui::map_poi::TileHeader) + ui::map_poi::TileHeader::kMaxRecords * sizeof(ui::map_poi::Record);
            output->annotations.resize((bytes + 7) / 8);
            while (!stopping && expected == generation)
            {
                output->result = source.read(ref, reinterpret_cast<uint8_t*>(output->annotations.data()), bytes);
                if (output->result.status == tiles::MapTileReadStatus::Ready)
                {
                    if (!ui::map_poi::validPayload(output->data(), output->result.size))
                    {
                        output->result.status = tiles::MapTileReadStatus::Invalid;
                        break;
                    }
                    const auto* header = reinterpret_cast<const ui::map_poi::TileHeader*>(output->data());
                    if (!header->partial) break;
                }
                else if (output->result.status != tiles::MapTileReadStatus::RetryLater) break;
                std::this_thread::yield();
            }
            if (output->result.status != tiles::MapTileReadStatus::Ready) output->annotations.clear();
            return output;
        }
        const auto info = source.lookup(ref);
        if (info.status != tiles::MapTileStatus::Available)
        {
            output->result.status = info.status == tiles::MapTileStatus::Missing ? tiles::MapTileReadStatus::Missing : tiles::MapTileReadStatus::Error;
            return output;
        }
        auto* pixels = tiles::NativePixelBuffer::create(
            info.size, pixel_bytes, pixel_limit,
            [](size_t alignment, size_t bytes) -> void*
            { void* result = nullptr; return posix_memalign(&result, alignment, bytes) == 0 ? result : nullptr; },
            std::free);
        if (!pixels)
        {
            output->result.status = tiles::MapTileReadStatus::RetryLater;
            output->result.error = -12;
            return output;
        }
        try
        {
            output->pixels = std::make_shared<PackagedMapPixels>(pixels);
        }
        catch (...)
        {
            tiles::NativePixelBuffer::release(pixels);
            throw;
        }
        output->result = source.read(ref, pixels, info.size);
        if (output->result.status != tiles::MapTileReadStatus::Ready) output->pixels.reset();
        return output;
    }
    void run()
    {
        std::unique_ptr<Storage> storage;
        std::unique_ptr<tiles::TmapMapTileSource> source;
        uint32_t prepared = 0;
        uint32_t monitored = 0;
        while (!stopping)
        {
            try
            {
                const auto now = milliseconds();
                const bool check = !monitored || uint32_t(now - monitored) >= 500;
                const bool refresh = !storage || (check && storage->changed());
                if (check) monitored = now;
                if (refresh)
                {
                    source.reset();
                    storage.reset();
                    uint32_t session;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        ++generation;
                        session = ++metadata.session;
                        metadata.mode = PackagedMapMode::Pending;
                        metadata.coverage.fill(0);
                        entries.clear();
                        queue.clear();
                    }
                    storage = std::make_unique<Storage>(root, session);
                    source = std::make_unique<tiles::TmapMapTileSource>(*storage);
                    prepared = 0;
                    auto status = storage->valid() ? tmap::Status::More : tmap::Status::IoError;
                    while (!stopping && (status == tmap::Status::More || status == tmap::Status::Busy))
                    {
                        status = source->prepare();
                        std::this_thread::yield();
                    }
                    std::lock_guard<std::mutex> lock(mutex);
                    metadata.mode = status != tmap::Status::Ok ? PackagedMapMode::Error : source->packageCount() ? PackagedMapMode::Tmap
                                                                                                                 : PackagedMapMode::Legacy;
                    metadata.coverage = storage->coverage;
                }
                uint64_t identity = 0;
                tiles::MapTileRef ref;
                uint32_t expected;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    if (queue.empty()) changed.wait_for(lock, std::chrono::milliseconds(500), [this]
                                                        { return stopping || !queue.empty(); });
                    if (stopping) break;
                    if (queue.empty()) continue;
                    identity = queue.front();
                    queue.erase(queue.begin());
                    const auto entry = entries.find(identity);
                    if (entry == entries.end()) continue;
                    ref = entry->second.ref;
                    expected = generation;
                }
                if (prepared != expected)
                {
                    source->cancelPendingAnnotations();
                    prepared = expected;
                }
                auto result = read(*source, ref, expected);
                std::lock_guard<std::mutex> lock(mutex);
                const auto entry = entries.find(identity);
                if (expected == generation && entry != entries.end())
                {
                    entry->second.queued = false;
                    entry->second.retry = milliseconds() + 250;
                    entry->second.result = std::move(result);
                }
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(mutex);
                metadata.mode = PackagedMapMode::Error;
                ++metadata.session;
                queue.clear();
                entries.clear();
                // Retry after a filesystem change. A failed storage allocation
                // is also bounded, even before a watch set could be created.
                if (!storage) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
    static constexpr size_t kEntries = 192;
    std::filesystem::path root;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::map<uint64_t, Entry> entries;
    std::vector<uint64_t> queue;
    PackagedMapSnapshot metadata;
    std::atomic_bool stopping{false};
    std::atomic<uint32_t> generation{1};
    uint64_t age = 0;
    size_t pixel_limit;
    std::thread worker;
};
PackagedMapSession::PackagedMapSession(std::filesystem::path root, size_t limit) : impl_(std::make_unique<Impl>(std::move(root), limit)) {}
PackagedMapSession::~PackagedMapSession() = default;
PackagedMapSnapshot PackagedMapSession::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->metadata;
}
std::shared_ptr<const PackagedMapTile> PackagedMapSession::request(const tiles::MapTileRef& ref) { return impl_->request(ref); }
void PackagedMapSession::cancel() { impl_->cancel(); }
} // namespace platform::linux_runtime
