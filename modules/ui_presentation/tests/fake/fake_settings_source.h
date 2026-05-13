#pragma once

#include "ui_presentation/settings/settings_source.h"

namespace ui::tests
{

class FakeSettingsSource final : public ui::settings::ISettingsSource
{
  public:
    bool buildSettingsSnapshot(ui::settings::SettingsSnapshot& out) const override
    {
        if (!available)
        {
            return false;
        }
        out = snapshot_value;
        return true;
    }

    bool available = true;
    ui::settings::SettingsSnapshot snapshot_value{};
};

} // namespace ui::tests
