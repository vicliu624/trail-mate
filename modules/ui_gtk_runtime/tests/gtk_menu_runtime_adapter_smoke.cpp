#include "uconsole_composition_root.h"
#include "ui_gtk_runtime/gtk_menu_runtime_adapter.h"

#include "product_composition/presentation_bundle.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>
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
        "trailmate_gtk_menu_runtime_adapter_smoke";

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

    trailmate::uconsole::GtkMenuRuntimeAdapter adapter;
    trailmate::uconsole::GtkMenuDescriptor descriptors[ui::menu::MenuModel::kMaxItems]{};
    std::size_t count = 0;

    assert(adapter.buildDescriptors(*root.presentation().ux_menu,
                                    descriptors,
                                    ui::menu::MenuModel::kMaxItems,
                                    count));
    assert(count > 0);

    bool found_map = false;
    for (std::size_t index = 0; index < count; ++index)
    {
        assert(descriptors[index].enabled);
        assert(descriptors[index].route.valid);
        assert(descriptors[index].route.screen_id == descriptors[index].screen_id);
        assert(descriptors[index].label != nullptr);
        if (descriptors[index].screen_id == ui::menu::MenuScreenId::Map)
        {
            found_map = true;
        }
    }

    assert(found_map);
    root.shutdown();
    std::filesystem::remove_all(root_dir, ec);
    return 0;
}
