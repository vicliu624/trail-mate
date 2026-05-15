#pragma once

#include "product_composition/presentation_bundle.h"
#include "ui_gtk_runtime/gtk_uconsole_screen_graph_bridge.h"

#include <cstddef>

namespace trailmate::uconsole::gtk
{

class GtkUConsoleScreenGraphPresenter
{
  public:
    bool load(const product_composition::PresentationBundle& presentation);

    std::size_t menuCount() const;
    std::size_t screenCount() const;

    const trailmate::uconsole::GtkMenuDescriptor* menuDescriptors() const;
    const trailmate::uconsole::GtkScreenDescriptor* screenDescriptors() const;

  private:
    GtkUConsoleScreenGraphBridge bridge_{};
};

} // namespace trailmate::uconsole::gtk
