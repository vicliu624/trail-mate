#include "platform/linux/runtime_paths.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <mutex>
#include <optional>

namespace platform::linux_runtime
{
namespace
{
struct ConfiguredPaths
{
    std::mutex mutex;
    std::optional<RuntimePaths> paths;
};
ConfiguredPaths& configured_paths() { static ConfiguredPaths value; return value; }

std::string sanitize_component(const char* value)
{
    if (!value || value[0] == '\0')
    {
        return "default";
    }

    std::string out;
    out.reserve(std::strlen(value));
    for (const unsigned char ch : std::string(value))
    {
        if (std::isalnum(ch) || ch == '_' || ch == '-')
        {
            out.push_back(static_cast<char>(ch));
        }
        else
        {
            out.push_back('_');
        }
    }
    return out.empty() ? "default" : out;
}

} // namespace

RuntimePaths resolve_paths()
{
    RuntimePaths paths{};
    {
        auto& configured = configured_paths();
        std::lock_guard<std::mutex> lock(configured.mutex);
        if (configured.paths) paths = *configured.paths;
    }

    // 1. Explicit overrides
    const char* settings_env = std::getenv("TRAIL_MATE_SETTINGS_ROOT");
    const char* sd_env = std::getenv("TRAIL_MATE_SD_ROOT");
    const char* cache_env = std::getenv("TRAIL_MATE_CACHE_ROOT");

    if (settings_env && settings_env[0] != '\0')
    {
        paths.settings_root = std::filesystem::path(settings_env);
    }
    if (sd_env && sd_env[0] != '\0')
    {
        paths.sd_root = std::filesystem::path(sd_env);
    }
    if (cache_env && cache_env[0] != '\0')
    {
        paths.cache_root = std::filesystem::path(cache_env);
    }

    // 2. Platform defaults for settings_root (if still empty)
    if (paths.settings_root.empty())
    {
#if defined(_WIN32)
        if (const char* appdata = std::getenv("APPDATA"))
        {
            if (appdata[0] != '\0')
            {
                paths.settings_root =
                    std::filesystem::path(appdata) / "TrailMateCardputerZero";
            }
        }
#endif

        if (paths.settings_root.empty())
        {
            if (const char* home = std::getenv("HOME"))
            {
                if (home[0] != '\0')
                {
                    paths.settings_root =
                        std::filesystem::path(home) / ".trailmate_cardputer_zero";
                }
            }
        }

        if (paths.settings_root.empty())
        {
            paths.settings_root =
                std::filesystem::current_path() / ".trailmate_cardputer_zero";
        }
    }

    // 3. sd_root defaults to <settings_root>/sdcard
    if (paths.sd_root.empty())
    {
        paths.sd_root = paths.settings_root / "sdcard";
    }

    // 4. cache_root defaults to <settings_root>/cache
    if (paths.cache_root.empty())
    {
        paths.cache_root = paths.settings_root / "cache";
    }

    // 5. state_root = settings_root
    paths.state_root = paths.settings_root;

    return paths;
}

RuntimePaths xdg_application_paths()
{
    const char* home = std::getenv("HOME");
    const auto base = home && home[0] ? std::filesystem::path(home) : std::filesystem::current_path();
    const auto choose = [&](const char* key, const std::filesystem::path& fallback)
    {
        const char* value = std::getenv(key);
        return value && value[0] && std::filesystem::path(value).is_absolute() ?
               std::filesystem::path(value) / "trailmate" : fallback / "trailmate";
    };
    RuntimePaths paths;
    paths.settings_root = choose("XDG_STATE_HOME", base / ".local" / "state");
    paths.state_root = paths.settings_root;
    paths.sd_root = choose("XDG_DATA_HOME", base / ".local" / "share");
    paths.cache_root = choose("XDG_CACHE_HOME", base / ".cache");
    return paths;
}

bool configure_runtime_paths(const RuntimePaths& paths)
{
    if (!paths.settings_root.is_absolute() || !paths.sd_root.is_absolute() || !paths.cache_root.is_absolute()) return false;
    auto& configured = configured_paths();
    std::lock_guard<std::mutex> lock(configured.mutex);
    if (configured.paths) return false;
    configured.paths = paths;
    return true;
}

std::filesystem::path settings_file(const char* ns)
{
    const RuntimePaths paths = resolve_paths();
    return paths.settings_root / "settings" /
           (sanitize_component(ns) + ".kv");
}

std::filesystem::path sqlite_database_path()
{
    const RuntimePaths paths = resolve_paths();
    return paths.settings_root / "trailmate.sqlite3";
}

std::filesystem::path sd_child(std::string_view relative)
{
    const RuntimePaths paths = resolve_paths();
    return paths.sd_root / relative;
}

std::filesystem::path resource_pack_root()
{
    const char* configured = std::getenv("TRAIL_MATE_RESOURCE_PACK_ROOT");
    if (configured && configured[0]) return std::filesystem::absolute(configured).lexically_normal();
    return std::filesystem::absolute(sd_child("trailmate/packs")).lexically_normal();
}

bool ensure_directory(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}

// ---------------------------------------------------------------------------
// Path safety
// ---------------------------------------------------------------------------

bool resolve_child_under_root(const std::filesystem::path& root,
                              std::string_view child,
                              std::filesystem::path& out)
{
    out.clear();

    if (child.empty())
    {
        return false;
    }

    // Reject absolute paths.
    const std::filesystem::path child_path(child);
    if (child_path.is_absolute())
    {
        return false;
    }

    // Reject ".." segments anywhere in the path.
    for (const auto& part : child_path)
    {
        if (part == "..")
        {
            return false;
        }
    }

    // Resolve and check containment.
    std::error_code ec;
    const std::filesystem::path joined = root / child_path;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(joined, ec);
    if (ec)
    {
        return false;
    }

    const std::filesystem::path canonical_root =
        std::filesystem::weakly_canonical(root, ec);
    if (ec)
    {
        return false;
    }

    // Ensure canonical starts with canonical_root.
    auto root_it = canonical_root.begin();
    auto path_it = canonical.begin();
    for (; root_it != canonical_root.end() && path_it != canonical.end();
         ++root_it, ++path_it)
    {
        if (*root_it != *path_it)
        {
            return false;
        }
    }
    if (root_it != canonical_root.end())
    {
        return false; // canonical is shorter than root
    }

    out = canonical;
    return true;
}

bool safe_remove_under_root(const std::filesystem::path& root,
                            std::string_view child)
{
    std::filesystem::path resolved;
    if (!resolve_child_under_root(root, child, resolved))
    {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(resolved, ec))
    {
        return true; // already gone
    }

    std::filesystem::remove(resolved, ec);
    return !ec;
}

bool safe_write_under_root(const std::filesystem::path& root,
                           std::string_view child,
                           const std::string& content)
{
    std::filesystem::path resolved;
    if (!resolve_child_under_root(root, child, resolved))
    {
        return false;
    }

    // Ensure parent directory exists.
    std::error_code ec;
    std::filesystem::create_directories(resolved.parent_path(), ec);
    if (ec)
    {
        return false;
    }

    // Write to temp file, close (which flushes user-space buffers), then
    // atomically rename.  On local filesystems rename is atomic; a full
    // fsync / FlushFileBuffers can be added later when power-loss safety
    // is required on real devices.
    const std::filesystem::path temp_path =
        resolved.string() + ".tmp";

    {
        std::ofstream stream(temp_path,
                             std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
        {
            return false;
        }
        stream << content;
        if (!stream.good())
        {
            stream.close();
            std::filesystem::remove(temp_path, ec);
            return false;
        }
        stream.close();
    }

    // Atomic rename.
    std::filesystem::remove(resolved, ec);
    ec.clear();
    std::filesystem::rename(temp_path, resolved, ec);
    if (ec)
    {
        std::filesystem::remove(temp_path, ec);
        return false;
    }

    return true;
}

} // namespace platform::linux_runtime
