#pragma once

#include "ui_presentation/settings/settings_action_sink.h"

namespace ui::tests
{

class FakeSettingsActionSink final : public ui::settings::ISettingsActionSink
{
  public:
    ui::UiActionResult applySetting(const ui::settings::SettingsPatchView& patch) override
    {
        ++apply_count;
        last_patch = patch;
        return result;
    }

    int apply_count = 0;
    ui::settings::SettingsPatchView last_patch{};
    ui::UiActionResult result = ui::UiActionResult::success();
};

} // namespace ui::tests
