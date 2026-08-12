/**
 * @file top_bar_power_presenter.cpp
 * @brief Shared TopBar power presenter with cached, debounced board sampling.
 */

#include "ui/widgets/top_bar_power_presenter.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "platform/ui/device_runtime.h"
#include "sys/clock.h"
#include "ui/formatters.h"

namespace ui
{
namespace widgets
{
namespace top_bar_power
{
namespace
{
constexpr std::size_t kMaxTargets = 8;
constexpr std::size_t kLabelLen = 32;
constexpr uint32_t kRefreshIntervalMs = 1000;
constexpr int kBatteryLevelStepPercent = 1;
constexpr uint8_t kChargingChangeStableSamples = 2;

struct PresenterState
{
    TopBar* targets[kMaxTargets] = {};
    char label[kLabelLen] = "--";
    bool has_label = false;
    bool has_sample_time = false;
    uint32_t last_sample_ms = 0;
    bool has_charging = false;
    bool charging = false;
    bool pending_charging = false;
    uint8_t pending_charging_count = 0;
};

PresenterState& state()
{
    static PresenterState s_state;
    return s_state;
}

bool has_target(const PresenterState& presenter)
{
    for (TopBar* target : presenter.targets)
    {
        if (target != nullptr && target->right_label != nullptr)
        {
            return true;
        }
    }
    return false;
}

bool contains_target(const PresenterState& presenter, TopBar& bar)
{
    for (TopBar* target : presenter.targets)
    {
        if (target == &bar)
        {
            return true;
        }
    }
    return false;
}

void apply_cached_label(PresenterState& presenter)
{
    if (!presenter.has_label)
    {
        return;
    }

    for (TopBar* target : presenter.targets)
    {
        if (target != nullptr && target->right_label != nullptr)
        {
            top_bar_set_right_text_ascii(*target, presenter.label);
        }
    }
}

void observe_charging(PresenterState& presenter, bool charging)
{
    if (!presenter.has_charging)
    {
        presenter.has_charging = true;
        presenter.charging = charging;
        presenter.pending_charging = charging;
        presenter.pending_charging_count = 0;
        return;
    }

    if (charging == presenter.charging)
    {
        presenter.pending_charging = charging;
        presenter.pending_charging_count = 0;
        return;
    }

    if (charging != presenter.pending_charging)
    {
        presenter.pending_charging = charging;
        presenter.pending_charging_count = 1;
        return;
    }

    if (presenter.pending_charging_count < kChargingChangeStableSamples)
    {
        ++presenter.pending_charging_count;
    }
    if (presenter.pending_charging_count >= kChargingChangeStableSamples)
    {
        presenter.charging = charging;
        presenter.pending_charging_count = 0;
    }
}

void sample_and_apply(PresenterState& presenter, uint32_t now_ms)
{
    const platform::ui::device::BatteryInfo info =
        platform::ui::device::battery_info();
    observe_charging(presenter, info.charging);

    char next_label[kLabelLen] = "--";
    ui_format_battery(info.level,
                      presenter.has_charging ? presenter.charging : info.charging,
                      next_label,
                      sizeof(next_label));

    presenter.last_sample_ms = now_ms;
    presenter.has_sample_time = true;
    if (!presenter.has_label || std::strcmp(presenter.label, next_label) != 0)
    {
        std::snprintf(presenter.label, sizeof(presenter.label), "%s", next_label);
        presenter.has_label = true;
        apply_cached_label(presenter);
        return;
    }

    presenter.has_label = true;
    apply_cached_label(presenter);
}

void on_top_bar_deleted(lv_event_t* e)
{
    TopBar* bar = static_cast<TopBar*>(lv_event_get_user_data(e));
    if (bar != nullptr)
    {
        unbind(*bar);
    }
}

} // namespace

void bind(TopBar& bar)
{
    if (bar.right_label == nullptr)
    {
        return;
    }

    PresenterState& presenter = state();
    if (!contains_target(presenter, bar))
    {
        for (TopBar*& target : presenter.targets)
        {
            if (target == nullptr)
            {
                target = &bar;
                if (bar.container != nullptr)
                {
                    lv_obj_add_event_cb(
                        bar.container, on_top_bar_deleted, LV_EVENT_DELETE, &bar);
                }
                break;
            }
        }
    }

    if (presenter.has_label)
    {
        top_bar_set_right_text_ascii(bar, presenter.label);
        return;
    }

    refresh_now();
}

void unbind(TopBar& bar)
{
    PresenterState& presenter = state();
    for (TopBar*& target : presenter.targets)
    {
        if (target == &bar)
        {
            target = nullptr;
        }
    }
}

void tick()
{
    PresenterState& presenter = state();
    if (!has_target(presenter))
    {
        return;
    }

    const uint32_t now_ms = sys::millis_now();
    if (presenter.has_sample_time &&
        static_cast<uint32_t>(now_ms - presenter.last_sample_ms) < kRefreshIntervalMs)
    {
        return;
    }
    sample_and_apply(presenter, now_ms);
}

void refresh_now()
{
    PresenterState& presenter = state();
    if (!has_target(presenter))
    {
        return;
    }
    sample_and_apply(presenter, sys::millis_now());
}

} // namespace top_bar_power
} // namespace widgets
} // namespace ui
