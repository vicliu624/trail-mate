#include "platform/ui/route_storage.h"

#include "platform/esp/idf_common/bsp_runtime.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>

namespace platform::ui::route_storage
{
namespace
{

constexpr const char* kRouteDir = "/routes";

bool has_kml_extension(const std::string& name)
{
    if (name.size() < 4)
    {
        return false;
    }
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });
    return lower.compare(lower.size() - 4, 4, ".kml") == 0;
}

bool is_regular_file(const std::string& path)
{
    struct stat st
    {
    };
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

} // namespace

bool is_supported()
{
    return platform::esp::idf_common::bsp_runtime::sdcard_capable();
}

bool list_routes(std::vector<std::string>& out_routes, std::size_t max_count)
{
    out_routes.clear();
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        return false;
    }

    const std::string base = std::string(platform::esp::idf_common::bsp_runtime::sdcard_mount_point()) + kRouteDir;
    DIR* dir = ::opendir(base.c_str());
    if (dir == nullptr)
    {
        return false;
    }

    while (out_routes.size() < max_count)
    {
        dirent* entry = ::readdir(dir);
        if (entry == nullptr)
        {
            break;
        }
        const char* name_c = entry->d_name;
        if (name_c == nullptr || ::strcmp(name_c, ".") == 0 || ::strcmp(name_c, "..") == 0)
        {
            continue;
        }
        std::string name = name_c;
        if (!has_kml_extension(name))
        {
            continue;
        }
        const std::string full_path = base + "/" + name;
        if (is_regular_file(full_path))
        {
            out_routes.push_back(name);
        }
    }
    ::closedir(dir);
    std::sort(out_routes.begin(), out_routes.end());
    return true;
}

bool remove_route(const std::string& path)
{
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready() || path.empty())
    {
        return false;
    }
    const std::string mount_prefixed = std::string(platform::esp::idf_common::bsp_runtime::sdcard_mount_point()) + path;
    return std::remove(mount_prefixed.c_str()) == 0;
}

const char* route_dir()
{
    return kRouteDir;
}

} // namespace platform::ui::route_storage