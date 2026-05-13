#pragma once

#include "ui_presentation/settings/settings_action_sink.h"
#include "ui_presentation/settings/settings_source.h"

namespace ui::settings
{

class SettingsModel
{
  public:
    SettingsModel(ISettingsSource& source, ISettingsActionSink& sink);

    SettingsSnapshot snapshot() const;
    ui::UiActionResult apply(const SettingsPatchView& patch);

  private:
    ISettingsSource& source_;
    ISettingsActionSink& sink_;
};

} // namespace ui::settings
