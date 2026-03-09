#pragma once

#include <stddef.h>

#include "app/app_facades.h"
#include "platform/ui/screen_runtime.h"
#include "ui/app_catalog.h"
#include "ui/menu/menu_runtime.h"

namespace ui::startup_shell
{

struct Hooks
{
    app::IAppMessagingFacade* messaging = nullptr;
    AppCatalog apps{};
    menu_runtime::Hooks::WatchFaceHooks watch_face{};
    void (*set_max_brightness)() = nullptr;
    void (*show_main_menu)() = nullptr;
};

bool format_menu_time(char* out, size_t out_len);
menu_runtime::Hooks::WatchFaceHooks defaultWatchFaceHooks();
menu_runtime::Hooks buildMenuRuntimeHooks(const Hooks& hooks);
platform::ui::screen::Hooks buildScreenSleepHooks(const Hooks& hooks);
void prepareBootUi(bool waking_from_sleep);
void initializeShell(const Hooks& hooks);
void finalizeStartup(bool waking_from_sleep);

} // namespace ui::startup_shell
