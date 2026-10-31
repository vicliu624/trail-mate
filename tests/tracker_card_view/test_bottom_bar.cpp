#include "ui/components/bottom_bar.h"
#include "ui/components/two_pane_nav.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include <cassert>
#include <cstdio>
#include <vector>

namespace ui::page_profile
{
const PageLayoutProfile& current()
{
    static PageLayoutProfile profile{};
    return profile;
}
const lv_font_t* resolve_body_font() { return &lv_font_montserrat_14; }
} // namespace ui::page_profile
namespace ui::i18n
{
void set_label_text(lv_obj_t* label, const char* text) { lv_label_set_text(label, text); }
} // namespace ui::i18n
void set_default_group(lv_group_t* group) { lv_group_set_default(group); }

namespace
{
struct Page
{
    lv_obj_t* root;
    lv_obj_t* filters[3];
    lv_obj_t* item;
    lv_obj_t* back;
    int activations = 0;
    int selected = 0;
};
int info_builds = 0;
void info(lv_obj_t* parent)
{
    ++info_builds;
    lv_label_set_text(lv_label_create(parent), "Radio status and modem preset");
}
} // namespace

int main()
{
    lv_init();
    for (const auto width : {320, 480, 240})
    {
        auto* display = lv_display_create(width, width == 320 ? 170 : (width == 480 ? 222 : 240));
        std::vector<uint16_t> pixels(width * 240);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*)
                                { lv_display_flush_ready(d); });
        Page page{};
        page.root = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(page.root);
        lv_obj_set_size(page.root, LV_PCT(100), LV_PCT(100));
        lv_obj_set_flex_flow(page.root, LV_FLEX_FLOW_COLUMN);
        auto* row = lv_obj_create(page.root);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 0);
        lv_obj_set_flex_grow(row, 1);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        auto* filters = lv_obj_create(row);
        lv_obj_set_size(filters, 90, LV_PCT(100));
        lv_obj_set_flex_flow(filters, LV_FLEX_FLOW_COLUMN);
        const char* names[] = {"Tracks", "Routes", "Nav"};
        for (unsigned i = 0; i < 3; ++i)
        {
            page.filters[i] = ui::components::bottom_bar::add_button(filters, names[i]);
            lv_obj_set_height(page.filters[i], 28);
        }
        auto* main = lv_obj_create(row);
        lv_obj_remove_style_all(main);
        lv_obj_set_size(main, 0, LV_PCT(100));
        lv_obj_set_flex_grow(main, 1);
        page.item = lv_btn_create(main);
        page.back = lv_btn_create(page.root);
        lv_obj_add_flag(page.back, LV_OBJ_FLAG_HIDDEN);
        ui::components::two_pane_nav::Adapter adapter{};
        adapter.ctx = &page;
        adapter.filter_panel = filters;
        adapter.build_info = info;
        adapter.get_key_target = [](void* p)
        { return static_cast<Page*>(p)->root; };
        adapter.get_filter_count = [](void*) -> size_t
        { return 3; };
        adapter.get_filter_button = [](void* p, size_t i)
        { return static_cast<Page*>(p)->filters[i]; };
        adapter.get_preferred_filter_index = [](void* p)
        { return static_cast<Page*>(p)->selected; };
        adapter.get_list_count = [](void*) -> size_t
        { return 1; };
        adapter.get_list_button = [](void* p, size_t)
        { return static_cast<Page*>(p)->item; };
        adapter.handle_filter_activate = [](void* p, lv_obj_t* button)
        {
            auto& page = *static_cast<Page*>(p);
            for (int i = 0; i < 3; ++i)
                if (page.filters[i] == button) page.selected = i;
            ++page.activations;
            return true;
        };
        ui::components::two_pane_nav::Controller controller;
        controller.init(adapter);
        auto& menu = controller.raw_binding()->menu;
        lv_obj_update_layout(page.root);
        assert(lv_obj_get_width(main) == width);
        assert(!menu.is_open() && lv_obj_has_flag(filters, LV_OBJ_FLAG_HIDDEN));
        const auto retained = lv_obj_get_child_count(page.root);
        lv_obj_send_event(menu.button(), LV_EVENT_CLICKED, nullptr);
        assert(menu.is_open() && lv_obj_get_width(filters) < 180);
        lv_area_t area{}, bar{};
        lv_obj_get_coords(filters, &area);
        lv_obj_get_coords(menu.button(), &bar);
        assert(area.x1 >= 0 && area.x2 < width && area.y2 < bar.y1);
        assert(page.activations == 0);
        uint32_t down = LV_KEY_DOWN;
        lv_obj_send_event(page.filters[0], LV_EVENT_KEY, &down);
        assert(lv_group_get_focused(controller.group()) == page.filters[1]);
        uint32_t up = LV_KEY_UP;
        lv_obj_send_event(page.filters[1], LV_EVENT_KEY, &up);
        assert(lv_group_get_focused(controller.group()) == page.filters[0]);
        lv_obj_send_event(page.filters[1], LV_EVENT_CLICKED, nullptr);
        assert(page.activations == 1 && page.selected == 1 && !menu.is_open());
        assert(lv_obj_get_width(main) == width);
        controller.focus_filter();
        assert(menu.is_open() && lv_group_get_focused(controller.group()) == page.filters[1]);
        // Long translations grow within the viewport instead of clipping offscreen.
        lv_label_set_text(lv_obj_get_child(page.filters[0], 0), "Navigation auf der Karte laden und anzeigen");
        menu.close();
        menu.open();
        lv_obj_update_layout(filters);
        assert(lv_obj_get_width(filters) <= width - 8);
        const auto previous_info_builds = info_builds;
        lv_obj_send_event(controller.raw_binding()->info_button, LV_EVENT_CLICKED, nullptr);
        assert(menu.in_submenu() && info_builds == previous_info_builds + 1);
        controller.on_ui_refreshed();
        assert(menu.in_submenu());
        assert(menu.back() && !menu.in_submenu() && menu.is_open());
        uint32_t escape = LV_KEY_ESC;
        lv_obj_send_event(page.filters[1], LV_EVENT_KEY, &escape);
        assert(!menu.is_open());
        assert(lv_obj_get_child_count(page.root) == retained);
        assert(page.activations == 1);
        controller.cleanup();
        lv_obj_del(page.root);
        lv_display_delete(display);
        std::printf("%dpx: full-width content, touch filter, focus, Info/back, close and object reuse passed.\n", width);
    }
}
