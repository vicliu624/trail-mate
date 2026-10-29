#include "app/input_event.h"
#include "ui/app_runtime.h"
#include "ui/screens/agenda/agenda_page_runtime.h"
#include "ui/screens/agenda/agenda_state.h"
#include "ui/shell_ui_runner.h"
#include "ui_presentation/waypoint/waypoint_model.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>
#define LODEPNG_NO_COMPILE_CPP
#include "src/libs/lodepng/lodepng.h"

using namespace std::chrono_literals;
namespace page = ui::agenda::page;
lv_obj_t* label(lv_obj_t* parent, const char* text)
{
    if (lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(parent, &lv_label_class) && !std::strcmp(lv_label_get_text(parent), text)) return parent;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i)
        if (auto* result = label(lv_obj_get_child(parent, i), text)) return result;
    return nullptr;
}
int main(int argc, char** argv)
{
    char root[] = "/tmp/trailmate-agenda-card-XXXXXX";
    assert(mkdtemp(root));
    setenv("TRAIL_MATE_SETTINGS_ROOT", root, 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root, 1);
    setenv("TRAIL_MATE_SD_ROOT", (std::filesystem::path(root) / "sd").c_str(), 1);
    setenv("TRAIL_MATE_RUNTIME_MODE", "local", 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    {
        trailmate::cardputer_zero::linux_ui::ShellSession shell;
        trailmate::cardputer_zero::linux_ui::CanvasLvglHost host(shell);
        assert(shell.begin());
        const auto tick = [&](auto duration)
        {const auto end=std::chrono::steady_clock::now()+duration;do{host.tick();std::this_thread::sleep_for(5ms);}while(std::chrono::steady_clock::now()<end); };
        const auto until = [&](auto condition)
        {const auto end=std::chrono::steady_clock::now()+10s;do{tick(20ms);assert(std::chrono::steady_clock::now()<end);}while(!condition()); };
        using Key = trailmate::cardputer_zero::app::InputKey;
        const auto key = [&](Key code, char value = 0)
        {shell.enqueueInputs({{code,value?std::string(1,value):std::string{},value}});tick(110ms); };
        const auto capture = [&](const char* name)
        {
            if (argc < 2) return;
            lv_refr_now(nullptr);
            host.tick();
            const auto& canvas = host.canvas();
            std::vector<unsigned char> pixels;
            for (const auto& p : canvas.pixels())
            {
                pixels.push_back(p.r);
                pixels.push_back(p.g);
                pixels.push_back(p.b);
                pixels.push_back(p.a);
            }
            unsigned char* encoded = nullptr;
            size_t size = 0;
            assert(lodepng_encode32(&encoded, &size, pixels.data(), canvas.width(), canvas.height()) == 0);
            std::filesystem::create_directories(argv[1]);
            std::ofstream file(std::filesystem::path(argv[1]) / name, std::ios::binary);
            file.write(reinterpret_cast<char*>(encoded), size);
            std::free(encoded);
            assert(file.good());
        };
        const auto click = [&](const char* text)
        {
            const auto target = [&]() -> lv_obj_t*
            {
                auto* object = label(page::state()->root, text);
                assert(object);
                auto* caption = object;
                while (object && !lv_obj_check_type(object, &lv_button_class)) object = lv_obj_get_parent(object);
                if (!object)
                {
                    auto* parent = lv_obj_get_parent(caption);
                    const auto next = lv_obj_get_index(caption) + 1;
                    if (parent && next < static_cast<int>(lv_obj_get_child_count(parent)))
                    {
                        auto* sibling = lv_obj_get_child(parent, next);
                        if (lv_obj_check_type(sibling, &lv_button_class)) object = sibling;
                    }
                }
                assert(object);
                return object;
            };
            auto* object = target();
            lv_obj_scroll_to_view(object, LV_ANIM_OFF);
            for (unsigned i = 0; i < 40 && lv_group_get_focused(lv_group_get_default()) != object; ++i)
            {
                key(Key::Tab);
                object = target();
            }
            if (lv_group_get_focused(lv_group_get_default()) != object) std::fprintf(stderr, "Unreachable Agenda control: %s\n", text);
            assert(lv_group_get_focused(lv_group_get_default()) == object);
            key(Key::Enter);
        };
        tick(3300ms);
        for (unsigned i = 0; i < 12; ++i) key(Key::Right);
        key(Key::Enter);
        until([&]
              { return page::state() && page::state()->snapshot.storage_ready; });
        assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "calendar"));
        capture("agenda-home.png");
        click("New");
        until([&]
              { return page::state()->view == page::View::Editor; });
        for (char c : std::string("Card event")) key(Key::Character, c);
        capture("agenda-typed.png");
        click("Time");
        until([&]
              { return page::state()->view == page::View::TimePicker; });
        capture("agenda-time.png");
        auto* ok = lv_obj_get_parent(label(page::state()->root, "OK"));
        assert(ok);
        lv_area_t footer;
        lv_obj_get_coords(ok, &footer);
        for (auto* field : page::state()->editor_widgets.clock_fields)
        {
            lv_area_t bounds;
            lv_obj_get_coords(field, &bounds);
            assert(bounds.y2 < footer.y1);
        }
        auto* helper = label(page::state()->root, "End <= start: next day");
        assert(helper);
        lv_area_t hint;
        lv_obj_get_coords(helper, &hint);
        assert(hint.y2 < footer.y1);
        key(Key::Home);
        until([&]
              { return page::state()->view == page::View::Editor; });
        click("Date");
        until([&]
              { return page::state()->view == page::View::DatePicker; });
        capture("agenda-date.png");
        key(Key::Home);
        until([&]
              { return page::state()->view == page::View::Editor; });
        capture("agenda-editor.png");
        assert(ui::agenda::applyCoordinate(page::state()->draft, {225432100, 1141234567}));
        click("Location");
        until([&]
              { return page::state()->view == page::View::LocationPicker; });
        auto* saved_button = lv_obj_get_parent(label(page::state()->root, "Saved waypoint"));
        auto* remove_button = lv_obj_get_parent(label(page::state()->root, "Remove location"));
        assert(saved_button && remove_button);
        lv_area_t viewport, footer_bounds;
        lv_obj_get_coords(lv_obj_get_parent(saved_button), &viewport);
        lv_obj_get_coords(remove_button, &footer_bounds);
        assert(viewport.y2 < footer_bounds.y1);
        click("Saved waypoint");
        until([&]
              { return page::state()->view == page::View::WaypointPicker; });
        waypoint::Page waypoints;
        until([&]
              { return page::state()->host->waypoints->page(0, 5, waypoints) == waypoint::Result::Ok; });
        click("New");
        until([&]
              { return page::state()->view == page::View::WaypointName; });
        lv_textarea_set_text(page::state()->editor_widgets.title, "Card camp");
        click("Save");
        until([&]
              { return page::state()->view == page::View::Editor; });
        assert(page::state()->draft.event.location_type == agenda::LocationType::Waypoint);
        click("Location");
        until([&]
              { return page::state()->view == page::View::LocationPicker; });
        click("Remove location");
        until([&]
              { return page::state()->view == page::View::Editor; });
        assert(!(page::state()->draft.event.flags & agenda::HasLocation));
        click("Location");
        until([&]
              { return page::state()->view == page::View::LocationPicker; });
        click("Saved waypoint");
        until([&]
              { return page::state()->view == page::View::WaypointPicker; });
        capture("agenda-waypoints.png");
        click("Card camp");
        until([&]
              { return page::state()->view == page::View::Editor; });
        assert(page::state()->draft.event.latitude_e7 == 225432100 && page::state()->draft.event.longitude_e7 == 1141234567);
        click("Save");
        until([&]
              { return page::state()->view == page::View::Agenda && label(page::state()->root, "Card event"); });
        capture("agenda-saved.png");
        click("Card event");
        until([&]
              { return page::state()->view == page::View::Detail; });
        const auto id = page::state()->draft.event.id;
        assert(id);
        click("Edit");
        until([&]
              { return page::state()->view == page::View::Editor; });
        assert(!std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "Card event"));
        key(Key::Home);
        tick(150ms);
        if (page::state()->confirm_discard) click("Discard");
        until([&]
              { return page::state()->view == page::View::Detail; });
        click("Delete");
        until([&]
              { return page::state()->confirm_delete; });
        click("Delete");
        until([&]
              { return page::state()->view == page::View::Agenda && !label(page::state()->root, "Card event"); });
        key(Key::Home);
        until([&]
              { return !ui_get_active_app(); });
    }
    std::filesystem::remove_all(root);
}
