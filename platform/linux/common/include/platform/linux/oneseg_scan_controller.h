#pragma once

#include "platform/linux/oneseg_channel_store.h"
#include <cstdint>

namespace platform::linux_runtime
{
// UI-thread adapter. All commands must enqueue work; none may wait for RF/DSP.
class OneSegScanPort
{
  public:
    struct Observation
    {
        bool active = false, restoration_failed = false, no_signal = false;
        bool failed = false, confirmed_media = false;
        unsigned channel = 13, programme = 0;
        std::vector<unsigned> programmes;
        std::string name, message;
    };
    virtual ~OneSegScanPort() = default;
    virtual Observation observe() const = 0;
    virtual bool start(unsigned channel) = 0;
    virtual void stop() = 0;
    virtual bool select(unsigned programme) = 0;
};

// Staged scan transaction. Cancellation/failure/empty results retain saved data.
// The port attests valid received mapping AND decoded media, never merely IQ.
class OneSegScanController
{
  public:
    enum class Phase
    {
        Idle,
        Stopping,
        Receiving,
        Completed,
        Cancelled,
        Error
    };
    struct Config
    {
        unsigned first_channel = 13, last_channel = 62;
        uint64_t acquisition_ms = 8000, programme_ms = 2000;
    };
    struct Snapshot
    {
        Phase phase = Phase::Idle;
        unsigned channel = 13, scanned = 0, total = 0;
        size_t found = 0;
        std::string message;
    };
    OneSegScanController(OneSegScanPort& port, OneSegChannelStore& saved, Config config)
        : port_(port), saved_(saved), config_(config) {}
    ~OneSegScanController()
    {
        if (running()) port_.stop();
    }
    OneSegScanController(const OneSegScanController&) = delete;
    OneSegScanController& operator=(const OneSegScanController&) = delete;
    bool running() const { return state_.phase == Phase::Stopping || state_.phase == Phase::Receiving; }
    const Snapshot& snapshot() const { return state_; }
    bool start()
    {
        if (running()) return false;
        state_ = {};
        staged_.clear();
        visited_.clear();
        ending_ = Phase::Idle;
        advance_ = false;
        requested_ = 0;
        if (config_.first_channel < 13 || config_.last_channel > 62 || config_.first_channel > config_.last_channel ||
            config_.acquisition_ms < 100 || config_.programme_ms < 100)
        {
            state_.phase = Phase::Error;
            state_.message = "Invalid scan configuration";
            return false;
        }
        state_.channel = config_.first_channel;
        state_.total = config_.last_channel - config_.first_channel + 1;
        state_.phase = Phase::Stopping;
        state_.message = "Preparing receiver";
        port_.stop();
        return true;
    }
    void cancel()
    {
        if (running()) finish(Phase::Cancelled, "Scan cancelled; previous channels retained");
    }
    void tick(uint64_t now)
    {
        if (!running()) return;
        const auto observed = port_.observe();
        if (observed.restoration_failed)
        {
            finish(Phase::Error, "Radio restoration failed; previous channels retained");
            if (!observed.active) state_.phase = Phase::Error;
            return;
        }
        if (state_.phase == Phase::Stopping)
        {
            if (observed.active) return;
            if (advance_ && observed.failed && !observed.no_signal)
            {
                state_.phase = Phase::Error;
                state_.message = observed.message.empty() ? "Receiver cleanup failed; previous channels retained" : observed.message;
                return;
            }
            if (ending_ == Phase::Cancelled || ending_ == Phase::Error)
            {
                state_.phase = ending_;
                return;
            }
            if (advance_)
            {
                ++state_.scanned;
                if (state_.channel == config_.last_channel)
                {
                    complete();
                    return;
                }
                ++state_.channel;
            }
            advance_ = false;
            visited_.clear();
            requested_ = 0;
            confirmed_channel_ = false;
            if (!port_.start(state_.channel))
            {
                finish(Phase::Error, "Cannot start scan receiver; previous channels retained");
                return;
            }
            deadline_ = now + config_.acquisition_ms;
            state_.phase = Phase::Receiving;
            state_.message = "Acquiring channel";
            return;
        }
        if (observed.channel != state_.channel)
        {
            finish(Phase::Error, "Receiver channel changed outside scan");
            return;
        }
        if (!observed.active)
        {
            if (observed.no_signal) next();
            else finish(Phase::Error, observed.message.empty() ? "Receiver stopped unexpectedly" : observed.message);
            return;
        }
        if (observed.failed)
        {
            finish(Phase::Error, observed.message.empty() ? "Receiver failed" : observed.message);
            return;
        }
        if (observed.confirmed_media && observed.programme)
        {
            OneSegChannel channel{state_.channel, observed.programme, observed.name};
            if (std::none_of(staged_.begin(), staged_.end(), [&channel](const auto& item)
                             { return channel.sameService(item); }))
            {
                if (staged_.size() >= OneSegChannelStore::capacity)
                {
                    finish(Phase::Error, "Scan service capacity exceeded; previous channels retained");
                    return;
                }
                staged_.push_back(std::move(channel));
                state_.found = staged_.size();
            }
            visit(observed.programme);
            confirmed_channel_ = true;
            if (requested_ == observed.programme) requested_ = 0;
        }
        if (requested_)
        {
            if (now < programme_deadline_) return;
            visit(requested_);
            requested_ = 0;
        }
        if (!confirmed_channel_ && now < deadline_) return;
        for (unsigned programme : observed.programmes)
        {
            if (!programme || std::find(visited_.begin(), visited_.end(), programme) != visited_.end()) continue;
            if (visited_.size() >= 32)
            {
                finish(Phase::Error, "Received programme capacity exceeded");
                return;
            }
            if (port_.select(programme))
            {
                requested_ = programme;
                programme_deadline_ = now + config_.programme_ms;
                state_.message = "Checking received programme";
            }
            else visit(programme); // A received table may remove it between snapshots.
            return;
        }
        next();
    }

  private:
    OneSegScanPort& port_;
    OneSegChannelStore& saved_;
    Config config_;
    Snapshot state_;
    Phase ending_ = Phase::Idle;
    std::vector<OneSegChannel> staged_;
    std::vector<unsigned> visited_;
    bool advance_ = false, confirmed_channel_ = false;
    unsigned requested_ = 0;
    uint64_t deadline_ = 0, programme_deadline_ = 0;
    void visit(unsigned programme)
    {
        if (std::find(visited_.begin(), visited_.end(), programme) == visited_.end()) visited_.push_back(programme);
    }
    void finish(Phase phase, const std::string& message)
    {
        ending_ = phase;
        state_.phase = Phase::Stopping;
        state_.message = message;
        port_.stop();
    }
    void next()
    {
        advance_ = true;
        state_.phase = Phase::Stopping;
        state_.message = "Releasing channel";
        port_.stop();
    }
    void complete()
    {
        state_.phase = Phase::Completed;
        if (staged_.empty()) state_.message = "No supported stations found; previous channels retained";
        else if (!saved_.replaceScan(staged_, state_.message)) state_.phase = Phase::Error;
        else state_.message = "Scan complete; channels saved";
    }
};
} // namespace platform::linux_runtime
