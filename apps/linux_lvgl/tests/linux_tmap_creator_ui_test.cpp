#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/linux/resource_pack_tasks.h"
#include "platform/ui/tmap_creation_runtime.h"
#include "ui/app_runtime.h"
#include "ui/input/pager_navigation.h"
#include "ui/page/page_profile.h"
#include "ui/screens/extensions/extensions_page_runtime.h"
#include "ui/screens/extensions/tmap_creator_page.h"
#include <SDL.h>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <unistd.h>
#include <vector>

namespace tc = platform::ui::tmap_creation;
struct ExtensionsApp final : AppScreen
{
    const char* stable_id() const override { return "extensions"; }
    const char* name() const override { return "Extensions"; }
    const lv_image_dsc_t* icon() const override { return nullptr; }
    void enter(lv_obj_t* parent) override { extensions::ui::runtime::enter(nullptr, parent); }
    void exit(lv_obj_t* parent) override { extensions::ui::runtime::exit(parent); }
};
struct Service final : tc::Service
{
    tc::Snapshot state;
    tc::Plan received;
    bool source_ready = false;
    Service() { state.producer_ready = true; }
    std::vector<tc::Source> sources() override
    {
        ++state.source_revision;
        return source_ready ? std::vector<tc::Source>{{"/sources/test.osm.pbf", "test.osm.pbf", 300}} : std::vector<tc::Source>{};
    }
    tc::Estimate estimate(const tc::Plan&) override
    {
        tc::Estimate value;
        value.valid = true;
        value.tiles = 4;
        value.raster_bytes = 524288;
        value.working_bytes = 67108864;
        value.available_bytes = 1024ULL * 1024 * 1024;
        return value;
    }
    bool start(const tc::Plan& plan, std::string&) override
    {
        received = plan;
        state.busy = true;
        state.phase = tc::Phase::Building;
        state.stage = "Rendering map tiles";
        return true;
    }
    tc::Snapshot snapshot() override { return state; }
    void cancel() override
    {
        state.busy = false;
        state.phase = tc::Phase::Cancelled;
        state.stage = "Creation cancelled";
    }
};
uint32_t now_ms = 0;
lv_obj_t* find_label(lv_obj_t* parent, const char* text)
{
    if (lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(parent, &lv_label_class) && !std::strcmp(lv_label_get_text(parent), text)) return parent;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i)
        if (auto* found = find_label(lv_obj_get_child(parent, i), text)) return found;
    return nullptr;
}
void fields(lv_obj_t* parent, std::vector<lv_obj_t*>& output)
{
    if (lv_obj_check_type(parent, &lv_textarea_class)) output.push_back(parent);
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) fields(lv_obj_get_child(parent, i), output);
}
int main(int argc, char** argv)
{
    const bool compact = argc > 1 && std::string(argv[1]) == "compact";
    char data_root[] = "/tmp/trailmate-tmap-ui-XXXXXX";
    assert(mkdtemp(data_root));
    setenv("TRAIL_MATE_SETTINGS_ROOT", data_root, 1);
    setenv("TRAIL_MATE_SD_ROOT", data_root, 1);
    setenv("TRAIL_MATE_PACK_CATALOG_URL", "http://127.0.0.1:1/catalog.json", 1);
    lv_init();
    lv_tick_set_cb([]
                   { return now_ms; });
    {
        platform::desktop::DeviceOptions options;
        options.title = "TMAP creator";
        options.ux_pack = compact ? "cardputer_compact" : "t_display_p4_touch";
        options.width = compact ? 320 : 1232;
        options.height = compact ? 170 : 568;
        options.fullscreen = false;
        options.hardware_keyboard = true;
        platform::desktop::SdlLvglHost host(options);
        const auto profile = compact ? ui::page_profile::make_cardputer_zero_profile() : ui::page_profile::make_t_display_p4_profile();
        ui::page_profile::set_active_profile(&profile);
        app_g = lv_group_create();
        lv_group_set_default(app_g);
        auto* root = lv_obj_create(lv_screen_active());
        lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(root, 0, 0);
        Service service;
        tc::bind(&service);
        unsigned closed = 0;
        extensions::ui::tmap_creator::open(
            root, [](void* value)
            { ++*static_cast<unsigned*>(value); },
            &closed);
        service.source_ready = true;
        ++service.state.source_revision; // Async completion must refresh without a second click.
        const auto step = [&]
        { now_ms += 300; assert(host.pump()); lv_timer_handler(); lv_obj_update_layout(root); host.present(); };
        const auto click = [&](const char* text)
        {
            step();
            auto* object = find_label(root, text);
            assert(object);
            while (object && !lv_obj_check_type(object, &lv_button_class)) object = lv_obj_get_parent(object);
            assert(object && !lv_obj_has_state(object, LV_STATE_DISABLED));
            lv_area_t area;
            lv_obj_get_coords(object, &area);
            assert(area.y1 >= 0 && area.y2 < options.height);
            lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr);
            step();
        };
        const auto fill = [&](const std::vector<const char*>& values)
        {
            std::vector<lv_obj_t*> found;
            fields(root, found);
            assert(found.size() == values.size());
            for (size_t i = 0; i < found.size(); ++i)
            {
                lv_textarea_set_text(found[i], values[i]);
                lv_obj_send_event(found[i], LV_EVENT_VALUE_CHANGED, nullptr);
            }
        };
        click("Next");
        fill({"102.70", "25", "102.71", "25.01"});
        click("Next");
        click("Next");
        fill({"Created on device", "device-area"});
        click("Next");
        fill({"device-area.tmap", "1"});
        click("Next");
        if (argc > 2)
        {
            std::filesystem::create_directories(std::filesystem::path(argv[2]).parent_path());
            lv_refr_now(nullptr);
            host.present();
            host.saveScreenshot(argv[2]);
        }
        click("Create");
        assert(service.received.source == "/sources/test.osm.pbf" && service.received.name == "Created on device" && service.received.west == 102.70 && service.received.max_zoom == 14);
        click("Close");
        assert(closed == 1 && service.state.busy);
        extensions::ui::tmap_creator::open(
            root, [](void* value)
            { ++*static_cast<unsigned*>(value); },
            &closed);
        click("Cancel");
        assert(!service.state.busy);
        click("New map");
        click("Close");
        assert(closed == 2);
        tc::bind(nullptr);
        const auto count = lv_obj_get_child_count(root);
        extensions::ui::tmap_creator::open(root, nullptr, nullptr);
        assert(lv_obj_get_child_count(root) == count);
        extensions::ui::tmap_creator::shutdown();
        // Exercise the actual Extensions entry and its return path as well as
        // the standalone wizard; no public catalog is contacted by this test.
        service.state.phase = tc::Phase::Idle;
        tc::bind(&service);
        ExtensionsApp app;
        ui_switch_to_app(&app, root);
        host.setKeyObserver([](void*, uint32_t key, bool pressed)
                            {
            auto* group = lv_group_get_default();
            if (pressed) ui::input::pager::prepare_group(group);
            return ui::input::pager::handle_key(key, pressed, group); },
                            nullptr);
        const auto press = [&](SDL_Keycode key)
        {
            SDL_Event event{};
            event.type = SDL_KEYDOWN;
            event.key.keysym.sym = key;
            assert(SDL_PushEvent(&event) == 1);
            step();
            event.type = SDL_KEYUP;
            assert(SDL_PushEvent(&event) == 1);
            step();
        };
        const auto activate = [&](const char* text)
        {
            auto* object = find_label(root, text);
            assert(object);
            while (object && !lv_obj_check_type(object, &lv_button_class)) object = lv_obj_get_parent(object);
            assert(object);
            lv_group_focus_obj(object);
            press(SDLK_RETURN);
        };
        step();
        assert(find_label(root, "T Make TMAP")); // Visible direct shortcut hint.
        press(SDLK_m);
        assert(find_label(root, "Installed") && find_label(root, "Uninstalled"));
        press(SDLK_m);
        assert(!find_label(root, "Installed") && !find_label(root, "Uninstalled"));
        uint32_t uppercase_menu = 'M';
        lv_group_send_data(lv_group_get_default(), uppercase_menu);
        step();
        assert(find_label(root, "Installed") && find_label(root, "Uninstalled"));
        press(SDLK_ESCAPE);
        assert(!find_label(root, "Installed") && !find_label(root, "Uninstalled"));
        auto* group = lv_group_get_default();
        auto* before = lv_group_get_focused(group);
        press(SDLK_s);
        assert(lv_group_get_focused(group) != before);
        auto* selected = lv_group_get_focused(group);
        press(SDLK_h);
        assert(find_label(root, "Select next control"));
        assert(find_label(root, "Make TMAP")); // Contextual Help advertises T.
        press(SDLK_h);
        assert(!find_label(root, "Select next control") && lv_group_get_focused(group) == selected);
        activate("T Make TMAP");
        activate("Close");
        press(SDLK_m);
        assert(find_label(root, "Installed"));
        uint32_t uppercase_creator = 'T';
        lv_group_send_data(lv_group_get_default(), uppercase_creator);
        step();
        assert(find_label(root, "Next") && !find_label(root, "Installed"));
        activate("Close");
        press(SDLK_t);
        assert(find_label(root, "Next"));
        press(SDLK_h);
        assert(find_label(root, "Activate Next / Back / Create"));
        press(SDLK_h);
        activate("Next");
        std::vector<lv_obj_t*> editors;
        fields(root, editors);
        assert(editors.size() == 4);
        lv_group_focus_obj(editors.front());
        lv_textarea_set_text(editors.front(), "");
        press(SDLK_h);
        assert(!std::strcmp(lv_textarea_get_text(editors.front()), "h"));
        press(SDLK_t);
        assert(!std::strcmp(lv_textarea_get_text(editors.front()), "ht"));
        assert(!find_label(root, "Activate Next / Back / Create"));
        press(SDLK_ESCAPE);
        activate("Close");
        assert(find_label(root, "Extensions"));
        ui_exit_active_app(root);
        platform::linux_runtime::stop_resource_pack_tasks();
        tc::bind(nullptr);
        lv_obj_del(root);
        lv_group_del(app_g);
        app_g = nullptr;
        ui::page_profile::set_active_profile(nullptr);
    }
    lv_deinit();
    std::filesystem::remove_all(data_root);
}
