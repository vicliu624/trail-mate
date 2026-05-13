#pragma once

#include "ui_presentation/settings/settings_snapshot.h"

namespace ui::settings
{

class ISettingsSource
{
  public:
    virtual ~ISettingsSource() = default;

    virtual bool buildSettingsSnapshot(SettingsSnapshot& out) const = 0;
};

} // namespace ui::settings
