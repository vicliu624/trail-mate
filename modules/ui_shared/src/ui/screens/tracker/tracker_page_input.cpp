#include "ui/screens/tracker/tracker_page_input.h"

#include "ui/components/two_pane_nav.h"
#include "ui/screens/tracker/tracker_state.h"

namespace
{

using Adapter = ::ui::components::two_pane_nav::Adapter;
using BackPlacement = ::ui::components::two_pane_nav::BackPlacement;
using NavFocusColumn = ::ui::components::two_pane_nav::FocusColumn;

static ::ui::components::two_pane_nav::Controller s_controller{};
static constexpr intptr_t kBackListItemUserData = -2;
static constexpr intptr_t kListUserDataOffset = 1;

static bool is_alive(void* /*ctx*/)
{
    auto& state = tracker::ui::g_tracker_state;
    return state.root && lv_obj_is_valid(state.root);
}

static lv_obj_t* get_key_target(void* /*ctx*/)
{
    auto& state = tracker::ui::g_tracker_state;
    return state.root ? state.root : state.list_panel;
}

static lv_obj_t* get_top_back_button(void* /*ctx*/)
{
    return tracker::ui::g_tracker_state.top_bar.back_btn;
}

static size_t get_filter_count(void* /*ctx*/)
{
    return 2;
}

static lv_obj_t* get_filter_button(void* /*ctx*/, size_t index)
{
    auto& state = tracker::ui::g_tracker_state;
    switch (index)
    {
    case 0:
        return state.mode_record_btn;
    case 1:
        return state.mode_route_btn;
    default:
        return nullptr;
    }
}

static int get_preferred_filter_index(void* /*ctx*/)
{
    return tracker::ui::g_tracker_state.mode == tracker::ui::TrackerPageState::Mode::Record ? 0 : 1;
}

static bool is_focusable_list_child(lv_obj_t* obj)
{
    if (!obj || !lv_obj_is_valid(obj))
    {
        return false;
    }
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) || lv_obj_has_state(obj, LV_STATE_DISABLED))
    {
        return false;
    }
    const intptr_t raw = reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj));
    return raw != kBackListItemUserData;
}

static lv_obj_t* get_list_back_button(void* /*ctx*/)
{
    auto& state = tracker::ui::g_tracker_state;
    if (!state.list_container)
    {
        return nullptr;
    }
    const uint32_t child_count = lv_obj_get_child_count(state.list_container);
    for (uint32_t index = 0; index < child_count; ++index)
    {
        lv_obj_t* child = lv_obj_get_child(state.list_container, index);
        if (!child || !lv_obj_is_valid(child))
        {
            continue;
        }
        const intptr_t raw = reinterpret_cast<intptr_t>(lv_obj_get_user_data(child));
        if (raw == kBackListItemUserData)
        {
            return child;
        }
    }
    return nullptr;
}

static bool include_start_stop_button()
{
    auto& state = tracker::ui::g_tracker_state;
    if (state.mode != tracker::ui::TrackerPageState::Mode::Record)
    {
        return false;
    }
    if (!state.start_stop_btn || !lv_obj_is_valid(state.start_stop_btn))
    {
        return false;
    }
    if (lv_obj_has_flag(state.start_stop_btn, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_state(state.start_stop_btn, LV_STATE_DISABLED))
    {
        return false;
    }
    return true;
}

static size_t get_list_count(void* /*ctx*/)
{
    auto& state = tracker::ui::g_tracker_state;
    size_t count = 0;
    if (state.list_container)
    {
        const uint32_t child_count = lv_obj_get_child_count(state.list_container);
        for (uint32_t index = 0; index < child_count; ++index)
        {
            if (is_focusable_list_child(lv_obj_get_child(state.list_container, index)))
            {
                ++count;
            }
        }
    }
    if (include_start_stop_button())
    {
        ++count;
    }
    return count;
}

static lv_obj_t* get_list_button(void* /*ctx*/, size_t index)
{
    auto& state = tracker::ui::g_tracker_state;
    size_t current = 0;
    if (state.list_container)
    {
        const uint32_t child_count = lv_obj_get_child_count(state.list_container);
        for (uint32_t child_index = 0; child_index < child_count; ++child_index)
        {
            lv_obj_t* child = lv_obj_get_child(state.list_container, child_index);
            if (!is_focusable_list_child(child))
            {
                continue;
            }
            if (current == index)
            {
                return child;
            }
            ++current;
        }
    }
    if (include_start_stop_button() && current == index)
    {
        return state.start_stop_btn;
    }
    return nullptr;
}

static int get_preferred_list_index(void* /*ctx*/)
{
    auto& state = tracker::ui::g_tracker_state;
    if (state.mode == tracker::ui::TrackerPageState::Mode::Record)
    {
        return state.selected_record_idx >= 0 ? state.selected_record_idx : -1;
    }
    return state.selected_route_idx >= 0 ? state.selected_route_idx : -1;
}

static bool handle_list_enter(void* /*ctx*/, lv_obj_t* focused)
{
    auto& state = tracker::ui::g_tracker_state;
    if (!focused || !lv_obj_is_valid(focused))
    {
        return false;
    }
    if (focused == state.start_stop_btn)
    {
        lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
        return true;
    }
    if (state.list_container)
    {
        const uint32_t child_count = lv_obj_get_child_count(state.list_container);
        for (uint32_t index = 0; index < child_count; ++index)
        {
            if (focused == lv_obj_get_child(state.list_container, index))
            {
                lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
                return true;
            }
        }
    }
    return false;
}

static void on_column_changed(void* /*ctx*/, NavFocusColumn column)
{
    tracker::ui::g_tracker_state.focus_col =
        (column == NavFocusColumn::Filter)
            ? tracker::ui::TrackerPageState::FocusColumn::Mode
            : tracker::ui::TrackerPageState::FocusColumn::Main;
}

static Adapter make_adapter()
{
    Adapter adapter{};
    adapter.is_alive = is_alive;
    adapter.get_key_target = get_key_target;
    adapter.get_top_back_button = get_top_back_button;
    adapter.get_filter_count = get_filter_count;
    adapter.get_filter_button = get_filter_button;
    adapter.get_preferred_filter_index = get_preferred_filter_index;
    adapter.get_list_count = get_list_count;
    adapter.get_list_button = get_list_button;
    adapter.get_preferred_list_index = get_preferred_list_index;
    adapter.get_list_back_button = get_list_back_button;
    adapter.handle_list_enter = handle_list_enter;
    adapter.on_column_changed = on_column_changed;
    adapter.filter_top_back_placement = BackPlacement::Trailing;
    return adapter;
}

} // namespace

namespace tracker
{
namespace ui
{
namespace input
{

void init_tracker_input()
{
    s_controller.init(make_adapter());
}

void cleanup_tracker_input()
{
    s_controller.cleanup();
}

void tracker_input_on_ui_refreshed()
{
    s_controller.on_ui_refreshed();
}

void tracker_focus_to_filter()
{
    s_controller.focus_filter();
}

void tracker_focus_to_list()
{
    s_controller.focus_list();
}

lv_group_t* tracker_input_get_group()
{
    return s_controller.group();
}

} // namespace input
} // namespace ui
} // namespace tracker
