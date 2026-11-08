#include "app/input_event.h"
#include "platform/ui/tmap_creation_runtime.h"
#include "ui/app_runtime.h"
#include "ui/menu/menu_layout.h"
#include "ui/shell_ui_runner.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;
using Key = trailmate::cardputer_zero::app::InputKey;
namespace tc = platform::ui::tmap_creation;
struct Service final : tc::Service
{
    tc::Snapshot state{};
    tc::Plan received{};
    tc::AreaSearchSnapshot search_state{};
    bool ready = false, reject = false;
    unsigned starts = 0;
    Service()
    {
        state.producer_ready = true;
        state.downloader_ready = true;
        state.source_directory = "/home/pi/.trailmate_cardputer_zero/sdcard/map-sources";
    }
    std::vector<tc::Source> sources() override
    {
        return ready ? std::vector<tc::Source>{{"/test.osm.pbf", "test.osm.pbf", 300}} : std::vector<tc::Source>{};
    }
    tc::Estimate estimate(const tc::Plan&) override
    {
        tc::Estimate result;
        result.valid = true;
        result.tiles = 4;
        result.raster_bytes = 524288;
        result.working_bytes = 67108864;
        result.available_bytes = 1024ULL * 1024 * 1024;
        return result;
    }
    bool start(const tc::Plan& plan, std::string& error) override
    {
        if (reject)
        {
            error = "Test producer error";
            return false;
        }
        ++starts;
        received = plan;
        state.busy = true;
        state.phase = tc::Phase::Building;
        state.stage = "Rendering map tiles";
        return true;
    }
    tc::Snapshot snapshot() override { return state; }
    bool searchAreas(const std::string& query) override
    {
        if (query.empty()) return false;
        search_state = {search_state.revision + 1, true, false, query, {}, {}};
        return true;
    }
    tc::AreaSearchSnapshot areaSearch() override { return search_state; }
    void cancel() override
    {
        state.busy = false;
        state.phase = tc::Phase::Cancelled;
        state.stage = "Creation cancelled";
    }
};
lv_obj_t* label(lv_obj_t* object, const char* text)
{
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(object, &lv_label_class) && !std::strcmp(lv_label_get_text(object), text)) return object;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        if (auto* found = label(lv_obj_get_child(object, i), text)) return found;
    return nullptr;
}
void objects(lv_obj_t* object, const lv_obj_class_t* type, std::vector<lv_obj_t*>& output)
{
    if (lv_obj_check_type(object, type)) output.push_back(object);
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i) objects(lv_obj_get_child(object, i), type, output);
}
lv_obj_t* button(const char* text)
{
    auto* object = label(lv_screen_active(), text);
    assert(object);
    while (object && !lv_obj_check_type(object, &lv_button_class)) object = lv_obj_get_parent(object);
    assert(object);
    return object;
}
int main(int argc, char** argv)
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-tmap-keyboard-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_PACK_CATALOG_URL", "http://127.0.0.1:1/catalog.json", 1);
    setenv("TRAIL_MATE_RUNTIME_MODE", "local", 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    {
        trailmate::cardputer_zero::linux_ui::ShellSession shell;
        trailmate::cardputer_zero::linux_ui::CanvasLvglHost host(shell);
        assert(shell.begin());
        const auto tick = [&](auto duration)
        {
            const auto end = std::chrono::steady_clock::now() + duration;
            do
            {
                host.tick();
                std::this_thread::sleep_for(5ms);
            } while (std::chrono::steady_clock::now() < end);
        };
        const auto key = [&](Key code, char value = 0)
        {
            shell.enqueueInputs({{code, value ? std::string(1, value) : std::string{}, value}});
            tick(80ms);
        };
        const auto snapshot = [&](const char* name)
        {
            if (argc < 2) return;
            std::filesystem::create_directories(argv[1]);
            host.tick();
            const auto& canvas = host.canvas();
            std::ofstream out(std::filesystem::path(argv[1]) / (std::string(name) + ".ppm"), std::ios::binary);
            out << "P6\n320 170\n255\n";
            for (const auto pixel : canvas.pixels())
            {
                const char rgb[]{char(pixel.r), char(pixel.g), char(pixel.b)};
                out.write(rgb, sizeof(rgb));
            }
        };
        tick(3300ms);
        Service service;
        auto* previous_service = tc::service();
        tc::bind(&service);
        assert(ui::menu_layout::launchAppByStableId("extensions"));
        tick(300ms);
        key(Key::Character, 't');
        tick(300ms);
        assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "extensions"));
        assert(lv_obj_has_state(button("Next"), LV_STATE_DISABLED));
        assert(lv_obj_get_height(button("Search")) == 20);
        const auto activate = [&](const char* caption)
        {
            auto* target = button(caption);
            assert(!lv_obj_has_state(target, LV_STATE_DISABLED));
            unsigned tabs = 0;
            while (lv_group_get_focused(app_g) != target && tabs++ < 24) key(Key::Tab);
            assert(lv_group_get_focused(app_g) == target);
            lv_obj_update_layout(lv_screen_active());
            lv_area_t area{};
            lv_obj_get_coords(target, &area);
            assert(area.x1 >= 0 && area.x2 < 320 && area.y1 >= 0 && area.y2 < 170);
            assert(lv_obj_get_height(target) == 20);
            key(Key::Enter);
            tick(100ms);
        };
        const auto get = [&](const lv_obj_class_t* type)
        {
            std::vector<lv_obj_t*> result;
            // Fields live in the wizard body, alongside the footer.
            objects(lv_obj_get_parent(lv_obj_get_parent(label(lv_screen_active(), "Tab / Enter"))), type, result);
            return result;
        };
        const auto fill = [&](std::initializer_list<const char*> values)
        {
            auto editors = get(&lv_textarea_class);
            assert(editors.size() == values.size());
            size_t index = 0;
            for (const auto* value : values)
            {
                auto* editor = editors[index++];
                unsigned tabs = 0;
                while (lv_group_get_focused(app_g) != editor && tabs++ < 24) key(Key::Tab);
                assert(lv_group_get_focused(app_g) == editor);
                lv_textarea_set_text(editor, "");
                for (const char* character = value; *character; ++character) key(Key::Character, *character);
                assert(!std::strcmp(lv_textarea_get_text(editor), value));
            }
        };
        fill({"test district"});
        auto* search_editor = get(&lv_textarea_class).front();
        lv_obj_send_event(search_editor, LV_EVENT_READY, nullptr);
        assert(service.search_state.busy && lv_obj_is_valid(search_editor));
        tick(100ms);
        assert(!std::strcmp(lv_textarea_get_text(get(&lv_textarea_class).front()), "test district"));
        service.search_state.busy = false;
        // Exercise the real keypad path as well as synchronous READY dispatch.
        key(Key::Enter);
        assert(service.search_state.busy && service.search_state.query == "test district");
        service.search_state.busy = false;
        service.search_state.error = "Test network failure; retry search.";
        ++service.search_state.revision;
        tick(350ms);
        assert(label(lv_screen_active(), "Test network failure; retry search."));
        assert(lv_obj_has_state(button("Next"), LV_STATE_DISABLED));
        activate("Search");
        service.search_state.busy = false;
        service.search_state.results = {
            {"R10", "Test district", "Test district, Yunnan, China", "CN", 102.70, 25, 102.71, 25.01},
            {"R11", "Test district", "Test district, California, United States", "US", -122.5, 37.7, -122.4, 37.8}};
        ++service.search_state.revision;
        tick(350ms);
        assert(!lv_obj_has_state(button("Next"), LV_STATE_DISABLED));
        snapshot("area-search");
        activate("Next");
        activate("Back");
        activate("Coordinates");
        fill({"", "", "", ""});
        activate("Next"); // Validation stays on Area, with a visible error.
        assert(label(lv_screen_active(), "Enter all four coordinates as decimal degrees."));
        fill({"102.70", "25", "102.71", "25.01"});
        assert(!label(lv_screen_active(), "Enter all four coordinates as decimal degrees."));
        snapshot("area");
        activate("Next");
        auto zooms = get(&lv_dropdown_class);
        assert(zooms.size() == 3);
        lv_dropdown_set_selected(zooms[2], 15);
        lv_obj_send_event(zooms[2], LV_EVENT_VALUE_CHANGED, nullptr);
        activate("Back");
        activate("Next");
        zooms = get(&lv_dropdown_class);
        assert(lv_dropdown_get_selected(zooms[2]) == 15);
        lv_dropdown_set_selected(zooms[2], 14);
        lv_obj_send_event(zooms[2], LV_EVENT_VALUE_CHANGED, nullptr);
        snapshot("layers");
        activate("Next");
        fill({"hwst map", "keyboard-area.tmap"});
        service.ready = true;
        activate("Advanced");
        fill({"hwst map", "keyboard-area.tmap", "keyboard-area", "2"});
        auto budgets = get(&lv_dropdown_class);
        assert(budgets.size() == 2);
        // Exercise actual keyboard selection, including the shared Pager router.
        // A popup's Down key must select an option rather than focus Back.
        unsigned source_tabs = 0;
        while (lv_group_get_focused(app_g) != budgets[1] && source_tabs++ < 24) key(Key::Tab);
        assert(lv_group_get_focused(app_g) == budgets[1]);
        key(Key::Enter);
        assert(lv_dropdown_is_open(budgets[1]));
        key(Key::Down);
        assert(lv_group_get_focused(app_g) == budgets[1]);
        assert(lv_dropdown_get_selected(budgets[1]) == 1);
        key(Key::Enter);
        assert(!lv_dropdown_is_open(budgets[1]));
        key(Key::Enter);
        key(Key::Up);
        key(Key::Enter);
        assert(lv_dropdown_get_selected(budgets[1]) == 0);
        lv_dropdown_set_selected(budgets[0], 0);
        activate("Back");
        activate("Next");
        auto editors = get(&lv_textarea_class);
        assert(!std::strcmp(lv_textarea_get_text(editors[3]), "2"));
        assert(lv_dropdown_get_selected(get(&lv_dropdown_class)[0]) == 0);
        activate("Next");
        snapshot("review");
        service.reject = true;
        activate("Create");
        assert(label(lv_screen_active(), "Test producer error"));
        service.reject = false;
        activate("Create");
        assert(service.starts == 1 && service.received.download_source && service.received.source.empty() && service.received.name == "hwst map" && service.received.revision == 2 &&
               service.received.maximum_bytes == 128ULL * 1024 * 1024 && service.received.max_zoom == 14);
        assert(lv_obj_has_state(button("New map"), LV_STATE_DISABLED));
        activate("Close");
        key(Key::Character, 't');
        tick(300ms);
        assert(label(lv_screen_active(), "Rendering map tiles"));
        service.state.busy = false;
        service.state.phase = tc::Phase::Succeeded;
        service.state.stage = "Map created and available in Map layers";
        service.state.output = "maps/tmap/osm/keyboard-area-osm.tmap";
        tick(350ms);
        snapshot("result");
        const auto bars = get(&lv_bar_class);
        assert(bars.size() == 1 && lv_bar_get_value(bars.front()) == 100);
        assert(lv_obj_has_state(button("Cancel"), LV_STATE_DISABLED));
        activate("View map");
        tick(300ms);
        assert(label(lv_screen_active(), "Map"));
        lv_area_t map_title{};
        lv_obj_get_coords(label(lv_screen_active(), "Map"), &map_title);
        assert(map_title.x1 >= 0 && map_title.x2 < 320 && map_title.y1 >= 0 && map_title.y2 < 170);
        snapshot("view-map");
        assert(!label(lv_screen_active(), "Tab / Enter"));
        key(Key::Home);
        tick(350ms);
        assert(button("View map") && button("New map"));
        activate("Close");
        key(Key::Character, 't');
        tick(300ms);
        assert(!lv_obj_has_state(button("New map"), LV_STATE_DISABLED));
        activate("New map");
        activate("Close");
        service.state.busy = true;
        service.state.phase = tc::Phase::Building;
        key(Key::Character, 't');
        tick(300ms);
        activate("Cancel");
        assert(!service.state.busy && service.state.phase == tc::Phase::Cancelled);
        activate("Close");
        ui_exit_active_app(lv_screen_active());
        tc::bind(previous_service);
    }
    std::filesystem::remove_all(root);
}
