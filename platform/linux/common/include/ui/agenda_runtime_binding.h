#pragma once
#include "platform/linux/agenda_source.h"
#include <memory>
class AppScreen;
namespace trailmate::linux_ui
{
// UI-thread composition. releaseUi() precedes LVGL teardown; the descriptor
// and stopped source must remain alive until the menu tree has been destroyed.
class AgendaRuntimeBinding
{
  public:
    explicit AgendaRuntimeBinding(::platform::linux_runtime::LinuxAgendaOptions);
    ~AgendaRuntimeBinding();
    AppScreen* application();
    const ::ui::map::MapMarkerBinding* markers();
    ::platform::linux_runtime::LinuxAgendaSource* source();
    void tick();
    void releaseUi();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trailmate::linux_ui
