#include "uconsole_composition_root.h"

#include "product_composition/presentation_bundle.h"
#include "ui_gtk_runtime/gtk_uconsole_screen_graph_presenter.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace
{

void set_env_var(const char* name, const std::string& value)
{
    setenv(name, value.c_str(), 1);
}

bool containsScreen(
    const trailmate::uconsole::gtk::GtkUConsoleScreenGraphPresenter& presenter,
    ui::menu::MenuScreenId screen_id,
    const char* binding_id)
{
    for (std::size_t index = 0; index < presenter.screenCount(); ++index)
    {
        const trailmate::uconsole::GtkScreenDescriptor& screen =
            presenter.screenDescriptors()[index];
        if (screen.screen_id == screen_id && screen.available &&
            screen.binding_id != nullptr &&
            std::strcmp(screen.binding_id, binding_id) == 0)
        {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    const auto root_dir =
        std::filesystem::temp_directory_path() /
        "trailmate_gtk_uconsole_screen_graph_presenter_smoke";

    std::error_code ec;
    std::filesystem::remove_all(root_dir, ec);
    std::filesystem::create_directories(root_dir / "settings", ec);
    std::filesystem::create_directories(root_dir / "sd", ec);
    std::filesystem::create_directories(root_dir / "cache", ec);

    set_env_var("TRAIL_MATE_SETTINGS_ROOT",
                (root_dir / "settings").string());
    set_env_var("TRAIL_MATE_SD_ROOT", (root_dir / "sd").string());
    set_env_var("TRAIL_MATE_CACHE_ROOT", (root_dir / "cache").string());
    set_env_var("TRAIL_MATE_LORA_DISABLE", "1");
    set_env_var("TRAIL_MATE_GPS_VALID", "0");

    trailmate::uconsole::UConsoleCompositionRoot root;
    assert(root.initialize());
    assert(product_composition::hasUxMenu(root.presentation()));
    assert(product_composition::hasScreenBindings(root.presentation()));

    trailmate::uconsole::gtk::GtkUConsoleScreenGraphPresenter presenter;
    assert(presenter.load(root.presentation()));
    assert(presenter.menuCount() > 0);
    assert(presenter.screenCount() > 0);
    assert(presenter.menuDescriptors()[0].route.valid);

    assert(containsScreen(presenter, ui::menu::MenuScreenId::Chat, "chat"));
    assert(containsScreen(presenter, ui::menu::MenuScreenId::Map, "map"));
    assert(containsScreen(presenter, ui::menu::MenuScreenId::Settings, "settings"));

    root.shutdown();
    std::filesystem::remove_all(root_dir, ec);
    return 0;
}
