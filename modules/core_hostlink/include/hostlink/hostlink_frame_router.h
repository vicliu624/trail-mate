#pragma once

#include "hostlink/hostlink_codec.h"
#include "hostlink/hostlink_types.h"

namespace hostlink
{

enum class HostlinkCommandId : uint8_t
{
    None = 0,
    TxMsg,
    GetConfig,
    SetConfig,
    SetTime,
    GetGps,
    TxAppData,
};

enum class HostlinkFrameDecisionType : uint8_t
{
    RejectNotReady = 0,
    CompleteHello,
    DispatchCommand,
    Unsupported,
};

struct HostlinkFrameDecision
{
    HostlinkFrameDecisionType type = HostlinkFrameDecisionType::Unsupported;
    HostlinkCommandId command = HostlinkCommandId::None;
    ErrorCode error = ErrorCode::Unsupported;
};

HostlinkFrameDecision route_frame(const Frame& frame, bool ready);

} // namespace hostlink
