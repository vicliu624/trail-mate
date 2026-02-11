#pragma once

#include <cstdint>

enum class VisMode : uint8_t
{
    Unknown = 0,
    Scottie1,
    Scottie2,
    ScottieDX,
    Robot72,
    Robot36,
    Martin1,
    Martin2,
    Pd50,
    Pd90,
    Pd120,
    Pd160,
    Pd180,
    Pd240,
    Pd290,
    P3,
    P5,
    P7,
};

struct VisModeInfo
{
    VisMode mode = VisMode::Unknown;
    float color_ms = 0.0f;
    float pd_scan_ms = 0.0f;
    float p_sync_ms = 0.0f;
    float p_porch_ms = 0.0f;
    float p_color_ms = 0.0f;
    int line_count = 0;
};

const char* vis_mode_name(VisMode mode);
