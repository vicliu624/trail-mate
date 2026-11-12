#pragma once

#include "hostlink/management_envelope.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hostlink::management
{

// One explicitly confirmed USB request. The port owns physical operations;
// this owner retains only correlation and bounded transition state.
class DownloadModePort
{
  public:
    virtual ~DownloadModePort() = default;
    virtual bool supported() const = 0;
    virtual bool connected() const = 0;
    virtual bool canBegin() const = 0;
    virtual void stopStorage() = 0;
    virtual bool storageStopped() const = 0;
    virtual bool claimStorage() = 0;
    virtual bool releaseStorage() = 0;
    virtual void resumeStorage() = 0;
    virtual bool reply(const uint8_t* ids, uint16_t sequence, Status status,
                       uint32_t completion_cookie, uint8_t* scratch, size_t capacity) = 0;
    virtual void enterBootloader() = 0;
};

class DownloadModeOwner final
{
  public:
    static constexpr uint16_t kOperation = 10;
    static constexpr uint32_t kTransitionTimeoutMs = 2000;
    static constexpr uint32_t kReplyGraceMs = 100;
    static constexpr uint32_t kDiscardedBit = 0x80000000U;

    explicit DownloadModeOwner(DownloadModePort& port) : port_(port) {}

    Status request(const EnvelopeView& request, uint16_t sequence, uint32_t now_ms)
    {
        if (request.code != kOperation || !request.session || !request.request ||
            !request.body || request.body_size != 2 || request.body[0] != 0x91)
            return Status::InvalidField;
        if (request.body[1] == 0xc2) return Status::Unauthorized;
        if (request.body[1] != 0xc3) return Status::InvalidField;
        if (!port_.supported()) return Status::Unsupported;
        if (state_ != State::Idle || !port_.connected() || !port_.canBegin()) return Status::Busy;
        std::memcpy(ids_, request.session, 16);
        std::memcpy(ids_ + 16, request.request, 16);
        sequence_ = sequence;
        cookie_ = (cookie_ + 1U) & ~kDiscardedBit;
        if (!cookie_) cookie_ = 1U;
        deadline_ = now_ms + kTransitionTimeoutMs;
        state_ = State::Stopping;
        port_.stopStorage();
        return Status::Ok;
    }

    // completion is posted by the USB writer only after the matching queued
    // response was fully written/flushed, or discarded. Stale frames cannot
    // complete a later request even when a host reuses its frame sequence.
    void advance(uint32_t now_ms, uint32_t completion, uint8_t* scratch, size_t capacity)
    {
        if (state_ == State::Idle) return;
        if (state_ == State::Grace)
        {
            if (!due(now_ms)) return;
            // A failed platform restart returns to restoration, rather than
            // leaving storage paused forever. Normally this call never returns.
            state_ = State::Restoring;
            port_.enterBootloader();
            return;
        }
        if (state_ == State::Restoring)
        {
            restore();
            return;
        }
        if (state_ == State::AwaitingReply && completion == cookie_)
        {
            state_ = State::Grace;
            deadline_ = now_ms + kReplyGraceMs;
            return;
        }
        if (!port_.connected() || due(now_ms) ||
            (state_ == State::AwaitingReply && completion == (cookie_ | kDiscardedBit)))
        {
            if (port_.connected()) (void)port_.reply(ids_, sequence_, Status::Busy, 0, scratch, capacity);
            state_ = State::Restoring;
            restore();
            return;
        }
        if (state_ != State::Stopping || !port_.storageStopped()) return;
        if (!claimed_)
        {
            if (!port_.claimStorage()) return;
            claimed_ = true;
        }
        if (!port_.reply(ids_, sequence_, Status::Ok, cookie_, scratch, capacity)) return;
        state_ = State::AwaitingReply;
        deadline_ = now_ms + kTransitionTimeoutMs;
    }

    bool active() const { return state_ != State::Idle; }

  private:
    enum class State : uint8_t
    {
        Idle,
        Stopping,
        AwaitingReply,
        Grace,
        Restoring,
    };

    bool due(uint32_t now_ms) const
    {
        return static_cast<int32_t>(now_ms - deadline_) >= 0;
    }

    void restore()
    {
        if (claimed_)
        {
            if (!port_.releaseStorage()) return;
            claimed_ = false;
        }
        if (!port_.storageStopped()) return;
        port_.resumeStorage();
        state_ = State::Idle;
    }

    DownloadModePort& port_;
    uint8_t ids_[32]{};
    uint32_t deadline_ = 0;
    uint32_t cookie_ = 0;
    uint16_t sequence_ = 0;
    State state_ = State::Idle;
    bool claimed_ = false;
};

} // namespace hostlink::management
