#pragma once

#include "linux_sim_app_shell.h"
#include "product_composition/presentation_bundle.h"
#include "ui_ascii_runtime/ascii_runtime_entry_adoption.h"
#include "ui_presentation/menu/menu_model.h"
#include "ui_presentation/screen/screen_binding_registry.h"

#include <cstddef>

namespace trailmate::apps::linux_sim_shell
{

class LinuxSimRuntimeEntryAdoptionProbe
{
  public:
    bool load(const LinuxSimAppShell& shell);

    bool ready() const;
    bool fallbackUsed() const;
    std::size_t menuCount() const;
    std::size_t screenCount() const;

    const trailmate::linux_sim::AsciiRuntimeEntryAdoption& adoption() const;

  private:
    ui::menu::MenuModel menu_{};
    ui::screen::ScreenBindingRegistry screen_bindings_{};
    product_composition::PresentationBundle presentation_{};
    trailmate::linux_sim::AsciiRuntimeEntryAdoption adoption_{};
    bool ready_ = false;
    bool fallback_ = true;
};

} // namespace trailmate::apps::linux_sim_shell
