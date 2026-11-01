#pragma once

#include "platform/linux/oneseg_tv_session.h"
#include <cstdio>

namespace platform::linux_runtime
{
// Software presentation acknowledgement and real PCM queue progress are
// distinct from decoding counters. Neither attests physical speaker acoustics.
inline int format_oneseg_telemetry(char* output, size_t capacity, const OneSegTvSession::Snapshot& s,
                                   uint64_t monotonic_ms, uint32_t presentation_age_ms,
                                   const OneSegAudioClock::Snapshot& audio, uint64_t process_id = 0)
{
    const bool audio_active = s.active && s.error == OneSegTvSession::Error::None && s.media_epoch &&
                              audio.generation == s.media_epoch && audio.valid && !audio.starved &&
                              s.media.pcm_samples && s.volume && s.media.error == OneSegMediaWorker::Error::None;
    const bool presenting = s.active && s.phase == OneSegTvSession::Phase::FramePresented &&
                            s.error == OneSegTvSession::Error::None && s.media_epoch &&
                            s.receive.error == OneSegReceiveWorker::Error::None && s.media.error == OneSegMediaWorker::Error::None &&
                            s.media.generation == s.media_epoch && s.media.video_frames && s.presented_frames &&
                            s.iq_age_ms < 2000 && s.video_age_ms < 2000 && presentation_age_ms < 2000 &&
                            (s.timing == OneSegTvSession::Timing::VideoMonotonic ||
                             (s.timing == OneSegTvSession::Timing::AudioQueueEstimate && audio_active));
    const int bytes = std::snprintf(output, capacity,
                                    "{\"event\":\"oneseg_status\",\"pid\":%llu,\"monotonic_ms\":%llu,\"generation\":%llu,\"media_epoch\":%llu,"
                                    "\"channel\":%u,\"active\":%u,\"phase\":%u,\"error\":%u,\"rx_error\":%u,"
                                    "\"iq_pairs\":%llu,\"iq_age_ms\":%u,\"iq_queue_high_water\":%u,\"iq_queue_slots\":%u,"
                                    "\"capture_error\":%d,\"recovery_error\":%d,\"max_fifo_level\":%u,\"max_service_gap_us\":%llu,"
                                    "\"ts_bytes\":%llu,\"programme\":%u,\"pmt_valid\":%u,\"transport_errors\":%llu,"
                                    "\"continuity_errors\":%llu,\"crc_errors\":%llu,\"video_frames\":%llu,"
                                    "\"presented_frames\":%llu,\"video_age_ms\":%u,\"presentation_age_ms\":%u,\"presenting\":%u,"
                                    "\"pcm_samples\":%llu,\"audio_output_active\":%u,\"audio_queued_us\":%u,\"audio_starved\":%u,"
                                    "\"timing\":%u,\"av_delta_us\":%lld,\"decode_errors\":%llu,\"media_error\":%u}\n",
                                    (unsigned long long)process_id, (unsigned long long)monotonic_ms, (unsigned long long)s.generation, (unsigned long long)s.media_epoch,
                                    s.channel, unsigned(s.active), unsigned(s.phase), unsigned(s.error), unsigned(s.receive.error),
                                    (unsigned long long)s.receive.forwarded_pairs, s.iq_age_ms, s.receive.queue_high_water, OneSegReceiveWorker::queue_slots,
                                    s.receive.radio.capture_error, s.receive.radio.recovery_error, s.receive.radio.max_fifo_level,
                                    (unsigned long long)s.receive.radio.max_service_gap_us, (unsigned long long)s.decoder.ts_bytes,
                                    unsigned(s.decoder.programmes.selected_program), unsigned(s.decoder.programmes.pmt_valid),
                                    (unsigned long long)s.decoder.programmes.transport_errors, (unsigned long long)s.decoder.programmes.continuity_errors,
                                    (unsigned long long)s.decoder.programmes.crc_errors, (unsigned long long)s.media.video_frames,
                                    (unsigned long long)s.presented_frames, s.video_age_ms, presentation_age_ms, unsigned(presenting),
                                    (unsigned long long)s.media.pcm_samples, unsigned(audio_active), audio.queued_us, unsigned(audio.starved),
                                    unsigned(s.timing), (long long)s.av_delta_us, (unsigned long long)s.media.decode_errors, unsigned(s.media.error));
    return bytes > 0 && static_cast<size_t>(bytes) < capacity ? bytes : 0;
}
} // namespace platform::linux_runtime
