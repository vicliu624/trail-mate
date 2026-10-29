#include "platform/linux/backlight.h"
#include "platform/linux/process_runner.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>

namespace platform::linux_runtime
{
namespace
{
bool read_number(const std::filesystem::path& path, uint32_t& output)
{
    std::ifstream file(path);
    uint64_t number = 0;
    if (!(file >> number) || number > std::numeric_limits<int32_t>::max()) return false;
    file >> std::ws;
    if (!file.eof()) return false;
    output = static_cast<uint32_t>(number);
    return true;
}
bool probe(const std::filesystem::path& device, BacklightSnapshot& state)
{
    uint32_t maximum = 0, brightness = 0;
    if (!read_number(device / "max_brightness", maximum) || !maximum ||
        !read_number(device / "brightness", brightness) || brightness > maximum) return false;
    uint32_t actual = 0;
    if (read_number(device / "actual_brightness", actual) && actual <= maximum) brightness = actual;
    state.available = true;
    state.native_maximum = maximum;
    state.level = static_cast<uint8_t>((uint64_t(brightness) * 255 + maximum / 2) / maximum);
    return true;
}
std::filesystem::path select(const BacklightOptions& options, BacklightSnapshot& state)
{
    if (!options.device.empty()) return probe(options.device, state) ? options.device : std::filesystem::path{};
    std::error_code error;
    std::filesystem::path selected;
    unsigned inspected = 0;
    for (std::filesystem::directory_iterator iterator(options.root, error), end; !error && iterator != end; iterator.increment(error))
    {
        BacklightSnapshot candidate;
        if (++inspected > 64 || (probe(iterator->path(), candidate) && !selected.empty()))
        {
            state = {};
            state.selection_required = true;
            return {};
        }
        if (candidate.available)
        {
            selected = iterator->path();
            state = candidate;
        }
    }
    if (error)
    {
        state = {};
        return {};
    }
    return selected;
}
bool write_level(const BacklightOptions& options, const std::filesystem::path& device,
                 uint32_t native, const std::atomic_bool& cancel)
{
    {
        std::ofstream file(device / "brightness");
        if (file)
        {
            file << native << '\n';
            file.flush();
            if (file) return true;
        }
    }
    // logind validates active-session ownership and performs the privileged
    // sysfs write. Only actual class nodes may use this fallback, never fixtures.
    if (options.root != "/sys/class/backlight" || device.parent_path() != options.root) return false;
    return run_process({"busctl", "call", "org.freedesktop.login1",
                        "/org/freedesktop/login1/session/auto", "org.freedesktop.login1.Session",
                        "SetBrightness", "ssu", "backlight", device.filename().string(), std::to_string(native)},
                       std::chrono::seconds(2), &cancel)
        .ok();
}
std::mutex global_mutex;
BacklightOptions configured_options;
bool configured = false, stopped = false;
std::unique_ptr<BacklightRuntime> runtime;
} // namespace
struct BacklightRuntime::Impl
{
    explicit Impl(BacklightOptions value) : options(std::move(value)), worker([this]
                                                                              { run(); }) {}
    ~Impl()
    {
        stopping = true;
        wake.notify_all();
        if (worker.joinable()) worker.join();
    }
    void run()
    {
        if (options.backend == "wayland_gamma")
        {
            run_gamma();
            return;
        }
        bool failed = false;
        while (!stopping)
        {
            BacklightSnapshot next;
            const auto target = select(options, next);
            const auto level = pending.exchange(-1);
            if (level >= 0)
            {
                const auto native = (uint64_t(level) * next.native_maximum + 127) / 255;
                failed = target.empty() || !write_level(options, target, static_cast<uint32_t>(native), stopping);
                if (!failed && !probe(target, next)) failed = true;
            }
            next.failed = failed;
            {
                std::lock_guard<std::mutex> lock(mutex);
                published = next;
            }
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_for(lock, std::chrono::seconds(1), [this]
                          { return stopping || pending >= 0; });
        }
    }
    void run_gamma()
    {
        GammaControl control;
        bool connected = false;
        int applied = -1;
        while (!stopping)
        {
            BacklightSnapshot next;
            auto status = GammaControlStatus::Ready;
            const bool reconnecting = !connected;
            if (!connected)
            {
                status = control.connect(options.gamma, stopping);
                connected = status == GammaControlStatus::Ready;
            }
            else if (!control.healthy(stopping))
            {
                connected = false;
                status = GammaControlStatus::Failed;
            }
            next.selection_required = status == GammaControlStatus::SelectionRequired;
            next.failed = status == GammaControlStatus::Failed;
            if (connected)
            {
                const int requested = pending.exchange(-1);
                // Reapply only a previously requested attenuation after reconnect.
                // Acquiring an initial control must not replace the original LUT.
                const int desired = requested >= 0 ? requested : applied;
                if (desired >= 0 && (requested >= 0 || reconnecting) &&
                    !control.apply(static_cast<uint8_t>(desired), stopping))
                {
                    connected = false;
                    next.failed = true;
                    if (requested >= 0)
                    {
                        int empty = -1;
                        pending.compare_exchange_strong(empty, requested);
                    }
                }
                else
                {
                    applied = desired;
                    next.available = true;
                    next.native_maximum = 255;
                    next.level = static_cast<uint8_t>(applied < 0 ? 255 : applied);
                }
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                published = next;
            }
            std::unique_lock<std::mutex> lock(mutex);
            // Failed connections retry at a bounded rate even with a pending
            // request. No UI-thread connection, shell command or busy loop.
            if (connected)
                wake.wait_for(lock, std::chrono::seconds(1), [this]
                              { return stopping || pending >= 0; });
            else
                wake.wait_for(lock, std::chrono::seconds(1), [this]
                              { return stopping.load(); });
        }
        control.close();
    }
    BacklightOptions options;
    mutable std::mutex mutex;
    BacklightSnapshot published;
    std::condition_variable wake;
    std::atomic_bool stopping{false};
    std::atomic<int> pending{-1};
    std::thread worker;
};
BacklightRuntime::BacklightRuntime(BacklightOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
BacklightRuntime::~BacklightRuntime() = default;
BacklightSnapshot BacklightRuntime::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->published;
}
bool BacklightRuntime::request(uint8_t level)
{
    if (impl_->stopping) return false;
    impl_->pending = level;
    impl_->wake.notify_all();
    return true;
}
bool configure_backlight(const BacklightOptions& options)
{
    if (options.backend != "sysfs" && options.backend != "wayland_gamma") return false;
    if ((!options.gamma.socket.empty() && !options.gamma.socket.is_absolute()) ||
        options.gamma.output.size() > 255 || options.gamma.output.find_first_of("\r\n\t\0", 0, 4) != std::string::npos ||
        (options.backend == "sysfs" && (!options.gamma.socket.empty() || !options.gamma.output.empty()))) return false;
    if (!options.root.is_absolute() || (!options.device.empty() && !options.device.is_absolute())) return false;
    std::lock_guard<std::mutex> lock(global_mutex);
    if (configured || runtime || stopped) return false;
    configured_options = options;
    configured = true;
    return true;
}
void prepare_backlight()
{
    std::lock_guard<std::mutex> lock(global_mutex);
    if (!runtime && !stopped) runtime = std::make_unique<BacklightRuntime>(configured_options);
}
BacklightSnapshot backlight_snapshot()
{
    std::lock_guard<std::mutex> lock(global_mutex);
    return runtime ? runtime->snapshot() : BacklightSnapshot{};
}
bool request_backlight(uint8_t level)
{
    std::lock_guard<std::mutex> lock(global_mutex);
    return runtime && runtime->request(level);
}
void shutdown_backlight()
{
    std::unique_ptr<BacklightRuntime> owned;
    {
        std::lock_guard<std::mutex> lock(global_mutex);
        stopped = true;
        owned = std::move(runtime);
    }
}
} // namespace platform::linux_runtime
