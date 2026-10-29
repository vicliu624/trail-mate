#include "chat/linux_mesh_worker.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/radio_power.h"
#include "platform/ui/settings_store.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <mutex>
#include <thread>
#include <unistd.h>

using namespace platform::linux_runtime;
using namespace std::chrono_literals;
struct Radio final : IPacketRadio
{
    const char* name() const override { return "power test"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override { return true; }
    void release() override {}
    bool configureLoRa(const RadioSettings&) override { return true; }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t*, size_t) override { return true; }
    bool pollReceive(RadioPacket*) override { return false; }
    float readRssi() override { return -100; }
    bool isOnline() const override { return false; }
    const char* lastError() const override { return ""; }
    RadioSettings appliedLoRaConfig() const override { return {}; }
    RadioStatistics stats() const override { return {}; }
};
struct Power final : IRadioPowerControl
{
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic<bool> on{true};
    bool entered = false, blocked = true, refuse = false;
    const std::thread::id ui = std::this_thread::get_id();
    bool setEnabled(bool value) override
    {
        assert(std::this_thread::get_id() != ui);
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        wake.notify_all();
        wake.wait(lock, [&]
                  { return !blocked; });
        if (refuse) return false;
        on = value;
        return true;
    }
    bool enabled() const override { return on; }
};
template <class Predicate>
void waitUntil(Predicate predicate)
{
    const auto end = std::chrono::steady_clock::now() + 3s;
    while (!predicate() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
    assert(predicate());
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-power-worker-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    platform::ui::settings_store::put_bool("settings", "radio_enabled", true);
    Radio radio;
    Power power;
    trailmate::linux_app::LinuxMeshWorker worker(123, &radio, &power);
    assert(worker.initialize());
    auto requests = worker.powerRequests();
    bind_radio_power(requests);
    assert(request_radio_enabled(false));
    {
        std::unique_lock<std::mutex> lock(power.mutex);
        assert(power.wake.wait_for(lock, 2s, [&]
                                   { return power.entered; }));
    }
    auto getter = std::async(std::launch::async, [&]
                             {
        auto state = radio_power_snapshot();
        assert(state.enabled && state.changing);
        assert(platform::ui::settings_store::get_bool("settings", "radio_enabled", false)); });
    const bool responsive = getter.wait_for(1s) == std::future_status::ready;
    {
        std::lock_guard<std::mutex> lock(power.mutex);
        power.blocked = false;
        power.wake.notify_all();
    }
    assert(responsive);
    getter.get();
    waitUntil([&]
              { return !requests->snapshot().changing; });
    assert(!requests->snapshot().enabled && !requests->snapshot().failed);
    assert(!platform::ui::settings_store::get_bool("settings", "radio_enabled", true));
    assert(request_radio_enabled(true));
    waitUntil([&]
              { return !requests->snapshot().changing; });
    assert(requests->snapshot().enabled);
    {
        std::lock_guard<std::mutex> lock(power.mutex);
        power.refuse = true;
    }
    assert(request_radio_enabled(false));
    waitUntil([&]
              { return !requests->snapshot().changing; });
    assert(requests->snapshot().failed && requests->snapshot().enabled);
    assert(platform::ui::settings_store::get_bool("settings", "radio_enabled", false));
    worker.shutdown();
    assert(!requests->request(false));
    unbind_radio_power(requests);
    std::filesystem::remove_all(root);
}
