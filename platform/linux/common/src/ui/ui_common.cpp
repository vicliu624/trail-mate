#include "ui/ui_common.h"

#include "platform/ui/time_runtime.h"

namespace
{

void noop_top_bar_battery_update()
{
}

} // namespace

void ui_update_top_bar_battery(ui::widgets::TopBar& bar)
{
    (void)bar;
    noop_top_bar_battery_update();
}

int ui_get_timezone_offset_min()
{
    return ::platform::ui::time::timezone_offset_min();
}

void ui_set_timezone_offset_min(int offset_min)
{
    ::platform::ui::time::set_timezone_offset_min(offset_min);
}

time_t ui_apply_timezone_offset(time_t utc_seconds)
{
    return ::platform::ui::time::apply_timezone_offset(utc_seconds);
}

bool ui_take_screenshot_to_sd()
{
    return false;
}
