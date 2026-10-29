#include "platform/linux/music_service.h"
#include "music/cover_decoder.h"
#include "music/player.h"
#include "platform/audio/audio_focus.h"
#include "platform/audio/media_volume.h"
#include "platform/linux/music_storage.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/sdl_pcm_output.h"
#include "platform/ui/settings_store.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace platform::linux_runtime
{
namespace
{
class Service final : public ::music::IService
{
    class Images final : public ::music::ImageLoader
    {
      public:
        explicit Images(Service& service) : service_(service) {}
        ::music::Artwork* decode(::music::Reader& input, const ::music::Track& track, uint16_t edge,
                                 bool (*pump)(void*), void* context, ::music::CoverResult& result) override
        {
            struct Context
            {
                Service* service;
                bool (*pump)(void*);
                void* context;
            } state{&service_, pump, context};
            return ::music::decode_cover(
                input, track, edge, result, [](void* raw)
                {
                auto& value = *static_cast<Context*>(raw);
                return !value.service->stopping_ && !value.service->urgent_ &&
                       (!value.pump || value.pump(value.context)); },
                &state);
        }

      private:
        Service& service_;
    };
    struct Runtime
    {
        LinuxMusicStorage storage;
        SdlPcmOutput output;
        Images images;
        ::music::Player player;
        explicit Runtime(Service& owner)
            : storage(resolve_paths().sd_root), output(true, &owner.audio_), images(owner), player(storage, output, images)
        {
            storage.mkdir("/Music");
        }
    };

  public:
    Service()
    {
        volume_ = platform::audio::media_volume(45);
        published_.volume = volume_;
        platform::audio::observe_media_volume(volumeChanged, this);
        platform::audio::register_music_focus_client({this, suspend, resume});
    }
    ~Service() override { shutdown(); }
    void prepare()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        startLocked();
    }
    void attach(uint8_t rows) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        attached_ = true;
        rows_ = std::min<uint8_t>(rows, ::music::kPageRows);
        properties_ = true;
        startLocked();
    }
    void detach() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        attached_ = false;
        rows_ = 0;
        edge_ = 0;
        lyrics_ = false;
        properties_ = true;
        wake_.notify_all();
    }
    bool snapshot(::music::Snapshot& output) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        output = published_;
        return true;
    }
    bool send(const ::music::Request& command) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (requests_.size() >= 16) return false;
        requests_.push_back(command);
        urgent_ = true;
        startLocked();
        return true;
    }
    uint8_t volume() const override { return volume_.load(); }
    void request_artwork(uint16_t edge) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        edge_ = edge ? std::clamp<uint16_t>(edge, 32, 240) : 0;
        properties_ = true;
        wake_.notify_all();
    }
    void request_lyrics(bool visible) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lyrics_ = visible;
        properties_ = true;
        wake_.notify_all();
    }
    ::music::Artwork* acquire_artwork() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (artwork_) artwork_->retain();
        return artwork_;
    }
    void shutdown()
    {
        platform::audio::register_music_focus_client({});
        platform::audio::observe_media_volume(nullptr, nullptr);
        stopping_ = true;
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        if (artwork_)
        {
            artwork_->drop();
            artwork_ = nullptr;
        }
    }

  private:
    void startLocked()
    {
        if (!stopping_ && !worker_.joinable()) worker_ = std::thread([this]
                                                                     { run(); });
        wake_.notify_all();
    }
    static bool suspend(void* context)
    {
        auto& self = *static_cast<Service*>(context);
        self.wake_.notify_all();
        std::unique_lock<std::mutex> lock(self.mutex_);
        return self.wake_.wait_for(lock, std::chrono::milliseconds(1000), [&]
                                   { return !self.audio_.active && !self.audio_.opening; });
    }
    static void resume(void* context) { static_cast<Service*>(context)->wake_.notify_all(); }
    static void volumeChanged(void* context, uint8_t percent)
    {
        auto& self = *static_cast<Service*>(context);
        self.volume_ = percent;
        self.volume_dirty_ = true;
        self.wake_.notify_all();
    }
    void publish(Runtime& runtime)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        published_ = runtime.player.state;
        auto* next = runtime.player.artwork();
        if (next == artwork_) return;
        if (next) next->retain();
        auto* old = artwork_;
        artwork_ = next;
        if (old) old->drop();
    }
    void run()
    {
        try
        {
            Runtime runtime(*this);
            volume_ = platform::audio::media_volume(std::clamp(platform::ui::settings_store::get_int("settings", "speaker_volume", 45), 0, 100));
            runtime.player.state.volume = volume_;
            platform::audio::publish_media_volume(volume_);
            while (!stopping_)
            {
                ::music::Request request;
                bool command = false, properties = false;
                uint8_t rows = 0;
                uint16_t edge = 0;
                bool lyrics = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (!requests_.empty())
                    {
                        request = requests_.front();
                        requests_.pop_front();
                        command = true;
                    }
                    urgent_ = !requests_.empty();
                    properties = properties_;
                    properties_ = false;
                    rows = attached_ ? rows_ : 0;
                    edge = attached_ ? edge_ : 0;
                    lyrics = attached_ && lyrics_;
                }
                if (properties)
                {
                    runtime.player.attach(rows);
                    runtime.player.request_artwork(edge);
                    runtime.player.request_lyrics(lyrics);
                }
                if (command)
                {
                    runtime.player.command(request);
                    if (request.action == ::music::Action::Volume)
                    {
                        platform::audio::publish_media_volume(runtime.player.state.volume);
                        platform::ui::settings_store::put_int("settings", "speaker_volume", runtime.player.state.volume);
                    }
                }
                if (volume_dirty_.exchange(false))
                {
                    ::music::Request gain;
                    gain.action = ::music::Action::Volume;
                    gain.value = volume_;
                    runtime.player.command(gain);
                }
                runtime.player.tick();
                publish(runtime);
                wake_.notify_all(); // A focus waiter observes physical audio closure.
                std::unique_lock<std::mutex> lock(mutex_);
                if (requests_.empty() && !properties_ && !volume_dirty_)
                    wake_.wait_for(lock, std::chrono::milliseconds(runtime.player.active() ? 5 : 100));
            }
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            published_.playback = ::music::Playback::Error;
            ::music::copy_text(published_.notice, sizeof(published_.notice), "Linux music service failed");
        }
        wake_.notify_all();
    }
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    std::deque<::music::Request> requests_;
    ::music::Snapshot published_;
    ::music::Artwork* artwork_ = nullptr;
    PcmActivity audio_;
    std::atomic_bool stopping_{false}, urgent_{false}, volume_dirty_{false};
    std::atomic<uint8_t> volume_{45};
    bool attached_ = false, properties_ = false, lyrics_ = false;
    uint8_t rows_ = 0;
    uint16_t edge_ = 0;
};
std::atomic<Service*> existing{nullptr};
Service& service()
{
    static Service value;
    existing.store(&value);
    return value;
}
} // namespace

::music::IService* linux_music_service() { return &service(); }
void prepare_linux_music() { service().prepare(); }
void shutdown_linux_music()
{
    if (auto* current = existing.load()) current->shutdown();
}
} // namespace platform::linux_runtime
