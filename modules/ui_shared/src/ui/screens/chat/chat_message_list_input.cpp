#if !defined(ARDUINO_T_WATCH_S3)
#include "ui/screens/chat/chat_message_list_input.h"

#include "ui/presentation/directory_browser_nav.h"
#include "ui/screens/chat/chat_message_list_components.h"

namespace chat
{
namespace ui
{
namespace message_list
{
namespace input
{
namespace
{

using BackPlacement = ::ui::presentation::directory_browser_nav::BackPlacement;
using Adapter = ::ui::presentation::directory_browser_nav::Adapter;

static bool screen_alive(void* ctx)
{
    auto* screen = static_cast<ChatMessageListScreen*>(ctx);
    return screen && screen->isAlive();
}

static lv_obj_t* get_key_target(void* ctx)
{
    auto* screen = static_cast<ChatMessageListScreen*>(ctx);
    return screen ? screen->getObj() : nullptr;
}

static lv_obj_t* get_top_back_button(void* ctx)
{
    auto* screen = static_cast<ChatMessageListScreen*>(ctx);
    return screen ? screen->getBackButton() : nullptr;
}

static size_t get_selector_count(void* /*ctx*/)
{
    return 3;
}

static lv_obj_t* get_selector_button(void* ctx, size_t index)
{
    auto* screen = static_cast<ChatMessageListScreen*>(ctx);
    if (!screen) return nullptr;
    switch (index)
    {
    case 0:
        return screen->getDirectButton();
    case 1:
        return screen->getBroadcastButton();
    case 2:
        return screen->getTeamButton();
    default:
        return nullptr;
    }
}

static int get_preferred_selector_index(void* ctx)
{
    auto* screen = static_cast<ChatMessageListScreen*>(ctx);
    if (!screen) return -1;
    if (lv_obj_t* btn = screen->getDirectButton())
    {
        if (lv_obj_has_state(btn, LV_STATE_CHECKED)) return 0;
    }
    if (lv_obj_t* btn = screen->getBroadcastButton())
    {
        if (lv_obj_has_state(btn, LV_STATE_CHECKED)) return 1;
    }
    if (lv_obj_t* btn = screen->getTeamButton())
    {
        if (lv_obj_has_state(btn, LV_STATE_CHECKED)) return 2;
    }
    return 0;
}

static size_t get_content_count(void* ctx)
{
    auto* screen = static_cast<ChatMessageListScreen*>(ctx);
    return screen ? screen->getItemCount() : 0;
}

static lv_obj_t* get_content_button(void* ctx, size_t index)
{
    auto* screen = static_cast<ChatMessageListScreen*>(ctx);
    return screen ? screen->getItemButton(index) : nullptr;
}

static int get_preferred_content_index(void* ctx)
{
    auto* screen = static_cast<ChatMessageListScreen*>(ctx);
    return screen ? screen->getSelectedIndex() : -1;
}

static lv_obj_t* get_content_back_button(void* ctx)
{
    auto* screen = static_cast<ChatMessageListScreen*>(ctx);
    return screen ? screen->getListBackButton() : nullptr;
}

static Adapter make_adapter(ChatMessageListScreen* screen)
{
    Adapter adapter{};
    adapter.ctx = screen;
    adapter.is_alive = screen_alive;
    adapter.get_key_target = get_key_target;
    adapter.get_top_back_button = get_top_back_button;
    adapter.get_selector_count = get_selector_count;
    adapter.get_selector_button = get_selector_button;
    adapter.get_preferred_selector_index = get_preferred_selector_index;
    adapter.get_content_count = get_content_count;
    adapter.get_content_button = get_content_button;
    adapter.get_preferred_content_index = get_preferred_content_index;
    adapter.get_content_back_button = get_content_back_button;
    adapter.selector_top_back_placement = BackPlacement::Leading;
    return adapter;
}

} // namespace

void init(ChatMessageListScreen* screen, Controller* controller)
{
    if (!controller) return;
    controller->init(make_adapter(screen));
}

void cleanup(Controller* controller)
{
    if (!controller) return;
    controller->cleanup();
}

void on_ui_refreshed(Controller* controller)
{
    if (!controller) return;
    controller->on_ui_refreshed();
}

void focus_selector(Controller* controller)
{
    if (!controller) return;
    controller->focus_selector();
}

void focus_content(Controller* controller)
{
    if (!controller) return;
    controller->focus_content();
}

} // namespace input
} // namespace message_list
} // namespace ui
} // namespace chat

#endif
