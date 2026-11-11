#include "ui/app_runtime.h"
#include "ui/page/page_profile.h"
#include "ui/screens/team/team_page_input.h"
#include "ui/screens/team/team_page_layout.h"
#include <cassert>
#include <cstdio>
#include <vector>

lv_group_t* app_g = nullptr;
void set_default_group(lv_group_t* group) { lv_group_set_default(group); }
namespace ui::page_profile
{
const PageLayoutProfile& current()
{
    static PageLayoutProfile profile{};
    return profile;
}
lv_coord_t resolve_control_button_height() { return 20; }
} // namespace ui::page_profile

int main()
{
    lv_init();
    for (const int width : {320, 480, 1232})
    {
        const int height = width == 320 ? 170 : (width == 480 ? 222 : 568);
        auto* display = lv_display_create(width, height);
        std::vector<uint16_t> pixels(width * height);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*)
                                { lv_display_flush_ready(d); });
        auto* root = team::ui::layout::create_root(lv_screen_active());
        lv_obj_set_style_pad_all(root, 0, 0);
        lv_obj_set_style_border_width(root, 0, 0);
        auto* header = team::ui::layout::create_header(root);
        auto* content = team::ui::layout::create_content(root);
        lv_obj_set_style_pad_all(content, 0, 0);
        lv_obj_set_style_border_width(content, 0, 0);
        auto* body = team::ui::layout::create_body(content);
        auto* actions = team::ui::layout::create_actions(content);
        lv_obj_set_style_pad_all(actions, 0, 0);
        lv_obj_set_style_border_width(actions, 0, 0);
        lv_obj_t* last = nullptr;
        for (int i = 0; i < 40; ++i)
        {
            last = lv_label_create(body);
            lv_label_set_text_fmt(last, "Member %d: retained roster and recent activity", i);
        }
        ui::widgets::TopBar top_bar{};
        top_bar.back_btn = lv_btn_create(header);
        std::vector<lv_obj_t*> buttons{lv_btn_create(actions), lv_btn_create(actions)};
        auto* default_focus = buttons[0];
        app_g = lv_group_create();
        team::ui::TeamPageInputContext context{root, body, &top_bar, &buttons, &default_focus};
        team::ui::init_team_input(context);
        lv_obj_update_layout(root);
        assert(lv_group_get_focused(app_g) == body && lv_group_get_editing(app_g));
        lv_area_t footer{};
        lv_obj_get_coords(actions, &footer);
        assert(footer.y1 >= 0 && footer.y2 < height);
        const auto callbacks = lv_obj_get_event_count(body);
        for (int i = 0; i < 8; ++i) team::ui::refresh_team_input(context);
        assert(lv_obj_get_event_count(body) == callbacks);
        for (int i = 0; i < 80; ++i) lv_group_send_data(app_g, LV_KEY_DOWN);
        assert(lv_obj_get_scroll_y(body) > 0 && lv_obj_get_scroll_bottom(body) == 0);
        lv_area_t bottom{}, viewport{};
        lv_obj_get_coords(last, &bottom);
        lv_obj_get_coords(body, &viewport);
        assert(bottom.y2 <= viewport.y2 && bottom.y1 >= viewport.y1);
        lv_obj_get_coords(actions, &bottom);
        assert(bottom.y1 == footer.y1 && bottom.y2 == footer.y2);
        for (int i = 0; i < 80; ++i) lv_group_send_data(app_g, LV_KEY_UP);
        assert(lv_obj_get_scroll_y(body) == 0);
        lv_group_focus_next(app_g);
        assert(lv_group_get_focused(app_g) == buttons[0] && !lv_group_get_editing(app_g));
        team::ui::cleanup_team_input();
        lv_group_delete(app_g);
        app_g = nullptr;
        lv_obj_delete(root);
        lv_display_delete(display);
        std::printf("%dx%d: full content reachable, fixed footer, keyboard reading and action focus passed.\n", width, height);
    }
}
