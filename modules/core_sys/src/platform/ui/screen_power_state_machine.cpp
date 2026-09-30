#include "platform/ui/screen_power_state_machine.h"

namespace platform::ui::screen_power
{

StateMachine::StateMachine(std::uint32_t timeout_ms)
    : timeout_ms_(clamp_timeout_ms(timeout_ms))
{
}

std::uint32_t StateMachine::clamp_timeout_ms(std::uint32_t timeout_ms)
{
    if (timeout_ms < kMinTimeoutMs)
    {
        return kDefaultTimeoutMs;
    }
    if (timeout_ms > kMaxTimeoutMs)
    {
        return kMaxTimeoutMs;
    }
    return timeout_ms;
}

void StateMachine::set_timeout_ms(std::uint32_t timeout_ms)
{
    timeout_ms_ = clamp_timeout_ms(timeout_ms);
}

std::uint32_t StateMachine::timeout_ms() const
{
    return timeout_ms_;
}

Effects StateMachine::enter_preview(std::uint32_t now_ms)
{
    state_ = State::WakePreview;
    preview_started_ms_ = now_ms;
    Effects effects{};
    effects.wake_display = true;
    effects.show_saver = true;
    effects.notify_wake = true;
    return effects;
}

Effects StateMachine::enter_awake()
{
    const bool was_sleeping = state_ == State::Sleeping;
    state_ = State::Awake;
    preview_started_ms_ = 0;
    Effects effects{};
    effects.wake_display = was_sleeping;
    effects.hide_saver = true;
    return effects;
}

Effects StateMachine::dispatch(Event event, std::uint32_t now_ms)
{
    switch (event)
    {
    case Event::Initialize:
        state_ = State::Awake;
        sleep_disable_depth_ = 0;
        last_activity_ms_ = now_ms;
        preview_started_ms_ = 0;
        return {};

    case Event::Tick:
        if (timeout_ms_ == kAlwaysOnTimeoutMs)
        {
            return state_ == State::Awake ? Effects{} : enter_awake();
        }
        if (sleep_disable_depth_ != 0)
        {
            return {};
        }
        if (state_ == State::WakePreview &&
            now_ms - preview_started_ms_ >= kPreviewDurationMs)
        {
            state_ = State::Sleeping;
            preview_started_ms_ = 0;
            Effects effects{};
            effects.sleep_display = true;
            effects.hide_saver = true;
            return effects;
        }
        if (state_ == State::Awake &&
            now_ms - last_activity_ms_ >= timeout_ms_)
        {
            state_ = State::Sleeping;
            Effects effects{};
            effects.sleep_display = true;
            return effects;
        }
        return {};

    case Event::Input:
        return dispatch(Event::WakeInput, now_ms);

    case Event::InputRelease:
        return {};

    case Event::WakeInput:
        last_activity_ms_ = now_ms;
        if (state_ == State::Sleeping)
        {
            return enter_preview(now_ms);
        }
        if (state_ == State::WakePreview)
        {
            preview_started_ms_ = now_ms;
        }
        return {};

    case Event::ConfirmInput:
        last_activity_ms_ = now_ms;
        if (state_ == State::Sleeping || state_ == State::WakePreview)
        {
            // The saver is an overlay. Returning from it must preserve the
            // page and focus that were active when the screen went idle.
            return enter_awake();
        }
        return {};

    case Event::Activity:
        last_activity_ms_ = now_ms;
        if (state_ == State::Sleeping)
        {
            return enter_preview(now_ms);
        }
        return {};

    case Event::ModalWake:
        last_activity_ms_ = now_ms;
        return enter_awake();

    case Event::DisableSleep:
        if (sleep_disable_depth_ < UINT32_MAX)
        {
            ++sleep_disable_depth_;
        }
        last_activity_ms_ = now_ms;
        if (state_ != State::Awake)
        {
            return enter_awake();
        }
        return {};

    case Event::EnableSleep:
        if (sleep_disable_depth_ > 0)
        {
            --sleep_disable_depth_;
        }
        last_activity_ms_ = now_ms;
        return {};
    }

    return {};
}

Snapshot StateMachine::snapshot() const
{
    Snapshot snapshot{};
    snapshot.state = state_;
    snapshot.sleep_disable_depth = sleep_disable_depth_;
    snapshot.last_activity_ms = last_activity_ms_;
    snapshot.preview_started_ms = preview_started_ms_;
    return snapshot;
}

} // namespace platform::ui::screen_power
