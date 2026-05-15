#include "ui/startup_shell.h"

#include <ctime>

#include "lvgl.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/time_runtime.h"
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/menu/menu_layout.h"
#include "ui/menu/menu_runtime.h"
#include "ui/ui_boot.h"
#include "ui/ui_common.h"
#include "ui/ui_status.h"
#include "ui/watch_face.h"
#include "ui/widgets/system_notification.h"
#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

namespace ui::startup_shell
{
namespace
{

ui::menu::MenuModel s_ux_menu_model;

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

void present_boot_overlay_now()
{
    lv_timer_handler();
    lv_refr_now(nullptr);
}

} // namespace

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
    strftime(out, out_len, "%H:%M", &info);
    return true;
}

menu_runtime::Hooks::WatchFaceHooks defaultWatchFaceHooks()
{
    menu_runtime::Hooks::WatchFaceHooks hooks{};
    hooks.create = watch_face_create;
    hooks.set_time = watch_face_set_time;
    hooks.set_node_id = watch_face_set_node_id;
    hooks.show = watch_face_show;
    hooks.is_ready = watch_face_is_ready;
    hooks.set_unlock_cb = watch_face_set_unlock_cb;
    return hooks;
}

menu_runtime::Hooks buildMenuRuntimeHooks(const Hooks& hooks)
{
    menu_runtime::Hooks runtime_hooks{};
    runtime_hooks.format_time = format_menu_time;
    runtime_hooks.show_main_menu = hooks.show_main_menu;

    const auto default_watch_face = defaultWatchFaceHooks();
    runtime_hooks.watch_face = hooks.watch_face;
    if (!runtime_hooks.watch_face.create)
    {
        runtime_hooks.watch_face.create = default_watch_face.create;
    }
    if (!runtime_hooks.watch_face.set_time)
    {
        runtime_hooks.watch_face.set_time = default_watch_face.set_time;
    }
    if (!runtime_hooks.watch_face.set_node_id)
    {
        runtime_hooks.watch_face.set_node_id = default_watch_face.set_node_id;
    }
    if (!runtime_hooks.watch_face.show)
    {
        runtime_hooks.watch_face.show = default_watch_face.show;
    }
    if (!runtime_hooks.watch_face.is_ready)
    {
        runtime_hooks.watch_face.is_ready = default_watch_face.is_ready;
    }
    if (!runtime_hooks.watch_face.set_unlock_cb)
    {
        runtime_hooks.watch_face.set_unlock_cb = default_watch_face.set_unlock_cb;
    }
    return runtime_hooks;
}

platform::ui::screen::Hooks buildScreenSleepHooks(const Hooks& hooks)
{
    platform::ui::screen::Hooks runtime_hooks{};
    runtime_hooks.format_time = format_menu_time;
    runtime_hooks.read_unread_count = ui::status::get_total_unread;
    runtime_hooks.show_main_menu = hooks.show_main_menu;
    runtime_hooks.on_wake_from_sleep = ui::menu_runtime::onWakeFromSleep;
    return runtime_hooks;
}

void prepareBootUi(bool waking_from_sleep)
{
    if (!waking_from_sleep)
    {
        ui::boot::show();
        ui::boot::set_log_line("Loading language packs...");
        present_boot_overlay_now();
    }
    ::ui::i18n::reload_language();
    ui::SystemNotification::init();
}

void initializeShell(const Hooks& hooks)
{
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(lv_screen_active(), 0, 0);

    ui::menu_layout::InitOptions menu_options{};
    menu_options.messaging = hooks.messaging;
    menu_options.apps = hooks.apps;
    if (hooks.ux_pack_id != nullptr &&
        ui_lvgl_ux::buildMenuForUxPack(hooks.ux_pack_id, s_ux_menu_model))
    {
        menu_options.ux_menu = &s_ux_menu_model;
    }
    ui::menu_layout::init(menu_options);

    ui::menu_runtime::init(
        lv_screen_active(), main_screen, ui::menu_layout::menuPanel(), buildMenuRuntimeHooks(hooks));

    if (hooks.set_max_brightness)
    {
        hooks.set_max_brightness();
    }

    ui::menu_runtime::showWatchFace();
    platform::ui::screen::init(buildScreenSleepHooks(hooks));
}

void finalizeStartup(bool waking_from_sleep)
{
    if (waking_from_sleep)
    {
        platform::ui::screen::update_user_activity();
    }

    ui::boot::mark_ready();
}

} // namespace ui::startup_shell
