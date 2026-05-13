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
    uint32_t packet_id = 0;
    ByteView channel_key;
    uint8_t channel_hash = 0;
    uint8_t hop_limit = 2;
    bool has_air_want_ack = false;
    bool air_want_ack = false;
    bool include_payload_dest = true;
    bool require_local_identity = true;
    bool require_peer_key = true;

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
