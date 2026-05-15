#pragma once

#include "product_composition/presentation_bundle.h"
#include "ui_lvgl_ux_packs/runtime/lvgl_menu_runtime_adapter.h"
#include "ui_lvgl_ux_packs/runtime/lvgl_screen_host_adapter.h"

#include <cstddef>

namespace ui_lvgl_ux
{

class LvglScreenGraphBridge
{
  public:
    bool load(const product_composition::PresentationBundle& presentation);

    std::size_t menuCount() const;
    std::size_t screenCount() const;

    const LvglMenuEntry* menuItems() const;
    const LvglScreenHostEntry* screenItems() const;

  private:
    LvglMenuRuntimeAdapter menu_adapter_{};
    LvglScreenHostAdapter screen_host_{};
    LvglMenuEntry menu_items_[ui::menu::MenuModel::kMaxItems]{};
    LvglScreenHostEntry screen_items_[ui::menu::MenuModel::kMaxItems]{};
    std::size_t menu_count_ = 0;
    std::size_t screen_count_ = 0;
};

} // namespace ui_lvgl_ux
