#pragma once

#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/common/snapshot_header.h"

#include <stdint.h>

namespace ui::mesh
{

enum class MeshDisplayState : uint8_t
{
    Off,
    Starting,
    Ready,
    Degraded,
    Error,
};

struct MeshStatusSnapshot
{
    ui::SnapshotHeader header;

    MeshDisplayState state = MeshDisplayState::Off;

    bool radio_ready = false;
    bool tx_busy = false;
    bool has_peer_key = false;
    bool team_linked = false;

    uint32_t known_nodes = 0;
    uint32_t unread_messages = 0;

    int16_t last_rssi = 0;
    int8_t last_snr = 0;

    ui::FixedText<32> protocol_label;
    ui::FixedText<32> radio_label;
    ui::FixedText<64> status_line;
};

} // namespace ui::mesh
