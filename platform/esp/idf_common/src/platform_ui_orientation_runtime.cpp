#include "platform/ui/orientation_runtime.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "boards/tab5/heading_runtime.h"
#endif

namespace platform::ui::orientation
{

HeadingState get_heading()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return ::boards::tab5::heading_runtime::get_data();
#else
    return {};
#endif
}

void ensure_heading_runtime()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    ::boards::tab5::heading_runtime::ensure_started();
#endif
}

} // namespace platform::ui::orientation
