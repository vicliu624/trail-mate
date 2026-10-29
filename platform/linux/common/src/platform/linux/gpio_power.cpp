#include "platform/linux/gpio_power.h"
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#if defined(__linux__)
#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace platform::linux_runtime
{
bool valid_gpio_power_options(const GpioPowerOptions& options)
{
    return options.line >= -1 && options.startup_ms <= 120000 &&
           (options.chip.empty() ? options.line < 0 : std::filesystem::path(options.chip).is_absolute());
}
namespace
{
class NativeLine final : public GpioPowerLine
{
  public:
    ~NativeLine() override
    {
        int ignored = 0;
        disable(ignored);
    }
    bool enable(const GpioPowerOptions& options, int& error) override
    {
        error = 0;
        if (!valid_gpio_power_options(options) || options.line < 0)
        {
            error = EINVAL;
            return false;
        }
#if defined(__linux__)
        if (line_ >= 0) return true;
        const int chip = ::open(options.chip.c_str(), O_RDONLY | O_CLOEXEC);
        if (chip < 0)
        {
            error = errno;
            return false;
        }
        gpio_v2_line_request request{};
        request.num_lines = 1;
        request.offsets[0] = static_cast<uint32_t>(options.line);
        request.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
        if (options.active_low) request.config.flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;
        request.config.num_attrs = 1;
        request.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
        request.config.attrs[0].attr.values = 1; // Logical active, set atomically.
        request.config.attrs[0].mask = 1;
        std::strncpy(request.consumer, "trailmate-gnss-power", sizeof(request.consumer) - 1);
        const int result = ::ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &request);
        if (result < 0) error = errno;
        ::close(chip);
        if (result < 0) return false;
        line_ = request.fd;
        return true;
#else
        error = ENOTSUP;
        return false;
#endif
    }
    bool disable(int& error) override
    {
        error = 0;
#if defined(__linux__)
        if (line_ < 0) return true;
        gpio_v2_line_values value{};
        value.mask = 1;
        const bool success = ::ioctl(line_, GPIO_V2_LINE_SET_VALUES_IOCTL, &value) == 0;
        if (!success) error = errno;
        ::close(line_);
        line_ = -1;
        return success;
#else
        return true;
#endif
    }

  private:
    int line_ = -1;
};
} // namespace
struct AsyncGpioPower::Impl
{
    using Clock = std::chrono::steady_clock;
    Impl(GpioPowerOptions value, std::shared_ptr<GpioPowerLine> backend)
        : options(std::move(value)), line(backend ? std::move(backend) : std::make_shared<NativeLine>()), worker([this]
                                                                                                                 { run(); }) {}
    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        worker.join();
    }
    void run()
    {
        std::unique_lock<std::mutex> lock(mutex);
        bool held = false;
        uint64_t completed = 0;
        Clock::time_point ready_at;
        while (!stopping)
        {
            if (completed == generation)
            {
                wake.wait(lock, [this, completed]
                          { return stopping || generation != completed; });
                continue;
            }
            const auto epoch = generation;
            const bool target = current.requested;
            int error = 0;
            if (target && !held)
            {
                lock.unlock();
                bool success = false;
                try
                {
                    success = line->enable(options, error);
                }
                catch (...)
                {
                    error = EIO;
                }
                const auto enabled_at = Clock::now();
                lock.lock();
                held = success;
                ready_at = enabled_at + std::chrono::milliseconds(options.startup_ms);
                if (epoch != generation) continue;
                if (!success)
                {
                    current.state = State::Failed;
                    current.error = error ? error : EIO;
                    // Retry busy/unavailable hardware with bounded backoff.
                    wake.wait_for(lock, std::chrono::seconds(1), [this, epoch]
                                  { return stopping || generation != epoch; });
                    continue;
                }
            }
            if (target && held)
            {
                if (epoch != generation) continue;
                current.state = State::Starting;
                current.error = 0;
                if (wake.wait_until(lock, ready_at, [this, epoch]
                                    { return stopping || generation != epoch; })) continue;
                current.state = State::Ready;
                completed = epoch;
            }
            else if (!target)
            {
                bool success = true;
                if (held)
                {
                    lock.unlock();
                    try
                    {
                        success = line->disable(error);
                    }
                    catch (...)
                    {
                        success = false;
                        error = EIO;
                    }
                    lock.lock();
                    held = false;
                }
                if (epoch != generation) continue;
                current.state = success ? State::Off : State::Failed;
                current.error = success ? 0 : error ? error
                                                    : EIO;
                completed = epoch;
            }
        }
        lock.unlock();
        if (held)
        {
            int ignored = 0;
            try
            {
                line->disable(ignored);
            }
            catch (...)
            {
            }
        }
    }
    GpioPowerOptions options;
    std::shared_ptr<GpioPowerLine> line;
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    uint64_t generation = 0;
    Snapshot current;
    std::thread worker;
};
AsyncGpioPower::AsyncGpioPower(GpioPowerOptions options, std::shared_ptr<GpioPowerLine> line)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(line))) {}
AsyncGpioPower::~AsyncGpioPower() = default;
void AsyncGpioPower::request(bool enabled)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (enabled == impl_->current.requested) return;
    impl_->current.requested = enabled;
    impl_->current.state = enabled ? State::Starting : State::Stopping;
    impl_->current.error = 0;
    ++impl_->generation;
    impl_->wake.notify_all();
}
AsyncGpioPower::Snapshot AsyncGpioPower::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->current;
}
} // namespace platform::linux_runtime
