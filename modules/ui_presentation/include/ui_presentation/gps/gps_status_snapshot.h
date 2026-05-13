#pragma once

#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/common/snapshot_header.h"

#include <stdint.h>

namespace ui::gps
{

struct GpsStatusSnapshot
{
    ui::SnapshotHeader header;

    bool receiver_enabled = false;
    bool receiver_powered = false;

    bool fix_valid = false;
    double latitude = 0.0;
    double longitude = 0.0;
    float altitude_m = 0.0f;
    float speed_mps = 0.0f;
    float course_deg = 0.0f;

    uint8_t satellites = 0;
    float hdop = 0.0f;

    bool time_synced = false;
    uint64_t epoch_seconds = 0;

    ui::FixedText<32> fix_label;
    ui::FixedText<32> coordinate_label;
    ui::FixedText<32> satellite_label;
    ui::FixedText<32> time_label;
};

} // namespace ui::gps
