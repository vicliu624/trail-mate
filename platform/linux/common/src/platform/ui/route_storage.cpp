#include "platform/ui/route_storage.h"

namespace platform::ui::route_storage
{
namespace
{

constexpr const char* kRouteDir = "/routes";

} // namespace

bool is_supported()
{
    return false;
}

bool list_routes(std::vector<std::string>& out_routes, std::size_t)
{
    out_routes.clear();
    return false;
}

bool remove_route(const std::string&)
{
    return false;
}

const char* route_dir()
{
    return kRouteDir;
}

} // namespace platform::ui::route_storage
