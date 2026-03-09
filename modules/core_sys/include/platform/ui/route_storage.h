#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace platform::ui::route_storage
{

bool is_supported();
bool list_routes(std::vector<std::string>& out_routes, std::size_t max_count = 64);
bool remove_route(const std::string& path);
const char* route_dir();

} // namespace platform::ui::route_storage