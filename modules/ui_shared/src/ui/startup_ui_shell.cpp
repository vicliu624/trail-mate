#include "ui/startup_ui_shell.h"

#include <ctime>

#include "lvgl.h"
#include "platform/ui/time_runtime.h"
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/menu/menu_layout.h"
#include "ui/menu/menu_runtime.h"
#include "ui/ui_boot.h"
#include "ui/widgets/system_notification.h"

namespace ui::startup_ui_shell
{
namespace
{

bool s_shell_initialized = false;

void present_boot_overlay_now()
{
    lv_timer_handler();
    lv_refr_now(nullptr);
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

ui::menu_runtime::Hooks build_menu_runtime_hooks()
{
    ui::menu_runtime::Hooks hooks{};
    hooks.format_time = format_menu_time;
    return hooks;
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

bool prepareBootUi(const Hooks& hooks, bool waking_from_sleep)
{
    if (waking_from_sleep)
    {
        ::ui::i18n::reload_language();
        return true;
    }
    if (!lock_ui(hooks))
    {
        return false;
    }
    ui::boot::show();
    ui::boot::set_log_line("Loading language packs...");
    present_boot_overlay_now();
    unlock_ui(hooks);
    ::ui::i18n::reload_language();
    ui::SystemNotification::init();
    return true;
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
    ui::menu_layout::init(options);
    ui::menu_runtime::init(
        lv_screen_active(), main_screen, ui::menu_layout::menuPanel(), build_menu_runtime_hooks());

    s_shell_initialized = true;
    unlock_ui(hooks);
    return true;
}

bool finalizeStartup(const Hooks& hooks, bool waking_from_sleep)
{
    (void)waking_from_sleep;
    if (!lock_ui(hooks))
    {
        return false;
    }
    ui::boot::mark_ready();
    unlock_ui(hooks);
    return true;
}

} // namespace ui::startup_ui_shell
