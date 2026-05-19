#include "uconsole_composition_root.h"
#include "ui_gtk_runtime/gtk_menu_runtime_adapter.h"
#include "ui_gtk_runtime/gtk_screen_host_adapter.h"

#include "product_composition/presentation_bundle.h"

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

} // namespace

int main()
{
    const auto root_dir =
        std::filesystem::temp_directory_path() /
        "trailmate_gtk_screen_host_adapter_smoke";

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
    assert(root.presentation().ux_menu != nullptr);
    assert(product_composition::hasUxMenu(root.presentation()));
    assert(product_composition::hasScreenBindings(root.presentation()));

    trailmate::uconsole::GtkMenuRuntimeAdapter menu_adapter;
    trailmate::uconsole::GtkMenuDescriptor descriptors[ui::menu::MenuModel::kMaxItems]{};
    std::size_t descriptor_count = 0;
    assert(menu_adapter.buildDescriptors(*root.presentation().ux_menu,
                                         descriptors,
                                         ui::menu::MenuModel::kMaxItems,
                                         descriptor_count));
    assert(descriptor_count > 0);

    const trailmate::uconsole::GtkMenuDescriptor* map_descriptor = nullptr;
    for (std::size_t index = 0; index < descriptor_count; ++index)
    {
        if (descriptors[index].screen_id == ui::menu::MenuScreenId::Map)
        {
            map_descriptor = &descriptors[index];
            break;
        }
    }

    assert(map_descriptor != nullptr);
    assert(map_descriptor->route.valid);
    const ui::screen::ScreenRoute route = map_descriptor->route;

    trailmate::uconsole::GtkScreenHostAdapter screen_host;
    trailmate::uconsole::GtkScreenDescriptor screen;
    assert(screen_host.resolve(route, screen));
    assert(screen.available);
    assert(screen.screen_id == ui::menu::MenuScreenId::Map);
    assert(std::strcmp(screen.binding_id, "map") == 0);
    assert(root.presentation().screen_bindings->find(screen.screen_id) != nullptr);

    root.shutdown();
    std::filesystem::remove_all(root_dir, ec);
    return 0;
}
