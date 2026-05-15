#pragma once

#include "product_composition/presentation_bundle.h"
#include "ui_gtk_runtime/gtk_uconsole_screen_graph_presenter.h"

#include <cstddef>

namespace trailmate::uconsole::gtk
{

class GtkRuntimeEntryAdoption
{
  public:
    bool load(const product_composition::PresentationBundle& presentation);

    bool ready() const;
    std::size_t menuCount() const;
    std::size_t screenCount() const;

    const GtkUConsoleScreenGraphPresenter& presenter() const;

  private:
    GtkUConsoleScreenGraphPresenter presenter_{};
    bool ready_ = false;
};

} // namespace trailmate::uconsole::gtk
