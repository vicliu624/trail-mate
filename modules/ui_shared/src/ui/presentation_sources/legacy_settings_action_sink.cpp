#include "ui/presentation_sources/legacy_settings_action_sink.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "platform/ui/gps_runtime.h"

#include <cstring>

namespace ui::presentation_sources
{
namespace
{

bool keyEquals(const ui::settings::SettingsPatchView& patch, const char* key)
{
    return std::strcmp(patch.key.c_str(), key) == 0;
}

bool parseBool(const char* text, bool& out)
{
    if (!text)
    {
        return false;
    }
    if (std::strcmp(text, "1") == 0 || std::strcmp(text, "true") == 0 ||
        std::strcmp(text, "TRUE") == 0 || std::strcmp(text, "on") == 0 ||
        std::strcmp(text, "ON") == 0)
    {
        out = true;
        return true;
    }
    if (std::strcmp(text, "0") == 0 || std::strcmp(text, "false") == 0 ||
        std::strcmp(text, "FALSE") == 0 || std::strcmp(text, "off") == 0 ||
        std::strcmp(text, "OFF") == 0)
    {
        out = false;
        return true;
    }
    return false;
}

} // namespace

ui::UiActionResult LegacySettingsActionSink::applySetting(
    const ui::settings::SettingsPatchView& patch)
{
    if (keyEquals(patch, "gps_enabled") || keyEquals(patch, "gps.enabled"))
    {
        bool enabled = false;
        if (!parseBool(patch.value.c_str(), enabled))
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }

        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().gps_enabled = enabled;
        app_ctx.saveConfig();
        ::platform::ui::gps::set_enabled(enabled);
        return ui::UiActionResult::success();
    }

    return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
}

LegacySettingsActionSink& legacy_settings_action_sink()
{
    static LegacySettingsActionSink sink;
    return sink;
}

} // namespace ui::presentation_sources
