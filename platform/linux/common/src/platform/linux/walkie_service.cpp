#include "platform/linux/walkie_service.h"
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
#include "app/app_facade_access.h"
#include "platform/audio/media_volume.h"
#include "platform/linux/sdl_pcm_input.h"
#include "platform/linux/sdl_pcm_output.h"
#include "platform/linux/walkie_session.h"
#include <SDL.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <random>
#include <string>
#include <thread>

namespace platform::linux_runtime
{
namespace
{
class Audio final : public IWalkieAudio
{
  public:
    bool startCapture() override
    {
        const bool ready = input.begin(8000, std::getenv("TRAIL_MATE_WALKIE_INPUT_DEVICE"));
        if (!ready) error = input.last_error();
        return ready;
    }
    void stopCapture() override { input.end(); }
    std::size_t capture(int16_t* samples, std::size_t count) override { return input.read(samples, count); }
    uint64_t captureDropped() const override { return input.dropped_samples(); }
    bool captureActive() const override { return input.active(); }
    bool play(const int16_t* samples, std::size_t count) override
    {
        if ((!output.active() && !output.begin(8000)) || !output.write(samples, count))
        {
            error = SDL_GetError();
            return false;
        }
        return true;
    }
    bool playbackReady() const override { return !output.active() || output.pending_ms() < 120; }
    void stopPlayback() override { output.end(); }
    const char* lastError() const override { return error.c_str(); }

  private:
    SdlPcmInput input;
    SdlPcmOutput output{false, nullptr, 256};
    std::string error;
};
class Service;
std::atomic<Service*> existing{nullptr};
class Service
{
  public:
    Service() { existing = this; }
    ~Service() { shutdown(); }
    bool start(bool monitor = false)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return false;
        if (status_.active)
        {
            if (monitor) monitor_ = true;
            return true;
        }
        if (!app::hasAppFacade() || !(self_ = static_cast<uint32_t>(app::messagingFacade().getSelfNodeId())))
        {
            error_ = "Device identity is not initialized";
            return false;
        }
        status_ = {};
        status_.active = true;
        monitor_ = monitor;
        ptt_ = false;
        error_.clear();
        if (++epoch_ == 0) ++epoch_;
        pending_ = epoch_.load();
        try
        {
            if (!worker_.joinable()) worker_ = std::thread([this]
                                                           { run(); });
        }
        catch (...)
        {
            status_.active = false;
            monitor_ = false;
            error_ = "Cannot start walkie worker";
            return false;
        }
        wake_.notify_all();
        return true;
    }
    void stop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++epoch_;
        pending_ = 0;
        ptt_ = false;
        monitor_ = false;
        status_ = {};
        wake_.notify_all();
    }
    void shutdown()
    {
        stopping_ = true;
        stop();
        if (worker_.joinable()) worker_.join();
    }
    void ptt(bool pressed)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ptt_ = status_.active && pressed;
        wake_.notify_all();
    }
    bool monitor(bool enabled)
    {
        if (enabled) return start(true);
        monitor_ = false;
        ptt_ = false;
        return true;
    }
    ::platform::ui::walkie::Status status()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto result = status_;
        result.monitor_enabled = monitor_;
        return result;
    }
    std::string error()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return error_;
    }

  private:
    void run()
    {
        while (!stopping_)
        {
            uint32_t id, self;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this]
                           { return stopping_ || pending_; });
                if (stopping_) return;
                id = pending_;
                pending_ = 0;
                self = self_;
            }
            std::string failure;
            try
            {
                Audio audio;
                auto& radio = foreground_packet_radio();
                WalkieOptions options;
                options.radio = mesh_packet_radio().appliedLoRaConfig();
                options.self_id = self;
                options.session_seed = static_cast<uint16_t>(std::random_device{}());
                WalkieSession session(radio, audio);
                if (!session.begin(options)) failure = session.lastError();
                else
                    while (!stopping_ && epoch_ == id)
                    {
                        const auto now = std::chrono::steady_clock::now();
                        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                        if (!session.step(ptt_, static_cast<uint32_t>(milliseconds), ::platform::audio::media_volume(45)))
                        {
                            failure = session.lastError();
                            break;
                        }
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            if (epoch_ == id) status_ = session.status();
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
            }
            catch (const std::exception& error)
            {
                failure = error.what();
            }
            std::lock_guard<std::mutex> lock(mutex_);
            if (epoch_ == id)
            {
                status_ = {};
                monitor_ = false;
                ptt_ = false;
                error_ = std::move(failure);
            }
        }
    }
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    std::atomic_bool stopping_{false}, ptt_{false}, monitor_{false};
    std::atomic<uint32_t> epoch_{0};
    uint32_t pending_ = 0, self_ = 0;
    ::platform::ui::walkie::Status status_;
    std::string error_;
};
Service& service()
{
    static Service value;
    return value;
}
} // namespace
bool native_walkie_built() { return true; }
bool start_native_walkie() { return service().start(); }
void stop_native_walkie() { service().stop(); }
void shutdown_native_walkie()
{
    if (auto* value = existing.load()) value->shutdown();
}
void native_walkie_ptt(bool pressed) { service().ptt(pressed); }
bool native_walkie_monitor(bool enabled) { return service().monitor(enabled); }
::platform::ui::walkie::Status native_walkie_status() { return service().status(); }
const char* native_walkie_error()
{
    thread_local std::string value;
    value = service().error();
    return value.c_str();
}
} // namespace platform::linux_runtime
#else
namespace platform::linux_runtime
{
bool native_walkie_built() { return false; }
bool start_native_walkie() { return false; }
void stop_native_walkie() {}
void shutdown_native_walkie() {}
void native_walkie_ptt(bool) {}
bool native_walkie_monitor(bool) { return false; }
::platform::ui::walkie::Status native_walkie_status() { return {}; }
const char* native_walkie_error() { return "Native walkie audio is not built"; }
} // namespace platform::linux_runtime
#endif
