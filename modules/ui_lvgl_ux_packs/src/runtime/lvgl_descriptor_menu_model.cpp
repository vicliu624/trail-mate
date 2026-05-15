#include "ui_lvgl_ux_packs/runtime/lvgl_descriptor_menu_model.h"

namespace ui_lvgl_ux
{

namespace
{

const char* bindingForScreen(const LvglRuntimeScreenGraphPresenter& presenter,
                             ui::menu::MenuScreenId screen_id)
{
    for (std::size_t index = 0; index < presenter.screenCount(); ++index)
    {
        const LvglScreenHostEntry& screen = presenter.screenEntries()[index];
        if (screen.screen_id == screen_id && screen.available &&
            screen.binding_id != nullptr)
        {
            return screen.binding_id;
        }
    }
    return nullptr;
}

} // namespace

bool LvglDescriptorMenuModel::load(
    const LvglPrimaryScreenGraphRuntime& runtime)
{
    item_count_ = 0;
    if (!runtime.usingPrimaryScreenGraph())
    {
        return false;
    }

    const LvglRuntimeScreenGraphPresenter& presenter =
        runtime.adoption().presenter();
    for (std::size_t index = 0;
         index < presenter.menuCount() &&
         item_count_ < ui::menu::MenuModel::kMaxItems;
         ++index)
    {
        const LvglMenuEntry& menu = presenter.menuEntries()[index];
        const char* binding = bindingForScreen(presenter, menu.screen_id);
        if (binding == nullptr)
        {
            continue;
        }

        items_[item_count_].label = menu.label;
        items_[item_count_].binding_id = binding;
        items_[item_count_].enabled = menu.enabled;
        ++item_count_;
    }

    return item_count_ > 0;
}

std::size_t LvglDescriptorMenuModel::itemCount() const noexcept
{
    return item_count_;
}

const LvglDescriptorMenuItem* LvglDescriptorMenuModel::items() const noexcept
{
    return items_;
}

} // namespace ui_lvgl_ux
