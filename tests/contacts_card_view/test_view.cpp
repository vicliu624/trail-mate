#include "ui/screens/contacts/contacts_node_card_view.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static size_t objects(lv_obj_t* root)
{
    size_t count = 1;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) count += objects(lv_obj_get_child(root, i));
    return count;
}

static void check_geometry(lv_obj_t* root, lv_obj_t* parent)
{
    lv_area_t child{}, bounds{};
    lv_obj_get_coords(root, &child);
    lv_obj_get_coords(parent, &bounds);
    assert(child.x1 >= bounds.x1 && child.x2 <= bounds.x2);
    assert(child.y1 >= bounds.y1 && child.y2 <= bounds.y2);
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        check_geometry(lv_obj_get_child(root, i), root);
}

static void write_frame(const char* name, int width, int height, const std::vector<uint16_t>& pixels)
{
    std::ofstream file(name, std::ios::binary);
    file << "P6\n"
         << width << " " << height << "\n255\n";
    for (const auto pixel : pixels)
    {
        const unsigned char rgb[3] = {static_cast<unsigned char>(((pixel >> 11) & 31) * 255 / 31),
                                      static_cast<unsigned char>(((pixel >> 5) & 63) * 255 / 63),
                                      static_cast<unsigned char>((pixel & 31) * 255 / 31)};
        file.write(reinterpret_cast<const char*>(rgb), 3);
    }
}

int main()
{
    lv_init();
    for (const int width : {320, 480})
    {
        const int height = width == 320 ? 170 : 222;
        auto* display = lv_display_create(width, height);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
        std::vector<uint16_t> pixels(width * height);
        lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*)
                                { lv_display_flush_ready(d); });
        auto* parent = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(parent);
        lv_obj_set_pos(parent, 0, 24);
        lv_obj_set_size(parent, width, height - 48);
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        auto* footer = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(footer);
        lv_obj_set_pos(footer, 0, height - 24);
        lv_obj_set_size(footer, width, 24);
        lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
        const auto view = contacts::ui::card_view::create(parent, &lv_font_montserrat_14,
                                                          &lv_font_montserrat_12, &lv_font_montserrat_16, footer);
        const size_t count = objects(parent);
        const size_t footer_count = objects(footer);
        int clicks = 0;
        lv_obj_add_event_cb(
            view.next, [](lv_event_t* event)
            { ++*static_cast<int*>(lv_event_get_user_data(event)); },
            LV_EVENT_CLICKED, &clicks);
        for (int i = 0; i < 1000; ++i)
        {
            lv_label_set_text(view.name, "North Ridge Relay with a long node name");
            lv_label_set_text(view.identity, i % 3 ? "MC Key 7a3f91...9b4c" : "RT Dest 4c18a2...e27d");
            lv_label_set_text(view.region, "~ Hangzhou / Xihu");
            lv_label_set_text(view.distance, i % 2 ? "Distance 3.6 km" : "Distance unknown");
            lv_label_set_text(view.status, "Seen 2m | SNR 8.5 dB");
            lv_label_set_text(view.counter, "1 / 1000");
            lv_obj_update_layout(parent);
            assert(objects(parent) == count);
            assert(objects(footer) == footer_count);
            check_geometry(view.root, parent);
            lv_area_t name{}, identity{};
            lv_obj_get_coords(view.name, &name);
            lv_obj_get_coords(view.identity, &identity);
            assert(name.y1 == identity.y1 || name.y2 == identity.y2 ||
                   (name.y1 < identity.y2 && identity.y1 < name.y2));
            assert(name.x2 < identity.x1);
            assert(lv_obj_get_parent(view.previous) == footer && lv_obj_get_parent(view.details) == footer);
        }
        lv_obj_send_event(view.next, LV_EVENT_CLICKED, nullptr);
        assert(clicks == 1);
        // Long administrative paths retain their text and animate in one row.
        const char* long_region = "~ China / Zhejiang Province / Hangzhou City / Xihu District / North Ridge";
        lv_label_set_text(view.region, long_region);
        lv_obj_update_layout(parent);
        assert(std::string(lv_label_get_text(view.region)) == long_region);
        assert(lv_anim_get(view.region, nullptr) != nullptr);
        for (int step = 0; step < 60; ++step)
        {
            lv_tick_inc(50);
            lv_timer_handler();
        }
        check_geometry(view.root, parent);
        assert(objects(parent) == count);
        lv_label_set_text(view.region, "~ Hangzhou / Xihu");
        lv_obj_update_layout(parent);
        assert(lv_anim_get(view.region, nullptr) == nullptr);
        lv_refr_now(display);
        write_frame(width == 320 ? "card-320.ppm" : "card-480.ppm", width, height, pixels);
        lv_mem_monitor_t monitor{};
        lv_mem_monitor(&monitor);
        std::printf("screen=%dx%d card_objects=%u used_heap=%u largest_free=%u\n", width, height,
                    static_cast<unsigned>(count - 1), static_cast<unsigned>(monitor.total_size - monitor.free_size),
                    static_cast<unsigned>(monitor.free_biggest_size));
        lv_obj_delete(parent);
        lv_obj_delete(footer);
        for (const int nodes : {10, 100, 1000})
        {
            auto* baseline = lv_obj_create(lv_screen_active());
            for (int i = 0; i < nodes; ++i)
            {
                auto* row = lv_obj_create(baseline);
                lv_obj_set_size(row, width - 104, 46);
                auto* name = lv_label_create(row);
                lv_label_set_text(name, "North Ridge Relay with a long node name");
                auto* status = lv_label_create(row);
                lv_label_set_text(status, "Seen 2m");
            }
            lv_mem_monitor(&monitor);
            std::printf("baseline_nodes=%d row_objects=%u used_heap=%u\n", nodes,
                        static_cast<unsigned>(objects(baseline) - 1),
                        static_cast<unsigned>(monitor.total_size - monitor.free_size));
            lv_obj_delete(baseline);
        }
        lv_display_delete(display);
    }
    lv_deinit();
}
