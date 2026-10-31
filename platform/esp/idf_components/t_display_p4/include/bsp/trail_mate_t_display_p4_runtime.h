#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    bool trail_mate_t_display_p4_display_runtime_init(void);
    bool trail_mate_t_display_p4_display_runtime_is_ready(void);
    bool trail_mate_t_display_p4_display_lock(uint32_t timeout_ms);
    void trail_mate_t_display_p4_display_unlock(void);
    esp_err_t trail_mate_t_display_p4_display_set_brightness_percent(int brightness_percent);

    typedef enum
    {
        TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_NONE = 0,
        TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_NOTIFICATION = 1,
        TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_RETICULUM_CALL = 2,
        TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_WALKIE = 3,
        TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_SSTV = 4,
        TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_MUSIC = 5,
        TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_FM = 6,
    } trail_mate_t_display_p4_audio_owner_t;

    bool trail_mate_t_display_p4_audio_is_ready(void);
    // A failed begin can retain a quarantined session when physical rollback
    // fails. The owning worker must call end_checked even after failed begin.
    bool trail_mate_t_display_p4_audio_begin(trail_mate_t_display_p4_audio_owner_t owner,
                                             uint32_t sample_rate_hz);
    void trail_mate_t_display_p4_audio_end(trail_mate_t_display_p4_audio_owner_t owner);
    // Idempotent when the requested owner is absent; never closes another owner.
    // A failed physical close retains ownership/focus
    // and blocks PCM/reopening until the owning worker successfully retries.
    bool trail_mate_t_display_p4_audio_end_checked(trail_mate_t_display_p4_audio_owner_t owner);
    bool trail_mate_t_display_p4_audio_read_mono(trail_mate_t_display_p4_audio_owner_t owner,
                                                 int16_t* pcm,
                                                 size_t sample_count);
    bool trail_mate_t_display_p4_audio_write_mono(trail_mate_t_display_p4_audio_owner_t owner,
                                                  const int16_t* pcm,
                                                  size_t sample_count);
    void trail_mate_t_display_p4_audio_set_volume_percent(uint8_t volume_percent);
    bool trail_mate_t_display_p4_audio_set_session_volume(trail_mate_t_display_p4_audio_owner_t owner, uint8_t volume_percent);
    bool trail_mate_t_display_p4_audio_set_input_gain(trail_mate_t_display_p4_audio_owner_t owner,
                                                      float gain_db);

#ifdef __cplusplus
}
#endif
