#pragma once

#include <stdint.h>

namespace platform::esp::arduino_common::display_runtime
{

void initialize();
void tickIfDue(uint32_t now_ms);

} // namespace platform::esp::arduino_common::display_runtime
