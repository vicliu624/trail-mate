#pragma once

#include "linux_uconsole_gtk_app_shell.h"
#include "product_composition/presentation_bundle.h"
#include "ui_gtk_runtime/gtk_runtime_entry_adoption.h"
#include "ui_presentation/menu/menu_model.h"
#include "ui_presentation/screen/screen_binding_registry.h"

#include <cstddef>

namespace trailmate::apps::linux_uconsole_gtk
{

class LinuxUConsoleGtkRuntimeEntryAdoptionProbe
{
  public:
    bool load(const LinuxUConsoleGtkAppShell& shell);

    bool ready() const;
    bool fallbackUsed() const;
    std::size_t menuCount() const;
    std::size_t screenCount() const;

    const trailmate::uconsole::gtk::GtkRuntimeEntryAdoption& adoption() const;

  private:
    ui::menu::MenuModel menu_{};
    ui::screen::ScreenBindingRegistry screen_bindings_{};
    product_composition::PresentationBundle presentation_{};
    trailmate::uconsole::gtk::GtkRuntimeEntryAdoption adoption_{};
    bool ready_ = false;
    bool fallback_ = true;
};

} // namespace trailmate::apps::linux_uconsole_gtk
