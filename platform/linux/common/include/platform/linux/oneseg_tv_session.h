#pragma once

#include "platform/linux/oneseg_audio_clock.h"
#include "platform/linux/oneseg_decoder_process.h"
#include "platform/linux/oneseg_media_worker.h"
#include "platform/linux/oneseg_service_text.h"

namespace platform::linux_runtime
{
class IPacketRadio;
// All public commands/snapshots are UI-safe. RF, child-process and media cleanup
// run on the owner thread. The referenced radio must outlive this object.
class OneSegTvSession
{
  public:
    struct Config
    {
        std::string python = "/usr/bin/python3", decoder;
        std::vector<std::string> environment;
        unsigned startup_timeout_ms = 15000;
        bool automatic_parameters = true;
    };
    enum class Phase : uint8_t
    {
        Idle,
        Starting,
        ReceivingIq,
        Decoding,
        FramePresented,
        Stopping,
        Stopped,
        Error
    };
    enum class Error : uint8_t
    {
        None,
        Configuration,
        Radio,
        Decoder,
        Media,
        Internal
    };
    enum class Timing : uint8_t
    {
        Unavailable,
        WaitingForAudio,
        AudioQueueEstimate,
        AudioStarved,
        VideoMonotonic,
        MissingVideoPts
    };
    struct Snapshot
    {
        Phase phase = Phase::Idle;
        Error error = Error::None;
        uint64_t generation = 0, media_epoch = 0, presented_frames = 0;
        uint64_t video_clock_reanchors = 0;
        unsigned channel = 13, volume = 70;
        uint16_t requested_programme = 0;
        uint32_t iq_age_ms = UINT32_MAX, video_age_ms = UINT32_MAX;
        uint32_t presentation_age_ms = UINT32_MAX;
        Timing timing = Timing::Unavailable;
        OneSegAudioClock::Snapshot audio_clock;
        int64_t presented_pts_us = INT64_MIN, av_delta_us = INT64_MIN;
        bool active = false;
        bool automatic_parameters = true;
        OneSegReceiveWorker::Snapshot receive;
        OneSegDecoderProcess::Snapshot decoder;
        OneSegMediaWorker::Snapshot media;
        OneSegServiceText service_text;
        char message[160]{};
    };
    OneSegTvSession(IPacketRadio&, Config, bool (*handoff)());
    ~OneSegTvSession();
    bool start(unsigned channel, uint16_t width, uint16_t height);
    void requestStop(uint64_t generation = 0);
    bool selectProgramme(uint64_t generation, uint16_t number);
    void setVolume(unsigned percent);
    Snapshot snapshot() const;
    bool copyLatestFrame(uint64_t generation, uint64_t media_epoch, uint8_t*, size_t capacity,
                         OneSegMediaWorker::FrameInfo&);
    // Call only after submitting that copied frame to the actual display widget.
    // This acknowledges video presentation; it does not attest audible audio.
    bool acknowledgeFrame(uint64_t generation, uint64_t media_epoch, uint64_t ordinal);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Real device composition. Returns null in demo/simulated radio modes.
// The foreground coordinator and physical receive handoff outlive the session.
std::unique_ptr<OneSegTvSession> make_oneseg_tv_session(OneSegTvSession::Config);
} // namespace platform::linux_runtime
