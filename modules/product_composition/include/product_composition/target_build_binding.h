#pragma once

#include <cstddef>

namespace product_composition
{

struct TargetBuildBinding
{
    const char* target_id;
    const char* build_entrypoint;
    const char* app_shell;
    const char* sdkconfig_defaults;
    const char* status;
};

const TargetBuildBinding* findTargetBuildBinding(const char* target_id);
const TargetBuildBinding* allTargetBuildBindings(std::size_t* count);
const TargetBuildBinding* esp32LvglTargetBuildBindings(std::size_t* count);

} // namespace product_composition
