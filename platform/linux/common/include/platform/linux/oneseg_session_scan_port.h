#pragma once

#include "platform/linux/oneseg_channel_name.h"
#include "platform/linux/oneseg_scan_controller.h"
#include "platform/linux/oneseg_tv_session.h"

namespace platform::linux_runtime
{
// Translate the real receiver's evidence without treating an error as no signal.
inline OneSegScanPort::Observation oneseg_scan_observation(const OneSegTvSession::Snapshot& state)
{
    OneSegScanPort::Observation value;
    value.active = state.active;
    value.channel = state.channel;
    value.message = state.message;
    using ReceiveError = OneSegReceiveWorker::Error;
    value.restoration_failed = state.receive.lease_restore_failed || state.receive.error == ReceiveError::RadioRestore;
    const bool decoder_end = state.receive.error == ReceiveError::None ||
                             state.receive.error == ReceiveError::SinkWrite ||
                             state.receive.error == ReceiveError::SinkFinish;
    value.no_signal = !state.active && state.automatic_parameters && decoder_end &&
                      state.decoder.child_exit_code == 4 && !state.decoder.child_signal &&
                      !state.decoder.forced_kill && !value.restoration_failed;
    // Errors can be published while child/RF cleanup is still in progress.
    // Wait for the terminal snapshot before distinguishing exit 4 from failure.
    value.failed = !state.active && state.error != OneSegTvSession::Error::None && !value.no_signal;
    const auto& table = state.decoder.programmes;
    value.programme = table.selected_program;
    for (size_t i = 0; i < std::min(table.program_count, table.programs.size()); ++i)
        if (table.programs[i].number) value.programmes.push_back(table.programs[i].number);
    const bool current = state.generation && state.decoder.generation == state.generation &&
                         state.media_epoch && state.media.generation == state.media_epoch;
    value.confirmed_media = state.active && state.error == OneSegTvSession::Error::None && !value.restoration_failed &&
                            state.phase != OneSegTvSession::Phase::Stopping && current &&
                            state.iq_age_ms < 2000 && table.pmt_valid && table.selected_program &&
                            state.media.error == OneSegMediaWorker::Error::None &&
                            ((table.selected_video_packets && state.media.video_frames && state.video_age_ms < 2000) ||
                             (table.selected_audio_packets && state.media.pcm_samples));
    if (value.confirmed_media) value.name = oneseg_received_channel_name(state.service_text, table.selected_program);
    return value;
}

class OneSegSessionScanPort final : public OneSegScanPort
{
  public:
    explicit OneSegSessionScanPort(OneSegTvSession& session) : session_(session) {}
    Observation observe() const override { return oneseg_scan_observation(session_.snapshot()); }
    bool start(unsigned channel) override
    {
        // A fixed research profile cannot implement a general channel search.
        if (!session_.snapshot().automatic_parameters) return false;
        return session_.start(channel, 320, 240);
    }
    void stop() override { session_.requestStop(); }
    bool select(unsigned programme) override
    {
        if (!programme || programme > 65535) return false;
        const auto state = session_.snapshot();
        return session_.selectProgramme(state.generation, static_cast<uint16_t>(programme));
    }

  private:
    OneSegTvSession& session_;
};
} // namespace platform::linux_runtime
