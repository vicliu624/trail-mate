#pragma once

#include "lvgl.h"

#include "app/app_facades.h"
#include "ui/app_catalog.h"

namespace ui
{
namespace menu_layout
{

struct InitOptions
{
    app::IAppMessagingFacade* messaging = nullptr;
    AppCatalog apps{};
};

void init(const InitOptions& options);
lv_obj_t* menuPanel();
void bringContentToFront();
void setMenuVisible(bool visible);

} // namespace menu_layout
} // namespace ui
