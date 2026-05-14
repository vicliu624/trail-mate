#pragma once

#include "lvgl.h"

#include "app/app_facades.h"
#include "ui/app_catalog.h"
#include "ui_lvgl_ux_packs/ux/ux_menu_model.h"

namespace ui
{
namespace menu_layout
{

struct InitOptions
{
    app::IAppMessagingFacade* messaging = nullptr;
    AppCatalog apps{};
    const ui_lvgl_ux::UxMenuModel* ux_menu = nullptr;
};

void init(const InitOptions& options);
lv_obj_t* menuPanel();
void bringContentToFront();
void refresh_localized_text();
void set_bottom_bar_node_text(const char* text);
void set_bottom_bar_ram_text(const char* text);
void set_bottom_bar_psram_text(const char* text);
void set_bottom_bar_psram_visible(bool visible);
void setMenuVisible(bool visible);

} // namespace menu_layout
} // namespace ui
