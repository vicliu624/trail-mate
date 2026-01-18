/**
 * @file ui_chat.cpp
 * @brief Chat interface implementation
 */

#include "ui_common.h"
#include "../app/app_context.h"
#include "../ui/ui_controller.h"
#include <memory>

static lv_obj_t *chat_container = NULL;
static std::unique_ptr<chat::ui::UiController> ui_controller = nullptr;

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (obj == chat_container || lv_obj_get_parent(obj) == chat_container) {
        // Return to main menu
        if (chat_container) {
            lv_obj_clean(chat_container);
            lv_obj_del(chat_container);
            chat_container = NULL;
        }
        ui_controller.reset();
        menu_show();
    }
}

void ui_chat_enter(lv_obj_t *parent)
{
    // Get app context
    app::AppContext& ctx = app::AppContext::getInstance();

    extern lv_group_t *app_g;
    if (app_g != NULL) {
        set_default_group(app_g);
    }
    
    // Create container
    chat_container = lv_obj_create(parent);
    lv_obj_set_size(chat_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(chat_container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(chat_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chat_container, 0, 0);
    lv_obj_set_style_pad_all(chat_container, 0, 0);
    lv_obj_set_style_radius(chat_container, 0, 0);
    
    // Create UI controller
    ui_controller = std::make_unique<chat::ui::UiController>(
        chat_container, ctx.getChatService());
    ui_controller->init();
    
    // Store in context (for access from main loop)
    // Note: This is a simplified approach. In a full implementation,
    // the UI controller would be managed by AppContext.
}

void ui_chat_exit(lv_obj_t *parent)
{
    if (chat_container) {
        lv_obj_clean(chat_container);
        lv_obj_del(chat_container);
        chat_container = NULL;
    }
    ui_controller.reset();
}
