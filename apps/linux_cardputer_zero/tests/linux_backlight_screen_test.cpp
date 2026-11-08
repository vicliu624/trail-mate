#include "platform/linux/backlight_screen.h"
#include "platform/ui/settings_store.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace
{
template <class Predicate>
void await(Predicate predicate)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate())
    {
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
} // namespace

int main()
{
    namespace runtime = platform::linux_runtime;
    namespace screen = platform::ui::screen;
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-backlight-screen-" + std::to_string(getpid()));
    std::filesystem::create_directories(root / "panel");
    std::ofstream(root / "panel/max_brightness") << 100;
    std::ofstream(root / "panel/brightness") << 37;
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    platform::ui::settings_store::put_uint("settings", "screen_timeout", 10000);
    uint32_t now = 0;
    runtime::BacklightOptions options;
    options.root = root;
    {
        runtime::BacklightScreenBinding binding(options, [&]
                                                { return now; });
        await([]
              { return runtime::backlight_snapshot().available; });
        screen::init({});
        assert(screen::supports_app_timeout_setting());
        runtime::service_screen_runtime();
        now = 10000;
        runtime::service_screen_runtime();
        assert(screen::is_sleeping());
        await([]
              { return runtime::backlight_snapshot().level == 0; });
        assert(runtime::BacklightScreenBinding::consumeWakeInput(false));
        assert(!screen::is_sleeping() && screen::is_saver_active());
        await([]
              { return runtime::backlight_snapshot().level == 94; });
        assert(runtime::BacklightScreenBinding::consumeWakeInput(true));
        assert(!screen::is_saver_active());
        assert(!runtime::BacklightScreenBinding::consumeWakeInput(false));
        now = 20000;
        runtime::service_screen_runtime();
        assert(screen::is_sleeping());
        await([]
              { return runtime::backlight_snapshot().level == 0; });
    }
    unsigned restored = 0;
    std::ifstream(root / "panel/brightness") >> restored;
    assert(restored == 37); // Exiting while asleep restores the prior brightness.
    std::filesystem::remove_all(root);
}
