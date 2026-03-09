#include "hostlink/hostlink_frame_router.h"

namespace hostlink
{

namespace
{

HostlinkFrameDecision make_decision(HostlinkFrameDecisionType type,
                                    HostlinkCommandId command,
                                    ErrorCode error)
{
    HostlinkFrameDecision decision;
    decision.type = type;
    decision.command = command;
    decision.error = error;
    return decision;
}

} // namespace

HostlinkFrameDecision route_frame(const Frame& frame, bool ready)
{
    if (!ready && frame.type != static_cast<uint8_t>(FrameType::Hello))
    {
        return make_decision(HostlinkFrameDecisionType::RejectNotReady,
                             HostlinkCommandId::None,
                             ErrorCode::NotInMode);
    }

    if (frame.type == static_cast<uint8_t>(FrameType::Hello))
    {
        return make_decision(HostlinkFrameDecisionType::CompleteHello,
                             HostlinkCommandId::None,
                             ErrorCode::Ok);
    }

    switch (static_cast<FrameType>(frame.type))
    {
    case FrameType::CmdTxMsg:
        return make_decision(HostlinkFrameDecisionType::DispatchCommand,
                             HostlinkCommandId::TxMsg,
                             ErrorCode::Ok);
    case FrameType::CmdGetConfig:
        return make_decision(HostlinkFrameDecisionType::DispatchCommand,
                             HostlinkCommandId::GetConfig,
                             ErrorCode::Ok);
    case FrameType::CmdSetConfig:
        return make_decision(HostlinkFrameDecisionType::DispatchCommand,
                             HostlinkCommandId::SetConfig,
                             ErrorCode::Ok);
    case FrameType::CmdSetTime:
        return make_decision(HostlinkFrameDecisionType::DispatchCommand,
                             HostlinkCommandId::SetTime,
                             ErrorCode::Ok);
    case FrameType::CmdGetGps:
        return make_decision(HostlinkFrameDecisionType::DispatchCommand,
                             HostlinkCommandId::GetGps,
                             ErrorCode::Ok);
    case FrameType::CmdTxAppData:
        return make_decision(HostlinkFrameDecisionType::DispatchCommand,
                             HostlinkCommandId::TxAppData,
                             ErrorCode::Ok);
    default:
        return make_decision(HostlinkFrameDecisionType::Unsupported,
                             HostlinkCommandId::None,
                             ErrorCode::Unsupported);
    }
}

} // namespace hostlink
