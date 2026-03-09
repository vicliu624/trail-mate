#include "platform/ui/route_storage.h"

#include "platform/ui/device_runtime.h"

#include <SD.h>
#include <algorithm>
#include <cctype>

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
    {
        return static_cast<char>(std::tolower(ch));
    });
    return lower.compare(lower.size() - 4, 4, ".kml") == 0;
}

} // namespace

bool is_supported()
{
    return true;
}

bool list_routes(std::vector<std::string>& out_routes, std::size_t max_count)
{
    out_routes.clear();
    if (!platform::ui::device::sd_ready())
    {
        return false;
    }

    File dir = SD.open(kRouteDir);
    if (!dir || !dir.isDirectory())
    {
        return false;
    }

    for (File file = dir.openNextFile(); file && out_routes.size() < max_count; file = dir.openNextFile())
    {
        if (!file.isDirectory())
        {
            const char* raw_name = file.name();
            std::string name = raw_name ? raw_name : "";
            if (has_kml_extension(name))
            {
                out_routes.push_back(name);
            }
        }
        file.close();
    }
    dir.close();

    std::sort(out_routes.begin(), out_routes.end());
    return true;
}

bool remove_route(const std::string& path)
{
    if (!platform::ui::device::sd_ready() || path.empty())
    {
        return false;
    }
    return SD.remove(path.c_str());
}

const char* route_dir()
{
    return kRouteDir;
}

} // namespace platform::ui::route_storage