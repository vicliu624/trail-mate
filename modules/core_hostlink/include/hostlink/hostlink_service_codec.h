#pragma once

#include "hostlink/hostlink_codec.h"
#include "hostlink/hostlink_types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hostlink
{

enum class PendingCommandType : uint8_t
{
    TxMsg = 1,
    TxAppData = 2,
};

struct PendingCommand
{
    PendingCommandType type = PendingCommandType::TxMsg;
    uint32_t to = 0;
    uint32_t portnum = 0;
    uint8_t channel = 0;
    uint8_t flags = 0;
    uint16_t payload_len = 0;
    uint8_t payload[kMaxFrameLen] = {};
};

struct HelloAckPayloadInfo
{
    uint16_t protocol_version = kProtocolVersion;
    uint16_t max_frame_len = static_cast<uint16_t>(kMaxFrameLen);
    uint32_t capabilities = 0;
    const char* model = nullptr;
    const char* firmware = nullptr;
};

struct GpsPayloadSnapshot
{
    bool valid = false;
    bool has_alt = false;
    bool has_speed = false;
    bool has_course = false;
    uint8_t satellites = 0;
    uint32_t age = 0;
    double lat = 0.0;
    double lng = 0.0;
    double alt_m = 0.0;
    double speed_mps = 0.0;
    double course_deg = 0.0;
};

bool build_hello_ack_payload(std::vector<uint8_t>& out, const HelloAckPayloadInfo& info);
bool build_gps_payload(std::vector<uint8_t>& out, const GpsPayloadSnapshot& snapshot);

bool parse_u64_le(const uint8_t* data, size_t len, size_t& off, uint64_t& out);
bool parse_tx_msg_command(const Frame& frame, uint8_t max_channels, PendingCommand& out);
bool parse_tx_app_data_command(const Frame& frame,
                               uint8_t max_channels,
                               size_t team_id_size,
                               PendingCommand& out);

} // namespace hostlink
