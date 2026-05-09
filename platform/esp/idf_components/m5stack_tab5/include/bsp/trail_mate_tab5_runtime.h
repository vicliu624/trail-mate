#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    bool trail_mate_tab5_display_runtime_init(void);
    bool trail_mate_tab5_display_runtime_is_ready(void);
    bool trail_mate_tab5_touch_interrupt_active(void);
    bool trail_mate_tab5_display_lock(uint32_t timeout_ms);
    void trail_mate_tab5_display_unlock(void);
    void trail_mate_tab5_set_ext_5v_enabled(bool enabled);
    void trail_mate_tab5_set_wifi_power_enabled(bool enabled);

#ifdef __cplusplus
}
#endif
