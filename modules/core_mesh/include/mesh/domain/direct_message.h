#pragma once

#include "mesh/domain/mesh_packet.h"
#include "mesh/domain/node_id.h"

#include <stdint.h>

namespace mesh
{

struct DirectMessageCommand
{
    NodeId from;
    NodeId to;
    ByteView payload;
    bool request_ack = true;
    uint32_t application_port = 0;

    DirectMessageCommand() = default;

    DirectMessageCommand(NodeId command_to, ByteView command_payload, bool command_request_ack)
        : from(),
          to(command_to),
          payload(command_payload),
          request_ack(command_request_ack),
          application_port(0)
    {
    }
};

enum class SendFailure : uint8_t
{
    None = 0,
    InvalidInput,
    LocalIdentityMissing,
    PeerKeyMissing,
    CryptoFailed,
    PacketBuildFailed,
    RadioSendFailed,
    AckTimeout,
    ProtocolUnsupported,
    NotReady,
};

struct SendResult
{
    bool ok = false;
    SendFailure failure = SendFailure::None;

    SendResult() = default;

    SendResult(bool result_ok, SendFailure result_failure)
        : ok(result_ok),
          failure(result_failure)
    {
    }

    static SendResult success()
    {
        return SendResult(true, SendFailure::None);
    }

    static SendResult fail(SendFailure failure)
    {
        return SendResult(false, failure);
    }
};

} // namespace mesh
