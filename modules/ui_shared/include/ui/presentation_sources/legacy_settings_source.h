#pragma once

#include "ui_presentation/settings/settings_source.h"

namespace ui::presentation_sources
{

class LegacySettingsSource final : public ui::settings::ISettingsSource
{
  public:
    bool buildSettingsSnapshot(ui::settings::SettingsSnapshot& out) const override;
};

LegacySettingsSource& legacy_settings_source();

} // namespace ui::presentation_sources
