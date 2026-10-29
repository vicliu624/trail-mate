#include "platform/linux/keyboard_backlight.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace platform::linux_runtime
{
namespace
{
bool number(const std::filesystem::path& path, uint64_t& value)
{
    std::ifstream file(path);
    file >> std::ws;
    if (file.peek() == '-') return false;
    if (!(file >> value)) return false;
    file >> std::ws;
    return file.eof();
}
bool write(const std::filesystem::path& path, uint64_t value)
{
    std::ofstream file(path);
    file << value << '\n';
    file.flush();
    return bool(file);
}
std::filesystem::path controller(const KeyboardBacklightOptions& options, BacklightSnapshot& snapshot)
{
    std::error_code error;
    const auto wanted = std::filesystem::weakly_canonical(options.device_tree_root / options.of_node.relative_path(), error);
    if (error) return {};
    std::filesystem::path selected;
    unsigned count = 0;
    for (std::filesystem::directory_iterator it(options.root, error), end; !error && it != end; it.increment(error))
    {
        if (++count > 64)
        {
            snapshot.selection_required = true;
            return {};
        }
        std::error_code probe_error;
        const auto identity = std::filesystem::canonical(it->path() / "device/of_node", probe_error);
        if (probe_error || identity != wanted) continue;
        if (!selected.empty())
        {
            snapshot.selection_required = true;
            return {};
        }
        selected = it->path();
    }
    return error ? std::filesystem::path{} : selected;
}
bool probe(const KeyboardBacklightOptions& options, const std::filesystem::path& channel,
           BacklightSnapshot& snapshot, uint64_t& maximum)
{
    uint64_t period = 0, duty = 0, enabled = 0;
    std::string polarity;
    std::ifstream polarity_file(channel / "polarity");
    polarity_file >> polarity;
    if (polarity != "normal" || !number(channel / "period", period) || !period || period > 1000000000ULL ||
        !number(channel / "duty_cycle", duty) || duty > period || !number(channel / "enable", enabled) || enabled > 1) return false;
    maximum = period * options.max_duty_permille / 1000;
    if (!maximum) return false;
    snapshot.available = true;
    snapshot.native_maximum = uint32_t(maximum);
    snapshot.level = enabled ? uint8_t(std::min<uint64_t>(255, (duty * 255 + maximum / 2) / maximum)) : 0;
    return true;
}
std::mutex global_mutex;
KeyboardBacklightOptions configured_options;
bool configured = false, stopped = false;
std::unique_ptr<KeyboardBacklightRuntime> runtime;
} // namespace
struct KeyboardBacklightRuntime::Impl
{
    explicit Impl(KeyboardBacklightOptions value) : options(std::move(value)), worker([this]
                                                                                      { run(); }) {}
    ~Impl()
    {
        stopping = true;
        wake.notify_all();
        if (worker.joinable()) worker.join();
    }
    void run()
    {
        while (!stopping)
        {
            BacklightSnapshot next;
            if (!options.of_node.empty())
            {
                const auto chip = controller(options, next);
                if (!chip.empty())
                {
                    uint64_t channels = 0, maximum = 0;
                    const auto channel = chip / ("pwm" + std::to_string(options.channel));
                    std::error_code error;
                    if (number(chip / "npwm", channels) && options.channel < channels)
                    {
                        if (!std::filesystem::is_directory(channel, error) && options.export_channel)
                        {
                            const bool exported = write(chip / "export", options.channel);
                            for (unsigned wait = 0; exported && !stopping && wait < 20 && !std::filesystem::is_directory(channel, error); ++wait)
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                        uint64_t period = 0, enabled = 0;
                        if (options.export_channel && number(channel / "period", period) && !period &&
                            number(channel / "enable", enabled) && !enabled)
                            next.failed = !write(channel / "period", options.initial_period_ns);
                        if (!next.failed && probe(options, channel, next, maximum))
                        {
                            const int desired = pending.load();
                            if (desired >= 0)
                            {
                                const uint64_t duty = (uint64_t(desired) * maximum + 127) / 255;
                                next.failed = !write(channel / "duty_cycle", duty) || !write(channel / "enable", desired ? 1 : 0);
                                if (!next.failed)
                                {
                                    int expected = desired;
                                    pending.compare_exchange_strong(expected, -1);
                                    next.available = false;
                                    const auto expected_level = desired ? uint8_t(std::min<uint64_t>(255, (duty * 255 + maximum / 2) / maximum)) : 0;
                                    if (!probe(options, channel, next, maximum) || next.level != expected_level) next.failed = true;
                                }
                            }
                        }
                    }
                }
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                published = next;
            }
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_for(lock, std::chrono::seconds(1), [this]
                          { return stopping || pending >= 0; });
            // Failed/missing hardware must not turn a retained request into a busy loop.
            if (pending >= 0 && (published.failed || !published.available))
                wake.wait_for(lock, std::chrono::milliseconds(250), [this]
                              { return stopping.load(); });
        }
    }
    KeyboardBacklightOptions options;
    mutable std::mutex mutex;
    BacklightSnapshot published;
    std::condition_variable wake;
    std::atomic_bool stopping{false};
    std::atomic<int> pending{-1};
    std::thread worker;
};
KeyboardBacklightRuntime::KeyboardBacklightRuntime(KeyboardBacklightOptions options)
{
    if (!valid_keyboard_backlight_options(options)) throw std::invalid_argument("Invalid keyboard PWM configuration");
    impl_ = std::make_unique<Impl>(std::move(options));
}
KeyboardBacklightRuntime::~KeyboardBacklightRuntime() = default;
BacklightSnapshot KeyboardBacklightRuntime::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->published;
}
bool KeyboardBacklightRuntime::request(uint8_t level)
{
    if (impl_->stopping || impl_->options.of_node.empty()) return false;
    impl_->pending = level;
    impl_->wake.notify_all();
    return true;
}
bool configure_keyboard_backlight(const KeyboardBacklightOptions& options)
{
    if (!valid_keyboard_backlight_options(options)) return false;
    std::lock_guard<std::mutex> lock(global_mutex);
    if (configured || runtime || stopped) return false;
    configured_options = options;
    configured = true;
    return true;
}
void prepare_keyboard_backlight()
{
    std::lock_guard<std::mutex> lock(global_mutex);
    if (!runtime && !stopped && !configured_options.of_node.empty()) runtime = std::make_unique<KeyboardBacklightRuntime>(configured_options);
}
BacklightSnapshot keyboard_backlight_snapshot()
{
    std::lock_guard<std::mutex> lock(global_mutex);
    return runtime ? runtime->snapshot() : BacklightSnapshot{};
}
bool request_keyboard_backlight(uint8_t level)
{
    std::lock_guard<std::mutex> lock(global_mutex);
    return runtime && runtime->request(level);
}
void shutdown_keyboard_backlight()
{
    std::unique_ptr<KeyboardBacklightRuntime> owned;
    {
        std::lock_guard<std::mutex> lock(global_mutex);
        stopped = true;
        owned = std::move(runtime);
    }
}
} // namespace platform::linux_runtime
