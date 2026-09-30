#include "lvgl.h"
#include <array>
#include <cassert>
#include <cstddef>

struct Item
{
    bool visible;
};
struct CategoryDef
{
    Item* items;
    size_t item_count;
};
Item profile[] = {{true}};
Item mesh[] = {{false}, {false}};
Item reticulum[] = {{true}};
CategoryDef kCategories[] = {{profile, 1}, {mesh, 2}, {reticulum, 1}};
struct
{
    std::array<lv_obj_t*, 3> filter_buttons{};
    size_t filter_count = 3;
    int current_category = 1;
} g_state;
bool should_show_item(const Item& item) { return item.visible; }

// Execute the production visibility update against real LVGL objects.
#include "settings_categories_actual.inc"

int main()
{
    lv_init();
    auto* display = lv_display_create(320, 240);
    auto* root = lv_obj_create(nullptr);
    for (auto& button : g_state.filter_buttons) button = lv_button_create(root);
    update_filter_styles();
    assert(lv_obj_has_flag(g_state.filter_buttons[1], LV_OBJ_FLAG_HIDDEN));
    assert(g_state.current_category == 0);
    assert(lv_obj_has_state(g_state.filter_buttons[0], LV_STATE_CHECKED));

    mesh[1].visible = true;
    update_filter_styles();
    assert(!lv_obj_has_flag(g_state.filter_buttons[1], LV_OBJ_FLAG_HIDDEN));
    g_state.current_category = 1;
    update_filter_styles();
    assert(lv_obj_has_state(g_state.filter_buttons[1], LV_STATE_CHECKED));

    mesh[1].visible = false;
    update_filter_styles();
    assert(g_state.current_category == 0);
    assert(!lv_obj_has_state(g_state.filter_buttons[1], LV_STATE_CHECKED));
    assert(!lv_obj_has_flag(g_state.filter_buttons[2], LV_OBJ_FLAG_HIDDEN));
    lv_obj_delete(root);
    lv_display_delete(display);
    lv_deinit();
}
