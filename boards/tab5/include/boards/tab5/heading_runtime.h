#pragma once

#include <cstdint>

#include "platform/ui/orientation_runtime.h"

namespace boards::tab5::heading_runtime
{

using HeadingState = ::platform::ui::orientation::HeadingState;

void ensure_started();
HeadingState get_data();

} // namespace boards::tab5::heading_runtime
