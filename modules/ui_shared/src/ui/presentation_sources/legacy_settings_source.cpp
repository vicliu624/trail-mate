#include "ui/presentation_sources/legacy_settings_source.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "sys/clock.h"

namespace ui::presentation_sources
{

bool LegacySettingsSource::buildSettingsSnapshot(ui::settings::SettingsSnapshot& out) const
{
    out = ui::settings::SettingsSnapshot{};
    out.header.valid = true;
    out.header.version = 1;
    out.header.generated_at_ms = sys::millis_now();

    out.section_count = 1;
    ui::copyText(out.sections[0].title, "GPS");
    out.sections[0].option_count = 1;
    ui::copyText(out.sections[0].options[0].key, "gps_enabled");
    ui::copyText(out.sections[0].options[0].label, "GPS Enabled");
    ui::copyText(out.sections[0].options[0].value_label,
                 app::configFacade().getConfig().gps_enabled ? "ON" : "OFF");
    out.sections[0].options[0].control = ui::settings::SettingControlKind::Toggle;
    return true;
}

LegacySettingsSource& legacy_settings_source()
{
    static LegacySettingsSource source;
    return source;
}

} // namespace ui::presentation_sources
