#include "platform/ui/route_storage.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "platform/linux/runtime_paths.h"

namespace platform::ui::route_storage
{
namespace
{

std::string s_route_dir_cache{};

std::filesystem::path route_dir_path()
{
    return ::platform::linux_runtime::resolve_paths().sd_root / "routes";
}

bool ensure_route_dir()
{
    std::error_code ec;
    const std::filesystem::path dir = route_dir_path();
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        return false;
    }
    s_route_dir_cache = dir.string();
    return true;
}

std::filesystem::path resolve_route_path(const std::string& path_or_name)
{
    std::filesystem::path candidate(path_or_name);
    if (candidate.is_absolute())
    {
        return candidate;
    }
    return route_dir_path() / candidate;
}

} // namespace

bool is_supported()
{
    return ensure_route_dir();
}

bool list_routes(std::vector<std::string>& out_routes, std::size_t max_count)
{
    out_routes.clear();
    if (!ensure_route_dir())
    {
        return false;
    }

    std::error_code ec;
    for (std::filesystem::directory_iterator it(route_dir_path(), ec), end;
         !ec && it != end; it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (!it->is_regular_file(ec) || ec)
        {
            continue;
        }
        out_routes.push_back(it->path().filename().string());
    }

    std::sort(out_routes.begin(), out_routes.end());
    if (out_routes.size() > max_count)
    {
        out_routes.resize(max_count);
    }
    return true;
}

bool remove_route(const std::string& path)
{
    // Prevent absolute paths or traversal — only operate under the routes dir.
    std::filesystem::path resolved;
    if (!::platform::linux_runtime::resolve_child_under_root(
            route_dir_path(), path, resolved))
    {
        return false;
    }

    std::error_code ec;
    return std::filesystem::remove(resolved, ec) && !ec;
}

const char* route_dir()
{
    if (s_route_dir_cache.empty())
    {
        (void)ensure_route_dir();
    }
    return s_route_dir_cache.c_str();
}

} // namespace platform::ui::route_storage
