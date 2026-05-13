#pragma once

#include "ui_presentation/settings/settings_action_sink.h"

namespace ui::presentation_sources
{

class LegacySettingsActionSink final : public ui::settings::ISettingsActionSink
{
  public:
    ui::UiActionResult applySetting(const ui::settings::SettingsPatchView& patch) override;
};

LegacySettingsActionSink& legacy_settings_action_sink();

} // namespace ui::presentation_sources
