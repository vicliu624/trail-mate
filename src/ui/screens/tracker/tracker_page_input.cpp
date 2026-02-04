#include "tracker_page_input.h"

#include "tracker_state.h"

namespace tracker
{
namespace ui
{
namespace input
{

void bind_focus(lv_group_t* group)
{
    if (!group)
    {
        return;
    }
    auto& state = g_tracker_state;
    if (state.top_bar.back_btn)
    {
        lv_group_add_obj(group, state.top_bar.back_btn);
    }
    if (state.mode_record_btn)
    {
        lv_group_add_obj(group, state.mode_record_btn);
    }
    if (state.mode_route_btn)
    {
        lv_group_add_obj(group, state.mode_route_btn);
    }
}

} // namespace input
} // namespace ui
} // namespace tracker
