#pragma once

#include <cstddef>

namespace product_composition
{

struct TargetBuildBinding
{
    const char* target_id = nullptr;
    const char* build_entrypoint = nullptr;
    const char* app_shell = nullptr;
    const char* sdkconfig_defaults = nullptr;
};

const TargetBuildBinding* findTargetBuildBinding(const char* target_id);
const TargetBuildBinding* esp32LvglTargetBuildBindings(std::size_t* count);

} // namespace product_composition
