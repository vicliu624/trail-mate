#include "platform/linux/waypoint_source.h"
#include "platform/linux/waypoint_store.h"
#include "waypoint/record_codec.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#if defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace platform::linux_runtime
{
namespace
{
using ::waypoint::Result;
using State = ::ui::waypoint::CommandState;
struct Projection final : ::waypoint::IStore
{
    int fd = -1;
    bool available = false;
    std::array<Result, ::waypoint::kMaxWaypoints> status{};
    ~Projection()
    {
#if defined(__linux__)
        if (fd >= 0) ::close(fd);
#endif
    }
    uint16_t slotCount() const override { return ::waypoint::kMaxWaypoints; }
    Result read(uint16_t slot, ::waypoint::Record& out) override
    {
        out = {};
        if (slot >= slotCount()) return Result::Invalid;
        if (!available) return Result::IoError;
        if (status[slot] != Result::Ok) return status[slot];
#if defined(__linux__)
        uint8_t bytes[::waypoint::kEncodedBytes];
        ::waypoint::Slot decoded;
        if (::pread(fd, bytes, sizeof(bytes), off_t(slot) * sizeof(bytes)) != sizeof(bytes)) return Result::IoError;
        if (!::waypoint::decode(bytes, sizeof(bytes), decoded)) return Result::Corrupt;
        if (decoded.state == ::waypoint::SlotState::Active) out = decoded.record;
        return Result::Ok;
#else
        return Result::IoError;
#endif
    }
    Result create(const ::waypoint::Record&, uint32_t& id) override
    {
        id = 0;
        return Result::IoError;
    }
    Result update(const ::waypoint::Record&) override { return Result::IoError; }
    Result remove(uint32_t) override { return Result::IoError; }
};
std::shared_ptr<Projection> project(LinuxWaypointStore& store)
try
{
#if defined(__linux__)
    auto view = std::make_shared<Projection>();
    view->fd = ::memfd_create("trailmate-waypoint-view", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (view->fd < 0 || ::ftruncate(view->fd, ::waypoint::kMaxWaypoints * ::waypoint::kEncodedBytes) != 0) return {};
    for (uint16_t index = 0; index < view->slotCount(); ++index)
    {
        ::waypoint::Slot slot;
        slot.generation = 1;
        view->status[index] = store.read(index, slot.record);
        if (view->status[index] != Result::Ok) continue;
        slot.state = slot.record.id ? ::waypoint::SlotState::Active : ::waypoint::SlotState::Empty;
        uint8_t bytes[::waypoint::kEncodedBytes];
        if (!::waypoint::encode(slot, bytes, sizeof(bytes)) ||
            ::pwrite(view->fd, bytes, sizeof(bytes), off_t(index) * sizeof(bytes)) != sizeof(bytes)) return {};
    }
    if (::fcntl(view->fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) != 0) return {};
    view->available = true;
    return view;
#else
    (void)store;
    return {};
#endif
}
catch (...)
{
    return {};
}
} // namespace
struct LinuxWaypointSource::Impl
{
    std::filesystem::path path;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::shared_ptr<Projection> published;
    ::waypoint::Record pending;
    ::ui::waypoint::CommandResult result;
    bool deleting = false, has_command = false, stopping = false;
    std::atomic<bool> active{true};
    std::thread worker;
    explicit Impl(std::filesystem::path value) : path(std::move(value)), worker([this]
                                                                                { run(); }) {}
    ~Impl() { stop(); }
    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            wake.notify_all();
        }
        if (worker.joinable()) worker.join();
        active = false;
        std::atomic_store(&published, std::shared_ptr<Projection>{});
    }
    ::ui::UiActionResult enqueue(const ::waypoint::Record& record, bool remove)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto view = std::atomic_load(&published);
        if (stopping || !active || !view || !view->available) return ::ui::UiActionResult::fail(::ui::UiActionFailure::NotReady);
        if (result.state == State::Pending) return ::ui::UiActionResult::fail(::ui::UiActionFailure::Busy);
        if (!++result.sequence) ++result.sequence;
        result.state = State::Pending;
        result.result = Result::Ok;
        result.id = 0;
        pending = record;
        deleting = remove;
        has_command = true;
        wake.notify_all();
        return ::ui::UiActionResult::success();
    }
    void run() noexcept
    {
        ::ui::waypoint::CommandResult terminal;
        bool committed = false;
        try
        {
            LinuxWaypointStore store(path);
            bool opened = false;
            auto next_refresh = std::chrono::steady_clock::time_point::min();
            for (;;)
            {
                ::waypoint::Record record;
                bool have = false, remove = false;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait_for(lock, std::chrono::milliseconds(100), [&]
                                  { return stopping || has_command; });
                    if (stopping && !has_command) break;
                    if (has_command)
                    {
                        record = pending;
                        pending = {};
                        remove = deleting;
                        terminal = result;
                        have = true;
                        has_command = false;
                    }
                }
                committed = false;
                const auto now = std::chrono::steady_clock::now();
                if (!have && now < next_refresh) continue;
                const auto health = store.begin();
                opened = health == Result::Ok;
                if (have)
                {
                    terminal.result = !opened ? health : remove  ? store.remove(record.id)
                                                     : record.id ? store.update(record)
                                                                 : store.create(record, terminal.id);
                    if (terminal.result == Result::Ok && record.id) terminal.id = record.id;
                    terminal.state = terminal.result == Result::Ok ? State::Succeeded : State::Failed;
                    committed = terminal.state == State::Succeeded;
                }
                auto view = opened ? project(store) : std::shared_ptr<Projection>{};
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    std::atomic_store(&published, std::move(view));
                    if (have) result = terminal;
                }
                committed = false;
                next_refresh = now + std::chrono::seconds(1);
            }
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(mutex);
            // A projection allocation failure cannot reverse a completed commit.
            if (result.state == State::Pending)
            {
                if (committed) result = terminal;
                else
                {
                    result.state = State::Failed;
                    result.result = Result::IoError;
                    result.id = 0;
                }
            }
            std::atomic_store(&published, std::shared_ptr<Projection>{});
            active = false;
        }
    }
};
LinuxWaypointSource::LinuxWaypointSource(std::filesystem::path path) : impl_(std::make_unique<Impl>(std::move(path))) {}
LinuxWaypointSource::~LinuxWaypointSource() = default;
void LinuxWaypointSource::shutdown() { impl_->stop(); }
bool LinuxWaypointSource::ready() const
{
    const auto view = std::atomic_load(&impl_->published);
    return impl_->active && view && view->available;
}
Result LinuxWaypointSource::page(uint32_t after, uint8_t capacity, ::waypoint::Page& out)
{
    out = {};
    auto view = std::atomic_load(&impl_->published);
    return impl_->active && view ? ::waypoint::query(*view, after, capacity, out) : Result::IoError;
}
Result LinuxWaypointSource::detail(uint32_t id, ::waypoint::Record& out)
{
    out = {};
    auto view = std::atomic_load(&impl_->published);
    return impl_->active && view ? ::waypoint::find(*view, id, out) : Result::IoError;
}
::ui::waypoint::CommandResult LinuxWaypointSource::commandResult() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->result;
}
::ui::UiActionResult LinuxWaypointSource::save(const ::waypoint::Record& record)
{
    auto validation = record;
    if (!validation.id) validation.id = 1;
    if (!::waypoint::valid(validation)) return ::ui::UiActionResult::fail(::ui::UiActionFailure::InvalidInput);
    return impl_->enqueue(record, false);
}
::ui::UiActionResult LinuxWaypointSource::remove(uint32_t id)
{
    if (!id) return ::ui::UiActionResult::fail(::ui::UiActionFailure::InvalidInput);
    ::waypoint::Record record;
    record.id = id;
    return impl_->enqueue(record, true);
}
} // namespace platform::linux_runtime
