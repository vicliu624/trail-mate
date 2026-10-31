#pragma once
#include <cstddef>
#include <cstdint>
enum trail_mate_t_display_p4_audio_owner_t { TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_FM = 6 };
bool trail_mate_t_display_p4_audio_begin(trail_mate_t_display_p4_audio_owner_t, uint32_t);
bool trail_mate_t_display_p4_audio_write_mono(trail_mate_t_display_p4_audio_owner_t, const int16_t*, size_t);
bool trail_mate_t_display_p4_audio_end_checked(trail_mate_t_display_p4_audio_owner_t);
