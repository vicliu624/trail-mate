#include "app/linux_app_services.h"
#include "platform/ui/settings_store.h"
#include "uconsole/uconsole_map_workspace_model.h"

#include <cassert>
#include <cmath>
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
    const auto root =
        std::filesystem::temp_directory_path() /
        "trailmate_uconsole_map_workspace_smoke";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "settings", ec);
    std::filesystem::create_directories(root / "sd", ec);
    std::filesystem::create_directories(root / "cache", ec);

    set_env_var("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").string());
    set_env_var("TRAIL_MATE_SD_ROOT", (root / "sd").string());
    set_env_var("TRAIL_MATE_CACHE_ROOT", (root / "cache").string());
    set_env_var("TRAIL_MATE_GPS_VALID", "0");
    set_env_var("TRAIL_MATE_MAP_LAT", "");
    set_env_var("TRAIL_MATE_MAP_LNG", "");

    ::platform::ui::settings_store::clear_namespace("uconsole_map");

    trailmate::linux_app::LinuxAppServices services;
    trailmate::uconsole::UConsoleMapWorkspaceModel model(services);
    const auto snapshot = model.snapshot();

    assert(snapshot.has_center);
    assert(!snapshot.has_fix);
    assert(snapshot.using_default_center);
    assert(!snapshot.has_configured_center);
    assert(snapshot.zoom == 2);
    assert(std::abs(snapshot.lat) < 0.000001);
    assert(std::abs(snapshot.lon) < 0.000001);
    assert(snapshot.source_label == "OSM");
    assert(snapshot.columns == 5U);
    assert(snapshot.rows == 3U);
    assert(snapshot.center_tile_index == 7U);
    assert(snapshot.tiles.size() == 15U);
    for (const auto& tile : snapshot.tiles)
    {
        assert(tile.id.source == ::platform::linux_runtime::MapBaseSource::Osm);
        assert(tile.id.z == 2);
    }

    set_env_var("TRAIL_MATE_MAP_LAT", "31.2304");
    set_env_var("TRAIL_MATE_MAP_LNG", "121.4737");
    model.setZoom(20);
    assert(model.snapshot().zoom == 18);
    model.setZoom(0);
    assert(model.snapshot().zoom == 1);

    std::filesystem::remove_all(root, ec);
    return 0;
}
