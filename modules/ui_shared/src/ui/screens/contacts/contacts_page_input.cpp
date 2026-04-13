/**
 * @file contacts_page_input.cpp
 * @brief Contacts page input handling implementation
 */

#include "ui/screens/contacts/contacts_page_input.h"

#include "ui/components/two_pane_nav.h"
#include "ui/screens/contacts/contacts_state.h"

namespace
{

using Adapter = ::ui::components::two_pane_nav::Adapter;
using BackPlacement = ::ui::components::two_pane_nav::BackPlacement;

static ::ui::components::two_pane_nav::Controller s_controller{};

static int mode_to_index(contacts::ui::ContactsMode mode)
{
    switch (mode)
    {
    case contacts::ui::ContactsMode::Contacts:
        return 0;
    case contacts::ui::ContactsMode::Nearby:
        return 1;
    case contacts::ui::ContactsMode::Ignored:
        return 2;
    case contacts::ui::ContactsMode::Broadcast:
        return 3;
    case contacts::ui::ContactsMode::Team:
        return 4;
    case contacts::ui::ContactsMode::Discover:
        return 5;
    }
    return 0;
}

static lv_obj_t* get_key_target(void* /*ctx*/)
{
    return contacts::ui::g_contacts_state.root
               ? contacts::ui::g_contacts_state.root
               : contacts::ui::g_contacts_state.list_panel;
}

static lv_obj_t* get_top_back_button(void* /*ctx*/)
{
    return contacts::ui::g_contacts_state.top_bar.back_btn;
}

static size_t get_filter_count(void* /*ctx*/)
{
    return 6;
}

static lv_obj_t* get_filter_button(void* /*ctx*/, size_t index)
{
    using namespace contacts::ui;
    switch (index)
    {
    case 0:
        return g_contacts_state.contacts_btn;
    case 1:
        return g_contacts_state.nearby_btn;
    case 2:
        return g_contacts_state.ignored_btn;
    case 3:
        return g_contacts_state.broadcast_btn;
    case 4:
        return g_contacts_state.team_btn;
    case 5:
        return g_contacts_state.discover_btn;
    default:
        return nullptr;
    }
}

static int get_preferred_filter_index(void* /*ctx*/)
{
    return mode_to_index(contacts::ui::g_contacts_state.current_mode);
}

static size_t get_list_count(void* /*ctx*/)
{
    return contacts::ui::g_contacts_state.list_items.size();
}

static lv_obj_t* get_list_button(void* /*ctx*/, size_t index)
{
    auto& items = contacts::ui::g_contacts_state.list_items;
    return (index < items.size()) ? items[index] : nullptr;
}

static int get_preferred_list_index(void* /*ctx*/)
{
    const int selected = contacts::ui::g_contacts_state.selected_index;
    return selected >= 0 ? selected : -1;
}

static lv_obj_t* get_list_back_button(void* /*ctx*/)
{
    return contacts::ui::g_contacts_state.back_btn;
}

static bool handle_list_enter(void* /*ctx*/, lv_obj_t* focused)
{
    using namespace contacts::ui;
    if (!focused || !lv_obj_is_valid(focused))
    {
        return false;
    }

    if (focused == g_contacts_state.prev_btn || focused == g_contacts_state.next_btn)
    {
        lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
        return true;
    }

    for (auto* item : g_contacts_state.list_items)
    {
        if (focused == item)
        {
            lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
            return true;
        }
    }
    return false;
}

static Adapter make_adapter()
{
    Adapter adapter{};
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
    adapter.filter_top_back_placement = BackPlacement::Trailing;
    return adapter;
}

} // namespace

void init_contacts_input()
{
    s_controller.init(make_adapter());
}

void cleanup_contacts_input()
{
    s_controller.cleanup();
}

void contacts_input_on_ui_refreshed()
{
    s_controller.on_ui_refreshed();
}

void contacts_focus_to_filter()
{
    s_controller.focus_filter();
}

void contacts_focus_to_list()
{
    s_controller.focus_list();
}

lv_group_t* contacts_input_get_group()
{
    return s_controller.group();
}
