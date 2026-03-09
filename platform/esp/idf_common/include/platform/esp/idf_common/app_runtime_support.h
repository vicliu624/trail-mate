#pragma once

#include <cstddef>

namespace platform::esp::idf_common
{

void tickBoundLifecycle(std::size_t max_events = 32);

} // namespace platform::esp::idf_common
