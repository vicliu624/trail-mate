#include "ui_gtk_runtime/gtk_descriptor_page_registry.h"

namespace trailmate::uconsole
{

namespace
{

const GtkMenuDescriptor* menuForScreen(
    const gtk::GtkUConsoleScreenGraphPresenter& presenter,
    ui::menu::MenuScreenId screen_id)
{
    for (std::size_t index = 0; index < presenter.menuCount(); ++index)
    {
        const GtkMenuDescriptor& menu = presenter.menuDescriptors()[index];
        if (menu.screen_id == screen_id)
        {
            return &menu;
        }
    }
    return nullptr;
}

} // namespace

bool GtkDescriptorPageRegistry::load(
    const gtk::GtkRuntimeEntryAdoption& adoption)
{
    page_count_ = 0;
    if (!adoption.ready())
    {
        return false;
    }

    const gtk::GtkUConsoleScreenGraphPresenter& presenter = adoption.presenter();
    for (std::size_t index = 0;
         index < presenter.screenCount() &&
         page_count_ < ui::menu::MenuModel::kMaxItems;
         ++index)
    {
        const GtkScreenDescriptor& screen = presenter.screenDescriptors()[index];
        if (!screen.available || screen.binding_id == nullptr)
        {
            continue;
        }

        const GtkMenuDescriptor* menu =
            menuForScreen(presenter, screen.screen_id);
        pages_[page_count_].binding_id = screen.binding_id;
        pages_[page_count_].label =
            menu != nullptr && menu->label != nullptr ? menu->label : screen.binding_id;
        pages_[page_count_].enabled = menu == nullptr || menu->enabled;
        ++page_count_;
    }

    return page_count_ > 0;
}

std::size_t GtkDescriptorPageRegistry::pageCount() const noexcept
{
    return page_count_;
}

const GtkDescriptorPage* GtkDescriptorPageRegistry::pages() const noexcept
{
    return pages_;
}

} // namespace trailmate::uconsole
