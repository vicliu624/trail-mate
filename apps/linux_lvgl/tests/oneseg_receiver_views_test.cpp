#include "platform/desktop/sdl_lvgl_host.h"
#include "ui/oneseg_receiver_views.h"
#include <cassert>
#include <iostream>

using namespace platform::linux_runtime;

static void layout()
{
    lv_tick_inc(10);
    lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
}

int main()
{
    lv_init();
    platform::desktop::DeviceOptions options;
    options.ux_pack = "t_display_p4_touch";
    options.width = 1232;
    options.height = 568;
    platform::desktop::SdlLvglHost host(options);
    auto* group = lv_group_create();
    auto* root = lv_obj_create(lv_screen_active());
    lv_obj_set_size(root, 1232, 568);
    lv_obj_set_style_pad_all(root, 8, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    static const int32_t columns[]{320, 350, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[]{270, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(root, columns, rows);
    lv_obj_t* panels[5]{};
    for (auto*& panel : panels)
    {
        panel = lv_obj_create(root);
        lv_obj_set_style_pad_all(panel, 6, 0);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_grid_cell(panels[0], LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(panels[1], LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(panels[2], LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(panels[3], LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_cell(panels[4], LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    auto* fields = lv_obj_create(panels[1]);
    lv_obj_set_width(fields, lv_pct(100));
    lv_obj_set_height(fields, 154);
    lv_obj_set_style_pad_all(fields, 0, 0);
    lv_obj_set_style_border_width(fields, 0, 0);
    lv_obj_set_flex_flow(fields, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(fields, 3, 0);
    lv_obj_set_style_pad_row(fields, 2, 0);
    auto* video = lv_obj_create(panels[0]);
    lv_obj_set_size(video, 320, 240);
    auto* actual = lv_label_create(panels[2]);
    lv_label_set_text(actual, "ACTUAL native observations");
    layout();

    OneSegReceiverViews views(panels[2], panels[3], panels[4], group);
    OneSegReceiverHelp help(root, group);
    for (const auto& info : oneseg_receiver_views())
    {
        if (info.view == OneSegReceiverView::Actual) continue;
        views.show(false);
        views.clearControls();
        lv_obj_clean(fields);
        views.select(info.view);
        views.show(true);
        views.controls(fields);
        layout();
        views.refresh();
        layout();
        assert(lv_obj_has_flag(actual, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_get_width(video) == 320 && lv_obj_get_height(video) == 240);
        assert(lv_obj_get_child_count(fields) == info.parameters.size());
        lv_area_t field_bounds;
        lv_obj_get_content_coords(fields, &field_bounds);
        for (uint32_t i = 0; i < lv_obj_get_child_count(fields); ++i)
        {
            lv_area_t bounds;
            lv_obj_get_coords(lv_obj_get_child(fields, i), &bounds);
            assert(bounds.x1 >= field_bounds.x1 && bounds.x2 <= field_bounds.x2);
            assert(bounds.y1 >= field_bounds.y1 && bounds.y2 <= field_bounds.y2);
            auto* cell = lv_obj_get_child(fields, i);
            lv_area_t heading, control;
            lv_obj_get_coords(lv_obj_get_child(cell, 0), &heading);
            lv_obj_get_coords(lv_obj_get_child(cell, 1), &control);
            assert(heading.y2 < control.y1);
            assert(heading.x1 >= bounds.x1 && heading.x2 <= bounds.x2);
        }
        const auto explanation = views.helpText();
        assert(explanation.find(info.equations) != std::string::npos);
        assert(!help.visible());
        help.open(info.name, explanation, fields);
        layout();
        assert(help.visible());
        lv_area_t popup;
        lv_obj_get_coords(help.object(), &popup);
        assert(popup.x1 >= 0 && popup.y1 >= 0 && popup.x2 < 1232 && popup.y2 < 568);
        help.close();
        assert(!help.visible());
        auto* first = lv_obj_get_child(lv_obj_get_child(fields, 0), 1);
        auto* second = lv_obj_get_child(lv_obj_get_child(fields, 1), 1);
        lv_group_focus_obj(first);
        help.open(info.name, explanation, first);
        uint32_t escape = LV_KEY_ESC;
        lv_obj_send_event(lv_group_get_focused(group), LV_EVENT_KEY, &escape);
        assert(!help.visible() && lv_group_get_focused(group) == first);
        lv_group_focus_obj(second);
        help.close();
        assert(lv_group_get_focused(group) == second);
        views.save();
        views.reset();
        assert(views.load());
        std::cout << "PASS UI " << info.name << '\n';
    }
    views.show(false);
    assert(!lv_obj_has_flag(actual, LV_OBJ_FLAG_HIDDEN));
    // LVGL lines own references to model-side point buffers. Remove the object
    // tree while those buffers and the input group still exist.
    lv_obj_delete(root);
    lv_group_delete(group);
    std::cout << "PASS 1232x568 layout, direct controls, popup bounds, actual restoration\n";
}
