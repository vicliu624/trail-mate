#include "linux_uconsole_gtk_app_shell.h"

#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

#include <cassert>
#include <cstring>

int main()
{
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkAppShell shell;
    assert(shell.validate());

    const auto& config = shell.config();
    assert(std::strcmp(config.target_id, "uconsole") == 0);
    assert(std::strcmp(config.ux_pack_id, "uconsole_desktop") == 0);
    assert(std::strcmp(shell.activeUxPackId(), "uconsole_desktop") == 0);
    assert(std::strcmp(config.transitional_source, "apps/linux_uconsole") == 0);
    assert(std::strcmp(config.legacy_adapter_target,
                       "trailmate_linux_uconsole_legacy_adapter") == 0);
    assert(ui_lvgl_ux::findUxPackById(shell.activeUxPackId()) != nullptr);
    return 0;
}
