#include "ui/startup_ui_shell.h"

#include <cstdint>
#include <cstdio>
#include <ctime>

#include "lvgl.h"
#include "platform/ui/time_runtime.h"
#include "sys/clock.h"
#include "ui/app_runtime.h"
#include "ui/components/screen_saver_overlay.h"
#include "ui/localization.h"
#include "ui/menu/menu_layout.h"
#include "ui/menu/menu_runtime.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/ui_boot.h"
#include "ui/ui_status.h"
#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

namespace ui::startup_ui_shell
{
namespace
{

bool s_shell_initialized = false;
ui::menu::MenuModel s_ux_menu_model;

#ifndef TRAIL_MATE_BOOT_UI_SYNC_PRESENT
#define TRAIL_MATE_BOOT_UI_SYNC_PRESENT 1
#endif

#if TRAIL_MATE_BOOT_UI_SYNC_PRESENT
constexpr uint8_t kBootPresentFrameCount = 4;
constexpr uint32_t kBootPresentFrameDelayMs = 16;
#endif

void present_boot_overlay_now()
{
#if TRAIL_MATE_BOOT_UI_SYNC_PRESENT
    for (uint8_t frame = 0; frame < kBootPresentFrameCount; ++frame)
    {
        if (lv_obj_t* top = lv_layer_top())
        {
            lv_obj_invalidate(top);
        }
        lv_timer_handler();
        lv_refr_now(nullptr);
        if (frame + 1U < kBootPresentFrameCount)
        {
            sys::sleep_ms(kBootPresentFrameDelayMs);
        }
    }
#else
    if (lv_obj_t* top = lv_layer_top())
    {
        lv_obj_invalidate(top);
    }
#endif
}

bool resolve_display_time(struct tm* out_tm)
{
    if (!out_tm)
    {
        return false;
    }

    if (::platform::ui::time::localtime_now(out_tm))
    {
        return true;
    }

    const std::time_t now = std::time(nullptr);
    if (now <= 0)
    {
        return false;
    }

    const std::time_t local = ::platform::ui::time::apply_timezone_offset(now);
    const std::tm* tmp = std::gmtime(&local);
    if (!tmp)
    {
        return false;
    }

    *out_tm = *tmp;
    return true;
}

bool format_menu_time(char* out, size_t out_len)
{
    if (!out || out_len < 6)
    {
        return false;
    }

    struct tm info
    {
    };
    if (!resolve_display_time(&info))
    {
        return false;
    }

    std::strftime(out, out_len, "%H:%M", &info);
    return true;
}

ui::menu_runtime::Hooks build_menu_runtime_hooks(const Hooks& composition)
{
    ui::menu_runtime::Hooks hooks{};
    hooks.format_time = format_menu_time;
    hooks.status_refresh_interval_ms = composition.status_refresh_interval_ms;
    return hooks;
}

void init_screen_saver_overlay()
{
    ui::components::screen_saver_overlay::Hooks overlay_hooks{};
    overlay_hooks.format_time = format_menu_time;
    overlay_hooks.read_unread_count = ui::status::get_total_unread;
    ui::components::screen_saver_overlay::init(overlay_hooks);
}

bool lock_ui(const Hooks& hooks)
{
    return hooks.lock_ui ? hooks.lock_ui(hooks.lock_timeout_ms) : true;
}

void unlock_ui(const Hooks& hooks)
{
    if (hooks.unlock_ui)
    {
        hooks.unlock_ui();
    }
}

} // namespace

bool beginBootUi(const Hooks& hooks, bool waking_from_sleep, const char* initial_line)
{
    std::printf("[BOOT][UI] show waking=%d line=%s\n",
                waking_from_sleep ? 1 : 0,
                initial_line ? initial_line : "");
    std::fflush(stdout);
    if (waking_from_sleep)
    {
        return true;
    }
    if (!lock_ui(hooks))
    {
        return false;
    }
    ui::boot::show();
    std::printf("[BOOT][UI] log line=%s\n", initial_line ? initial_line : "");
    std::fflush(stdout);
    ui::boot::set_log_line(initial_line);
    present_boot_overlay_now();
    unlock_ui(hooks);
    return true;
}

void prepareBootResources()
{
    ::ui::i18n::reload_language();
    ui::feedback::init();
}

bool initializeMenuSkeleton(const Hooks& hooks)
{
    if (s_shell_initialized)
    {
        return true;
    }
    if (!lock_ui(hooks))
    {
        return false;
    }

    lv_obj_t* active_screen = lv_screen_active();
    lv_obj_set_style_bg_color(active_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(active_screen, 0, 0);

    ui::menu_layout::InitOptions options{};
    options.apps = hooks.apps;
    if (hooks.ux_pack_id != nullptr &&
        ui_lvgl_ux::buildMenuForUxPack(hooks.ux_pack_id, s_ux_menu_model))
    {
        options.ux_menu = &s_ux_menu_model;
    }
    ui::menu_layout::init(options);
    ui::menu_runtime::init(
        lv_screen_active(), main_screen, ui::menu_layout::menuPanel(), build_menu_runtime_hooks(hooks));
    init_screen_saver_overlay();

    s_shell_initialized = true;
    unlock_ui(hooks);
    return true;
}

bool finalizeStartup(const Hooks& hooks, bool waking_from_sleep)
{
    std::printf("[BOOT][UI] ready waking=%d\n", waking_from_sleep ? 1 : 0);
    std::fflush(stdout);
    if (!lock_ui(hooks))
    {
        return false;
    }
    ui::boot::mark_ready();
    unlock_ui(hooks);
    return true;
}

} // namespace ui::startup_ui_shell
