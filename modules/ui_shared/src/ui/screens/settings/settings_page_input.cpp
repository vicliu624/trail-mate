/**
 * @file settings_page_input.cpp
 * @brief Settings input handling
 */

#include "ui/screens/settings/settings_page_input.h"

#include "ui/presentation/directory_browser_nav.h"
#include "ui/screens/settings/settings_page_components.h"
#include "ui/screens/settings/settings_state.h"

namespace settings
{
namespace ui
{
namespace input
{
namespace
{

using Adapter = ::ui::presentation::directory_browser_nav::Adapter;
using BackPlacement = ::ui::presentation::directory_browser_nav::BackPlacement;

static ::ui::presentation::directory_browser_nav::Controller s_controller{};

static constexpr bool use_touch_first_settings_mode()
{
#if defined(ARDUINO_T_DECK) || defined(ARDUINO_T_DECK_PRO)
    return true;
#else
    return false;
#endif
}

static lv_obj_t* get_key_target(void* /*ctx*/)
{
    return g_state.root ? g_state.root : g_state.list_panel;
}

static lv_obj_t* get_top_back_button(void* /*ctx*/)
{
    return g_state.top_bar.back_btn;
}

static size_t get_selector_count(void* /*ctx*/)
{
    return g_state.filter_count;
}

static lv_obj_t* get_selector_button(void* /*ctx*/, size_t index)
{
    return (index < g_state.filter_count) ? g_state.filter_buttons[index] : nullptr;
}

static int get_preferred_selector_index(void* /*ctx*/)
{
    return g_state.current_category;
}

static size_t get_content_count(void* /*ctx*/)
{
    return g_state.item_count;
}

static lv_obj_t* get_content_button(void* /*ctx*/, size_t index)
{
    return (index < g_state.item_count) ? g_state.item_widgets[index].btn : nullptr;
}

static int get_preferred_content_index(void* /*ctx*/)
{
    return -1;
}

static lv_obj_t* get_content_back_button(void* /*ctx*/)
{
    return g_state.list_back_btn;
}

static bool handle_selector_activate(void* /*ctx*/, lv_obj_t* selector_button)
{
    return settings::ui::components::activate_filter_button(selector_button);
}

static bool handle_content_activate(void* /*ctx*/, lv_obj_t* content_button)
{
    return settings::ui::components::activate_list_button(content_button);
}

static bool handle_content_back_activate(void* /*ctx*/, lv_obj_t* back_button)
{
    return settings::ui::components::activate_list_back_button(back_button);
}

static Adapter make_adapter()
{
    Adapter adapter{};
    adapter.get_key_target = get_key_target;
    adapter.get_top_back_button = get_top_back_button;
    adapter.get_selector_count = get_selector_count;
    adapter.get_selector_button = get_selector_button;
    adapter.get_preferred_selector_index = get_preferred_selector_index;
    adapter.get_content_count = get_content_count;
    adapter.get_content_button = get_content_button;
    adapter.get_preferred_content_index = get_preferred_content_index;
    adapter.get_content_back_button = get_content_back_button;
    if (use_touch_first_settings_mode())
    {
        adapter.handle_selector_activate = handle_selector_activate;
        adapter.handle_content_activate = handle_content_activate;
        adapter.handle_content_back_activate = handle_content_back_activate;
    }
    adapter.selector_top_back_placement = BackPlacement::Leading;
    return adapter;
}

} // namespace

void init()
{
    s_controller.init(make_adapter());
}

void cleanup()
{
    s_controller.cleanup();
}

void on_ui_refreshed()
{
    s_controller.on_ui_refreshed();
}

void focus_to_selector()
{
    s_controller.focus_selector();
}

void focus_to_content()
{
    s_controller.focus_content();
}

lv_group_t* get_group()
{
    return s_controller.group();
}

} // namespace input
} // namespace ui
} // namespace settings
