#pragma once

#include <cstdint>

namespace platform::ui::screen_power
{

enum class State : std::uint8_t
{
    Awake,
    Sleeping,
    WakePreview,
};

enum class Event : std::uint8_t
{
    Initialize,
    Tick,
    Input,
    WakeInput,
    ConfirmInput,
    InputRelease,
    Activity,
    ModalWake,
    DisableSleep,
    EnableSleep,
};

struct Effects
{
    bool wake_display = false;
    bool sleep_display = false;
    bool show_saver = false;
    bool hide_saver = false;
    bool notify_wake = false;
    bool show_main_menu = false;
};

struct Snapshot
{
    State state = State::Awake;
    std::uint32_t sleep_disable_depth = 0;
    std::uint32_t last_activity_ms = 0;
    std::uint32_t preview_started_ms = 0;
};

class StateMachine
{
  public:
    static constexpr std::uint32_t kDefaultTimeoutMs = 60000;
    static constexpr std::uint32_t kMinTimeoutMs = 10000;
    static constexpr std::uint32_t kMaxTimeoutMs = 300000;
    // Existing settings/UI encode Always with this value; it is not five minutes.
    static constexpr std::uint32_t kAlwaysOnTimeoutMs = kMaxTimeoutMs;
    static constexpr std::uint32_t kPreviewDurationMs = 3000;

    explicit StateMachine(std::uint32_t timeout_ms = kDefaultTimeoutMs);

    static std::uint32_t clamp_timeout_ms(std::uint32_t timeout_ms);

    void set_timeout_ms(std::uint32_t timeout_ms);
    std::uint32_t timeout_ms() const;

    Effects dispatch(Event event, std::uint32_t now_ms);
    Snapshot snapshot() const;

  private:
    Effects enter_preview(std::uint32_t now_ms);
    Effects enter_awake();

    State state_ = State::Awake;
    std::uint32_t timeout_ms_ = kDefaultTimeoutMs;
    std::uint32_t sleep_disable_depth_ = 0;
    std::uint32_t last_activity_ms_ = 0;
    std::uint32_t preview_started_ms_ = 0;
};

} // namespace platform::ui::screen_power
