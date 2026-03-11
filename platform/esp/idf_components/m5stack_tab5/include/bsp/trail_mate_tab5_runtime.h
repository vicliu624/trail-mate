#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    bool trail_mate_tab5_display_runtime_init(void);
    bool trail_mate_tab5_display_runtime_is_ready(void);
    bool trail_mate_tab5_touch_interrupt_active(void);

#ifdef __cplusplus
}
#endif
