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
    EvTeamState = 0x86,
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
    CapTeamState = 1u << 7,
    CapAprsGateway = 1u << 8,
};

enum class ConfigKey : uint8_t
{
    MeshProtocol = 1,
    Region = 2,
    Channel = 3,
    DutyCycle = 4,
    ChannelUtil = 5,
    AprsEnable = 20,
    AprsIgateCallsign = 21,
    AprsIgateSsid = 22,
    AprsToCall = 23,
    AprsPath = 24,
    AprsTxMinIntervalSec = 25,
    AprsDedupeWindowSec = 26,
    AprsSymbolTable = 27,
    AprsSymbolCode = 28,
    AprsPositionIntervalSec = 29,
    AprsNodeIdMap = 30,
    AprsSelfEnable = 31,
    AprsSelfCallsign = 32,
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
    AprsEnable = 20,
    AprsIgateCallsign = 21,
    AprsIgateSsid = 22,
    AprsToCall = 23,
    AprsPath = 24,
    AprsTxMinIntervalSec = 25,
    AprsDedupeWindowSec = 26,
    AprsSymbolTable = 27,
    AprsSymbolCode = 28,
    AprsPositionIntervalSec = 29,
    AprsNodeIdMap = 30,
    AprsSelfEnable = 31,
    AprsSelfCallsign = 32,
    AppRxTotal = 40,
    AppRxFromIs = 41,
    AppRxDirect = 42,
    AppRxRelayed = 43,
};

enum class AppDataMetaKey : uint8_t
{
    RxTimestampS = 1,
    RxTimestampMs = 2,
    RxTimeSource = 3,
    Direct = 4,
    HopCount = 5,
    HopLimit = 6,
    RxOrigin = 7,
    FromIs = 8,
    RssiDbmX10 = 9,
    SnrDbX10 = 10,
    FreqHz = 11,
    BwHz = 12,
    Sf = 13,
    Cr = 14,
    PacketId = 15,
    ChannelHash = 16,
    WireFlags = 17,
    NextHop = 18,
    RelayNode = 19,
};

} // namespace hostlink
