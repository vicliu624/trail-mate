/**
 * @file ui_chat.cpp
 * @brief Chat interface implementation
 */

#include "ui_common.h"

static lv_obj_t *menu = NULL;

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_button_is_root(menu, obj)) {
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu = NULL;
        menu_show();
    }
}

void ui_chat_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);

    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    // Create a simple label showing "chat"
    lv_obj_t *label = lv_label_create(main_page);
    lv_label_set_text(label, "chat");
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);

    lv_menu_set_page(menu, main_page);
}

void ui_chat_exit(lv_obj_t *parent)
{
    if (menu) {
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu = NULL;
    }
}
