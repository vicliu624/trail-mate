#include "ui/menu/dashboard/dashboard_state.h"

namespace ui::menu::dashboard
{
namespace
{

DashboardUi s_dashboard{};

} // namespace

DashboardUi& dashboard_state()
{
    return s_dashboard;
}

void dashboard_reset()
{
    if (s_dashboard.timer != nullptr)
    {
        lv_timer_del(s_dashboard.timer);
        s_dashboard.timer = nullptr;
    }
    s_dashboard = DashboardUi{};
}

} // namespace ui::menu::dashboard
