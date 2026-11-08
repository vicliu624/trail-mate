#include "../../../modules/ui_shared/src/ui/screens/music/music_state.h"
#include "app/input_event.h"
#include "platform/ui/orientation_runtime.h"
#include "ui/app_runtime.h"
#include "ui/menu/dashboard/dashboard_state.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/menu/menu_layout.h"
#include "ui/screens/tracker/tracker_state.h"
#include "ui/shell_ui_runner.h"
#include "ui/widgets/route_image_strip.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

using namespace trailmate::cardputer_zero;
using namespace std::chrono_literals;
using InputKey = trailmate::cardputer_zero::app::InputKey;

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
                      ("trailmate-compact-widgets-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_RUNTIME_MODE", "offline", 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    {
        linux_ui::ShellSession shell;
        linux_ui::CanvasLvglHost host(shell);
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
        const auto key = [&](InputKey code, char value = 0)
        {
            shell.enqueueInputs({{code, value ? std::string(1, value) : std::string{}, value}});
            tick(250ms);
        };
        tick(3300ms);
        assert(shell.ready());
        auto* initial = lv_group_get_focused(menu_g);
        assert(initial);
        key(InputKey::Right);
        auto* right = lv_group_get_focused(menu_g);
        assert(right && right != initial && lv_obj_get_y(right) == lv_obj_get_y(initial));
        key(InputKey::Down);
        auto* down = lv_group_get_focused(menu_g);
        assert(down && down != right && lv_obj_get_y(down) > lv_obj_get_y(right));
        key(InputKey::Character, 'v');
        assert(ui::menu::dashboard::isFeedOpen());
        auto* state = ui::menu::dashboard::dashboard_state();
        assert(state && state->widgets[0].root);
        const auto count = static_cast<unsigned>(ui::menu::dashboard::WidgetKind::Count) -
                           (::platform::ui::orientation::heading_supported() ? 0U : 1U);
        for (unsigned page = 0; page < count; ++page)
        {
            assert(state->page == page);
            auto* widget = state->widgets[0].root;
            assert(widget && lv_obj_is_valid(widget));
            assert(lv_obj_get_style_transform_scale_x(widget, LV_PART_MAIN) == 170);
            assert(lv_obj_get_width(widget) * 170 / 256 <= 320);
            assert(lv_obj_get_height(widget) * 170 / 256 <= lv_obj_get_height(state->grid));
            if (page + 1 < count) key(InputKey::Down);
        }
        // Widget controls take Left/Right without closing the feed.
        key(InputKey::Right);
        assert(ui::menu::dashboard::isFeedOpen());
        key(InputKey::Character, 'v');
        assert(!ui::menu::dashboard::isFeedOpen());
        assert(lv_group_get_focused(menu_g) == down);
        key(InputKey::Enter);
        assert(!ui::menu::dashboard::isFeedOpen());
        key(InputKey::Home);
        key(InputKey::Character, 'v');
        assert(ui::menu::dashboard::isFeedOpen());
        key(InputKey::Home);
        assert(!ui::menu::dashboard::isFeedOpen());
        assert(ui::menu_layout::launchAppByStableId("music"));
        tick(100ms);
        auto* music = ui::music_page::state();
        assert(music && music->service && music->root);
        ui::music_page::navigate(ui::music_page::View::Playing);
        lv_obj_update_layout(music->root);
        lv_area_t album, progress, elapsed;
        lv_obj_get_coords(music->album, &album);
        lv_obj_get_coords(music->progress, &progress);
        lv_obj_get_coords(music->elapsed, &elapsed);
        assert(album.y2 + 3 < progress.y1);
        assert(progress.y2 < elapsed.y1 && elapsed.y2 < 146);
        key(InputKey::Home);
        const auto storage = root / "sd";
        const auto retained = root / "sd-retained";
        std::filesystem::create_directories(storage / "tracks");
        std::ofstream(storage / "tracks/storage-ready.gpx")
            << "<gpx><trk><name>Storage ready</name><trkseg><trkpt lat=\"51.5\" lon=\"-0.17\"/></trkseg></trk></gpx>";
        assert(ui::menu_layout::launchAppByStableId("tracker"));
        tick(1600ms);
        auto& tracker = ::tracker::ui::g_tracker_state;
        assert(tracker.card_counter && !std::strcmp(lv_label_get_text(tracker.card_counter), "1 / 1"));
        // Exercise the real asynchronous storage probe: recover the open page
        // after its root becomes unavailable, without leaving/reentering it.
        std::filesystem::rename(storage, retained);
        std::ofstream(storage) << "temporarily unavailable volume";
        tick(1600ms);
        assert(!std::strcmp(lv_label_get_text(tracker.card_name), "No SD Card"));
        std::filesystem::remove(storage);
        std::filesystem::rename(retained, storage);
        tick(1600ms);
        assert(!std::strcmp(lv_label_get_text(tracker.card_counter), "1 / 1"));
        key(InputKey::Home);
        std::vector<uint8_t> bitmap(54 + 320 * 180 * 3, 0);
        const auto integer = [&](size_t offset, uint32_t value)
        { for (unsigned byte=0;byte<4;++byte) bitmap[offset+byte]=uint8_t(value>>(byte*8)); };
        bitmap[0] = 'B';
        bitmap[1] = 'M';
        bitmap[26] = 1;
        bitmap[28] = 24;
        integer(2, bitmap.size());
        integer(10, 54);
        integer(14, 40);
        integer(18, 320);
        integer(22, 180);
        for (unsigned y = 0; y < 180; ++y)
            for (unsigned x = 0; x < 320; ++x) bitmap[54 + ((179 - y) * 320 + x) * 3 + (y < 60 ? 2 : y < 120 ? 1
                                                                                                             : 0)] = 255;
        const auto image_path = (root / "fullscreen.bmp").string();
        std::ofstream(image_path, std::ios::binary).write(reinterpret_cast<const char*>(bitmap.data()), bitmap.size());
        ui::widgets::route_image_strip::Widget photos;
        ui::widgets::route_image_strip::create(lv_screen_active(), photos);
        ui::widgets::route_image_strip::Item photo;
        photo.downloaded = true;
        photo.local_path = image_path;
        photo.preview_path = image_path;
        photo.view_path = image_path;
        ui::widgets::route_image_strip::set_items(photos, &photo, 1);
        ui::widgets::route_image_strip::set_hidden(photos, false);
        assert(ui::widgets::route_image_strip::handle_key(photos, LV_KEY_ENTER));
        tick(400ms);
        lv_obj_invalidate(photos.fullscreen_root);
        tick(400ms);
        for (unsigned y : {30U, 85U, 150U})
        {
            for (unsigned x : {80U, 240U})
            {
                const auto& pixel = host.canvas().pixel(x, y);
                if (y == 30) assert(pixel.r > 240 && pixel.g < 8 && pixel.b < 8);
                else if (y == 85) assert(pixel.r < 8 && pixel.g > 240 && pixel.b < 8);
                else assert(pixel.r < 8 && pixel.g < 8 && pixel.b > 240);
            }
        }
        ui::widgets::route_image_strip::destroy(photos);
    }
    std::filesystem::remove_all(root);
}
