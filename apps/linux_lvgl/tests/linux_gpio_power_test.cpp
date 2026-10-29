#include "platform/linux/gpio_power.h"
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace platform::linux_runtime;
using namespace std::chrono_literals;
template <class F>
void eventually(F predicate)
{
    const auto end = std::chrono::steady_clock::now() + 3s;
    while (!predicate() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
    assert(predicate());
}
struct Line final : GpioPowerLine
{
    std::atomic<unsigned> enables{0}, disables{0};
    std::atomic_bool busy{false}, fail_off{false};
    std::mutex mutex;
    std::condition_variable wake;
    bool blocked = false;
    std::thread::id caller;
    bool enable(const GpioPowerOptions& options, int& error) override
    {
        assert(options.line == 3 && options.active_low);
        std::unique_lock<std::mutex> lock(mutex);
        caller = std::this_thread::get_id();
        ++enables;
        wake.wait(lock, [this]
                  { return !blocked; });
        error = busy ? EBUSY : 0;
        return !busy;
    }
    bool disable(int& error) override
    {
        ++disables;
        error = fail_off ? EIO : 0;
        return !fail_off;
    }
    void unblock()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            blocked = false;
        }
        wake.notify_all();
    }
};
int main()
{
    GpioPowerOptions options{"/dev/gpiochip-test", 3, true, 100};
    assert(valid_gpio_power_options(options) && valid_gpio_power_options({}));
    auto invalid = options;
    invalid.line = -1;
    assert(valid_gpio_power_options(invalid)); // Disable without deleting reusable wiring settings.
    invalid = options;
    invalid.chip = "relative";
    assert(!valid_gpio_power_options(invalid));
    invalid = options;
    invalid.startup_ms = 120001;
    assert(!valid_gpio_power_options(invalid));
    {
        auto line = std::make_shared<Line>();
        AsyncGpioPower power(options, line);
        power.request(true);
        eventually([&]
                   { return line->enables == 1; });
        assert(power.snapshot().state == AsyncGpioPower::State::Starting);
        eventually([&]
                   { return power.snapshot().state == AsyncGpioPower::State::Ready; });
        for (unsigned i = 0; i < 100; ++i) power.request(true);
        assert(line->enables == 1 && line->caller != std::this_thread::get_id());
        power.request(false);
        eventually([&]
                   { return power.snapshot().state == AsyncGpioPower::State::Off; });
        assert(line->disables == 1);
    }
    {
        auto line = std::make_shared<Line>();
        line->blocked = true;
        AsyncGpioPower power(options, line);
        power.request(true);
        eventually([&]
                   { return line->enables == 1; });
        const auto start = std::chrono::steady_clock::now();
        power.request(false);
        assert(power.snapshot().state == AsyncGpioPower::State::Stopping);
        assert(std::chrono::steady_clock::now() - start < 100ms);
        line->unblock();
        eventually([&]
                   { return power.snapshot().state == AsyncGpioPower::State::Off; });
        assert(line->disables == 1); // Late enable completion cannot publish Ready.
    }
    {
        auto line = std::make_shared<Line>();
        line->busy = true;
        AsyncGpioPower power(options, line);
        power.request(true);
        eventually([&]
                   { return power.snapshot().state == AsyncGpioPower::State::Failed; });
        assert(power.snapshot().error == EBUSY);
        line->busy = false;
        eventually([&]
                   { return power.snapshot().state == AsyncGpioPower::State::Ready; });
        assert(line->enables == 2);
        line->fail_off = true;
        power.request(false);
        eventually([&]
                   { return power.snapshot().state == AsyncGpioPower::State::Failed; });
        assert(power.snapshot().error == EIO && !power.snapshot().requested);
        power.request(false);
        assert(power.snapshot().state == AsyncGpioPower::State::Failed);
    }
    {
        auto line = std::make_shared<Line>();
        options.startup_ms = 120000;
        const auto start = std::chrono::steady_clock::now();
        {
            AsyncGpioPower power(options, line);
            power.request(true);
            eventually([&]
                       { return line->enables == 1; });
        }
        assert(line->disables == 1 && std::chrono::steady_clock::now() - start < 1s);
    }
}
