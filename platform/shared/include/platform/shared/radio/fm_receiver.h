#pragma once

#include "platform/shared/radio/fm_iq_consumer.h"

namespace platform::shared::radio
{
// Independent FM application service. A single worker owns this object; page
// callbacks must dispatch commands to that worker, never call it concurrently.
// Keep it in persistent storage, not on an ESP task stack.
class FmReceiver
{
  public:
    class Port
    {
      public:
        virtual ~Port() = default;
        // Failed acquisitions retain no ownership. Successful acquisitions must
        // be paired with release even when starting the stream subsequently fails.
        virtual bool acquireRadio() = 0;
        virtual bool acquireAudio(uint32_t sample_rate) = 0;
        // RX-only: implementations must never bypass packet TX frequency guards.
        // Start must qualify the requested rate and RF passband, not just accept
        // a chip command. No source-1 rate inferred from requested rate alone.
        virtual bool startIq(uint32_t frequency_hz, uint32_t pair_rate) = 0;
        // Called even after failed startIq: cancel producer, join, restore PHY.
        virtual bool stopIq() = 0;
        // Return actual accepted samples. Short writes are fatal, not playback.
        virtual size_t writeAudio(const int16_t* samples, size_t count) = 0;
        // Flush pending PCM and relinquish audio focus before returning.
        virtual bool releaseAudio() = 0;
        virtual bool releaseRadio() = 0;
    };
    enum class Phase
    {
        Stopped,
        Receiving,
        Playing,
        Error
    };
    enum class Error
    {
        None,
        Frequency,
        RadioBusy,
        AudioBusy,
        ReceiveStart,
        InvalidIq,
        Discontinuity,
        AudioWrite,
        Cleanup
    };
    struct Snapshot
    {
        Phase phase = Phase::Stopped;
        Error error = Error::None;
        uint32_t frequency_hz = 0;
        uint64_t session = 0;
        uint64_t pcm_samples = 0;
        bool cleanup_failed = false;
        uint8_t volume = 50;
    };

    explicit FmReceiver(Port& port) : port_(port) {}
    FmReceiver(const FmReceiver&) = delete;
    FmReceiver& operator=(const FmReceiver&) = delete;
    // Owner must stop/join its worker before destroying the port or service.
    const Snapshot& snapshot() const { return state_; }

    bool start(uint32_t frequency_hz)
    {
        // Reject invalid UI input without disrupting an existing station.
        if (frequency_hz < 87500000 || frequency_hz > 108000000) return false;
        if (!stop()) return false;
        state_.session++;
        state_.frequency_hz = frequency_hz;
        state_.pcm_samples = 0;
        consumer_.reset();
        connected_ = false;
        if (!port_.acquireRadio()) return fail(Error::RadioBusy);
        radio_owned_ = true;
        if (!port_.acquireAudio(BroadcastFmDecoder::kOutputRate)) return fail(Error::AudioBusy);
        audio_owned_ = true;
        stream_attempted_ = true;
        if (!port_.startIq(frequency_hz, BroadcastFmDecoder::kInputRate)) return fail(Error::ReceiveStart);
        state_.phase = Phase::Receiving;
        return true;
    }

    bool stop()
    {
        bool ok = true;
        if (stream_attempted_)
        {
            ok = port_.stopIq() && ok;
            stream_attempted_ = false;
        }
        if (audio_owned_)
        {
            ok = port_.releaseAudio() && ok;
            audio_owned_ = false;
        }
        if (radio_owned_)
        {
            ok = port_.releaseRadio() && ok;
            radio_owned_ = false;
        }
        consumer_.reset();
        connected_ = false;
        state_.phase = ok ? Phase::Stopped : Phase::Error;
        state_.error = ok ? Error::None : Error::Cleanup;
        state_.cleanup_failed = !ok;
        return ok;
    }

    void setVolume(uint8_t percent) { state_.volume = percent > 100 ? 100 : percent; }

    // Called by the consumer worker, never by the time-critical FIFO callback.
    // Session tags reject queued blocks left over from a previous station.
    bool consume(uint64_t session, const uint8_t* iq, size_t bytes, uint32_t rate,
                 uint32_t generation, uint64_t first_pair, FmIqConsumer::Format format)
    {
        if (session != state_.session ||
            (state_.phase != Phase::Receiving && state_.phase != Phase::Playing)) return false;
        const auto result = consumer_.consume(iq, bytes, rate, generation, first_pair,
                                              pcm_, FmIqConsumer::kMaxPcmSamples, format);
        if (result.status != FmIqConsumer::Status::Accepted && result.status != FmIqConsumer::Status::GapReset)
            return fail(Error::InvalidIq);
        // First block seeds the discriminator. Subsequent gaps require a fresh
        // session; do not silently continue playing after FIFO loss or retune.
        if (connected_ && result.status == FmIqConsumer::Status::GapReset) return fail(Error::Discontinuity);
        connected_ = true;
        for (size_t n = 0; n < result.samples; ++n)
            pcm_[n] = static_cast<int16_t>(static_cast<int32_t>(pcm_[n]) * state_.volume / 100);
        if (result.samples && port_.writeAudio(pcm_, result.samples) != result.samples)
            return fail(Error::AudioWrite);
        state_.pcm_samples += result.samples;
        if (result.samples) state_.phase = Phase::Playing;
        return true;
    }

  private:
    bool fail(Error error)
    {
        stop();
        state_.phase = Phase::Error;
        state_.error = error; // Preserve root cause alongside cleanup failure.
        return false;
    }
    Port& port_;
    FmIqConsumer consumer_;
    int16_t pcm_[FmIqConsumer::kMaxPcmSamples]{};
    Snapshot state_;
    bool radio_owned_ = false;
    bool audio_owned_ = false;
    bool stream_attempted_ = false;
    bool connected_ = false;
};
} // namespace platform::shared::radio
