#include "platform/linux/runtime_paths.h"
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

int main()
{
    using namespace platform::linux_runtime;
    unsetenv("TRAIL_MATE_SETTINGS_ROOT");
    unsetenv("TRAIL_MATE_SD_ROOT");
    unsetenv("TRAIL_MATE_CACHE_ROOT");
    const auto legacy = resolve_paths();
    if (legacy.settings_root.filename() != ".trailmate_cardputer_zero") return 1;
    char name[] = "/tmp/trailmate-paths-test-XXXXXX";
    if (!mkdtemp(name)) return 2;
    const std::filesystem::path base(name);
    setenv("XDG_STATE_HOME", (base / "state").c_str(), 1);
    setenv("XDG_DATA_HOME", (base / "data").c_str(), 1);
    setenv("XDG_CACHE_HOME", (base / "cache").c_str(), 1);
    const auto paths = xdg_application_paths();
    if (paths.settings_root != base / "state/trailmate" || paths.sd_root != base / "data/trailmate" ||
        paths.cache_root != base / "cache/trailmate") return 3;
    auto invalid = paths;
    invalid.sd_root = "relative";
    if (configure_runtime_paths(invalid) || !configure_runtime_paths(paths) || configure_runtime_paths(paths)) return 4;
    const auto configured = resolve_paths();
    if (configured.sd_root != paths.sd_root || configured.settings_root != paths.settings_root) return 5;
    setenv("TRAIL_MATE_SD_ROOT", (base / "override").c_str(), 1);
    if (resolve_paths().sd_root != base / "override") return 6;
    if (std::filesystem::exists(paths.sd_root) || std::filesystem::exists(paths.settings_root)) return 7;
    std::filesystem::remove_all(base);
    return 0;
}
