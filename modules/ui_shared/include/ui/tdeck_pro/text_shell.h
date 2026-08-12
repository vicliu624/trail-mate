#pragma once

#include "lvgl.h"
#include "ui/app_catalog.h"

namespace ui::tdeck_pro::text_shell
{

struct InitOptions
{
    AppCatalog apps{};
    // The shell asks its host to change menu visibility when an application
    // takes over.  The host owns menu timers/status lifecycle; the shell
    // never duplicates that runtime state.
    void (*set_menu_visible)(bool visible) = nullptr;
};

// This shell is a display adapter for the existing AppCatalog.  It owns the
// text list and focus widgets only; application state remains in AppScreen
// implementations and their existing shared services.
void init(const InitOptions& options);
lv_obj_t* menu_panel();
bool launch_app_by_stable_id(const char* stable_id);
void refresh_localized_text();
void set_visible(bool visible);
void bring_content_to_front();

// Header values are supplied by the existing menu runtime.  The text shell
// owns their widgets and decides their 240x320 placement.
void set_time_text(const char* text);
void set_battery_text(const char* text);
void set_walkie_recording(bool recording);

// Compatibility sinks for the shared menu runtime.  The text shell renders
// these as plain status text, never as coloured information chips.
void set_node_text(const char* text);
void set_help_text(const char* text);

} // namespace ui::tdeck_pro::text_shell
