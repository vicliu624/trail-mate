#pragma once

#include "ui_presentation/common/ui_action_result.h"
#include "ui_presentation/settings/settings_snapshot.h"

namespace ui::settings
{

class ISettingsActionSink
{
  public:
    virtual ~ISettingsActionSink() = default;

    virtual ui::UiActionResult applySetting(const SettingsPatchView& patch) = 0;
};

} // namespace ui::settings
