#include "ui/screens/chat/chat_page_runtime.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "ui/app_runtime.h"
#include "ui/screens/chat/chat_ui_controller.h"
#include "ui/ui_common.h"
#include "ui/ui_theme.h"

#include <memory>

namespace
{

const chat::ui::shell::Host* s_host = nullptr;
lv_obj_t* s_chat_container = nullptr;
std::unique_ptr<chat::ui::UiController> s_ui_controller = nullptr;

void request_shell_exit(void*)
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

} // namespace

namespace chat::ui::runtime
{

bool is_available()
{
    return app::hasAppFacade();
}

lv_obj_t* get_container()
{
    return s_chat_container;
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    if (!is_available())
    {
        return;
    }

    s_host = host;

    if (s_chat_container && lv_obj_is_valid(s_chat_container))
    {
        lv_obj_del(s_chat_container);
    }
    s_chat_container = nullptr;
    s_ui_controller.reset();

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    s_chat_container = lv_obj_create(parent);
    lv_obj_set_size(s_chat_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_chat_container, ::ui::theme::page_bg(), 0);
    lv_obj_set_style_bg_opa(s_chat_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chat_container, 0, 0);
    lv_obj_set_style_pad_all(s_chat_container, 0, 0);
    lv_obj_set_style_radius(s_chat_container, 0, 0);

    if (app_g != nullptr)
    {
        set_default_group(app_g);
    }
    else
    {
        set_default_group(prev_group);
    }

    chat::ChannelId default_channel = (app::configFacade().getConfig().chat_channel == 1)
                                          ? chat::ChannelId::SECONDARY
                                          : chat::ChannelId::PRIMARY;
    s_ui_controller = std::unique_ptr<chat::ui::UiController>(
        new chat::ui::UiController(s_chat_container,
                                   app::messagingFacade().getChatService(),
                                   default_channel,
                                   request_shell_exit,
                                   nullptr));
    s_ui_controller->init();

    app::runtimeFacade().setChatUiRuntime(s_ui_controller.get());
}

void exit(lv_obj_t* parent)
{
    (void)parent;

    if (!is_available())
    {
        s_host = nullptr;
        return;
    }

    app::runtimeFacade().setChatUiRuntime(nullptr);
    s_ui_controller.reset();

    if (s_chat_container && !lv_obj_is_valid(s_chat_container))
    {
        s_chat_container = nullptr;
    }
    if (s_chat_container)
    {
        lv_obj_del(s_chat_container);
        s_chat_container = nullptr;
    }

    s_host = nullptr;
}

} // namespace chat::ui::runtime
