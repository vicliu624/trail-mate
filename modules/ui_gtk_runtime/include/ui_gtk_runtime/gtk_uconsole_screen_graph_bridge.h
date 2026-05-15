#pragma once

#include "product_composition/presentation_bundle.h"
#include "ui_gtk_runtime/gtk_menu_runtime_adapter.h"
#include "ui_gtk_runtime/gtk_screen_host_adapter.h"

#include <cstddef>

namespace trailmate::uconsole::gtk
{

class GtkUConsoleScreenGraphBridge
{
  public:
    bool load(const product_composition::PresentationBundle& presentation);

    std::size_t menuCount() const;
    std::size_t screenBindingCount() const;

    const trailmate::uconsole::GtkMenuDescriptor* menuItems() const;
    const trailmate::uconsole::GtkScreenDescriptor* screenItems() const;

  private:
    trailmate::uconsole::GtkMenuRuntimeAdapter menu_adapter_{};
    trailmate::uconsole::GtkScreenHostAdapter screen_host_{};
    trailmate::uconsole::GtkMenuDescriptor menu_items_[ui::menu::MenuModel::kMaxItems]{};
    trailmate::uconsole::GtkScreenDescriptor screen_items_[ui::menu::MenuModel::kMaxItems]{};
    std::size_t menu_count_ = 0;
    std::size_t screen_binding_count_ = 0;
};

} // namespace trailmate::uconsole::gtk
