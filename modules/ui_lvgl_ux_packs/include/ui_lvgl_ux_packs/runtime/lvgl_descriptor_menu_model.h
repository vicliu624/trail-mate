#pragma once

#include "ui_lvgl_ux_packs/runtime/lvgl_primary_screen_graph_runtime.h"
#include "ui_presentation/menu/menu_model.h"

#include <cstddef>

namespace ui_lvgl_ux
{

struct LvglDescriptorMenuItem
{
    const char* label = nullptr;
    const char* binding_id = nullptr;
    bool enabled = false;
};

class LvglDescriptorMenuModel
{
  public:
    bool load(const LvglPrimaryScreenGraphRuntime& runtime);

    std::size_t itemCount() const noexcept;
    const LvglDescriptorMenuItem* items() const noexcept;

  private:
    LvglDescriptorMenuItem items_[ui::menu::MenuModel::kMaxItems]{};
    std::size_t item_count_ = 0;
};

} // namespace ui_lvgl_ux
