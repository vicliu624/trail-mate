#pragma once

#include <cstddef>
#include <cstdint>

namespace hostlink
{

constexpr uint8_t kMagic0 = 'H';
constexpr uint8_t kMagic1 = 'L';
constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kHeaderSize = 8;
constexpr size_t kCrcSize = 2;
constexpr size_t kMaxFrameLen = 512;

enum class FrameType : uint8_t
{
    Hello = 0x01,
    HelloAck = 0x02,
    Ack = 0x03,

    CmdTxMsg = 0x10,
    CmdGetConfig = 0x11,
    CmdSetConfig = 0x12,
    CmdSetTime = 0x13,
    CmdGetGps = 0x14,

    EvRxMsg = 0x80,
    EvTxResult = 0x81,
    EvStatus = 0x82,
    EvLog = 0x83,
    EvGps = 0x84,
    EvAppData = 0x85,
};

enum class ErrorCode : uint8_t
{
    Ok = 0,
    BadCrc = 1,
    Unsupported = 2,
    Busy = 3,
    InvalidParam = 4,
    NotInMode = 5,
    Internal = 6,
};

enum Capabilities : uint32_t
{
    CapTxMsg = 1u << 0,
    CapConfig = 1u << 1,
    CapSetTime = 1u << 2,
    CapStatus = 1u << 3,
    CapLogs = 1u << 4,
    CapGps = 1u << 5,
    CapAppData = 1u << 6,
};

enum class ConfigKey : uint8_t
{
    MeshProtocol = 1,
    Region = 2,
    Channel = 3,
    DutyCycle = 4,
    ChannelUtil = 5,
};

enum class StatusKey : uint8_t
{
    Battery = 1,
    Charging = 2,
    LinkState = 3,
    MeshProtocol = 4,
    Region = 5,
    Channel = 6,
    DutyCycle = 7,
    ChannelUtil = 8,
    LastError = 9,
};

} // namespace hostlink
