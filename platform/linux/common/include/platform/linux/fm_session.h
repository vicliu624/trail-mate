#pragma once
#include "platform/shared/radio/fm_iq_stream.h"
#include <memory>

namespace platform::linux_runtime
{
class IPacketRadio;
// UI-safe commands/snapshots. Radio and handoff must outlive this owner.
// Destruction joins workers and is shutdown-only, never a page-exit callback.
class FmSession
{
  public:
    enum class Phase
    {
        Idle,
        Starting,
        PcmQueued,
        Stopping,
        Stopped,
        Error
    };
    enum class Error
    {
        None,
        Handoff,
        RadioAcquire,
        Audio,
        InvalidIq,
        QueueFull,
        Stream,
        Restore,
        Internal
    };
    struct Snapshot
    {
        Phase phase = Phase::Idle;
        Error error = Error::None;
        uint64_t session = 0, pcm_samples = 0;
        uint32_t frequency_hz = 100800000;
        bool active = false, calibration_exception_requested = false;
        bool lease_restore_failed = false;
        // Final diagnostics only; not a live estimate or station-lock signal.
        platform::shared::radio::FmIqResult radio;
    };
    FmSession(IPacketRadio&, bool (*handoff)());
    ~FmSession();
    // Active requests coalesce to the latest station and cancel the old one.
    bool tune(uint32_t frequency_hz, bool allow_image_calibration_warning = false);
    void requestStop();
    void setVolume(unsigned percent);
    Snapshot snapshot() const;
    void waitUntilFinished(); // Shutdown/tests only.
    FmSession(const FmSession&) = delete;
    FmSession& operator=(const FmSession&) = delete;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
