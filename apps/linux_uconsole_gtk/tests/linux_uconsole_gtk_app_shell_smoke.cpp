#include "linux_uconsole_gtk_app_shell.h"
#include "linux_uconsole_gtk_historical_source_descriptor.h"

#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"
#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"
#include "ui_presentation/menu/menu_model.h"

#include <cassert>
#include <cstring>

int main()
{
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkAppShell shell;
    assert(shell.validate());

    const auto& config = shell.config();
    assert(std::strcmp(config.target_id, "uconsole") == 0);
    assert(std::strcmp(shell.targetId(), "uconsole") == 0);
    assert(std::strcmp(config.ux_pack_id, "uconsole_desktop") == 0);
    assert(std::strcmp(shell.activeUxPackId(), "uconsole_desktop") == 0);
    assert(shell.targetProfile() != nullptr);
    assert(shell.targetProfile()->renderer ==
           product_composition::TargetRenderer::Gtk);
    const auto& descriptor =
        trailmate::apps::linux_uconsole_gtk::
            linuxUConsoleGtkHistoricalSourceDescriptor();
    assert(std::strcmp(config.historical_source,
                       descriptor.historical_root_name) == 0);
    assert(std::strcmp(descriptor.historical_role,
                       "pre-refactor uConsole GTK implementation root") == 0);
    assert(std::strcmp(descriptor.replacement_owner,
                       "apps/linux_uconsole_gtk + modules/ui_gtk_runtime") == 0);
    assert(ui_lvgl_ux::findUxPackById(shell.activeUxPackId()) != nullptr);

    ui::menu::MenuModel menu;
    assert(ui_lvgl_ux::buildMenuForUxPack(shell.activeUxPackId(), menu));
    assert(menu.size() > 0);
    return 0;
}
