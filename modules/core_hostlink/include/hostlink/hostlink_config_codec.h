#pragma once

#include "hostlink/hostlink_types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hostlink
{

constexpr size_t kAprsIgateCallsignCapacity = 16;
constexpr size_t kAprsToCallCapacity = 16;
constexpr size_t kAprsPathCapacity = 64;
constexpr size_t kAprsNodeMapCapacity = 255;
constexpr size_t kAprsSelfCallsignCapacity = 16;

struct RxStatsSnapshot
{
    uint32_t total = 0;
    uint32_t from_is = 0;
    uint32_t direct = 0;
    uint32_t relayed = 0;
};

struct AprsConfigSnapshot
{
    bool enabled = false;
    char igate_callsign[kAprsIgateCallsignCapacity] = {0};
    uint8_t igate_ssid = 0;
    char tocall[kAprsToCallCapacity] = {0};
    char path[kAprsPathCapacity] = {0};
    uint16_t tx_min_interval_s = 0;
    uint16_t dedupe_window_s = 0;
    char symbol_table = 0;
    char symbol_code = 0;
    uint16_t position_interval_s = 0;
    uint8_t node_map_len = 0;
    uint8_t node_map[kAprsNodeMapCapacity] = {0};
    bool self_enable = false;
    char self_callsign[kAprsSelfCallsignCapacity] = {0};
};

struct StatusPayloadSnapshot
{
    uint8_t battery = 0xFF;
    bool charging = false;
    uint8_t link_state = 0;
    uint32_t last_error = 0;
    uint8_t mesh_protocol = 0;
    uint8_t region = 0;
    uint8_t channel = 0;
    bool duty_cycle = false;
    uint8_t channel_util = 0;
    RxStatsSnapshot app_rx{};
    AprsConfigSnapshot aprs{};
};

struct ConfigPatch
{
    bool has_mesh_protocol = false;
    uint8_t mesh_protocol = 0;

    bool has_region = false;
    uint8_t region = 0;

    bool has_channel = false;
    uint8_t channel = 0;

    bool has_duty_cycle = false;
    bool duty_cycle = false;

    bool has_channel_util = false;
    uint8_t channel_util = 0;

    bool has_aprs_enable = false;
    bool aprs_enable = false;

    bool has_aprs_igate_callsign = false;
    char aprs_igate_callsign[kAprsIgateCallsignCapacity] = {0};

    bool has_aprs_igate_ssid = false;
    uint8_t aprs_igate_ssid = 0;

    bool has_aprs_tocall = false;
    char aprs_tocall[kAprsToCallCapacity] = {0};

    bool has_aprs_path = false;
    char aprs_path[kAprsPathCapacity] = {0};

    bool has_aprs_tx_min_interval_s = false;
    uint16_t aprs_tx_min_interval_s = 0;

    bool has_aprs_dedupe_window_s = false;
    uint16_t aprs_dedupe_window_s = 0;

    bool has_aprs_symbol_table = false;
    char aprs_symbol_table = 0;

    bool has_aprs_symbol_code = false;
    char aprs_symbol_code = 0;

    bool has_aprs_position_interval_s = false;
    uint16_t aprs_position_interval_s = 0;

    bool has_aprs_node_map = false;
    uint8_t aprs_node_map_len = 0;
    uint8_t aprs_node_map[kAprsNodeMapCapacity] = {0};

    bool has_aprs_self_enable = false;
    bool aprs_self_enable = false;

    bool has_aprs_self_callsign = false;
    char aprs_self_callsign[kAprsSelfCallsignCapacity] = {0};
};

bool encode_status_payload(std::vector<uint8_t>& out,
                           const StatusPayloadSnapshot& snapshot,
                           bool include_config);

bool decode_config_patch(const uint8_t* data, size_t len, ConfigPatch& out);

} // namespace hostlink
