#include "ui_presentation/settings/settings_model.h"

namespace ui::settings
{

SettingsModel::SettingsModel(ISettingsSource& source, ISettingsActionSink& sink)
    : source_(source),
      sink_(sink)
{
}

SettingsSnapshot SettingsModel::snapshot() const
{
    SettingsSnapshot out{};
    if (!source_.buildSettingsSnapshot(out))
    {
        out.header.valid = false;
    }
    return out;
}

ui::UiActionResult SettingsModel::apply(const SettingsPatchView& patch)
{
    return sink_.applySetting(patch);
}

} // namespace ui::settings
