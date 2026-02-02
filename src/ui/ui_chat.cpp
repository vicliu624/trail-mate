/**
 * @file ui_chat.cpp
 * @brief Chat interface implementation
 */

#include "../app/app_context.h"
#include "../ui/ui_controller.h"
#include "ui_common.h"
#include <memory>

static lv_obj_t* chat_container = NULL;
static std::unique_ptr<chat::ui::UiController> ui_controller = nullptr;

lv_obj_t* ui_chat_get_container()
{
    return chat_container;
}

static void back_event_handler(lv_event_t* e)
{
    lv_obj_t* obj = (lv_obj_t*)lv_event_get_target(e);
    if (obj == chat_container || lv_obj_get_parent(obj) == chat_container)
    {
        ui_request_exit_to_menu();
    }
}

void ui_chat_enter(lv_obj_t* parent)
{
    // Get app context
    app::AppContext& ctx = app::AppContext::getInstance();

    extern lv_group_t* app_g;
    if (app_g != NULL)
    {
        set_default_group(app_g);
    }

    Serial.printf("[UI Chat] enter: parent=%p active=%p\n",
                  parent, lv_screen_active());
    if (lv_obj_t* active = lv_screen_active())
    {
        Serial.printf("[UI Chat] active child count=%u\n",
                      (unsigned)lv_obj_get_child_cnt(active));
    }

    // Create container
    chat_container = lv_obj_create(parent);
    lv_obj_set_size(chat_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(chat_container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(chat_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chat_container, 0, 0);
    lv_obj_set_style_pad_all(chat_container, 0, 0);
    lv_obj_set_style_radius(chat_container, 0, 0);

    Serial.printf("[UI Chat] container=%p valid=%d\n",
                  chat_container,
                  chat_container ? (lv_obj_is_valid(chat_container) ? 1 : 0) : 0);
    if (chat_container)
    {
        Serial.printf("[UI Chat] container child count=%u\n",
                      (unsigned)lv_obj_get_child_cnt(chat_container));
    }

    // Create UI controller
    ui_controller = std::unique_ptr<chat::ui::UiController>(
        new chat::ui::UiController(chat_container, ctx.getChatService()));
    ui_controller->init();

    // Store in context (for access from main loop)
    // Note: This is a simplified approach. In a full implementation,
    // the UI controller would be managed by AppContext.
}

void ui_chat_exit(lv_obj_t* parent)
{
    Serial.printf("[UI Chat] exit: parent=%p container=%p\n",
                  parent, chat_container);
    if (lv_obj_t* active = lv_screen_active())
    {
        Serial.printf("[UI Chat] exit active child count=%u\n",
                      (unsigned)lv_obj_get_child_cnt(active));
    }
    if (chat_container && lv_obj_is_valid(chat_container))
    {
        Serial.printf("[UI Chat] exit container child count=%u\n",
                      (unsigned)lv_obj_get_child_cnt(chat_container));
    }

    // Destroy controller first so child screens can clean up safely.
    ui_controller.reset();

    if (chat_container && !lv_obj_is_valid(chat_container))
    {
        chat_container = NULL;
    }
    if (chat_container)
    {
        lv_obj_del(chat_container);
        chat_container = NULL;
    }
}
