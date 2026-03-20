#pragma once

#include "boards/tab5/heading_runtime.h"

namespace platform::esp::idf_common::tab5_heading_runtime
{
using HeadingState = ::boards::tab5::heading_runtime::HeadingState;
inline void ensure_started() { ::boards::tab5::heading_runtime::ensure_started(); }
inline HeadingState get_data() { return ::boards::tab5::heading_runtime::get_data(); }
} // namespace platform::esp::idf_common::tab5_heading_runtime
