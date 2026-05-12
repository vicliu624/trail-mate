#include "mesh/protocol/meshtastic/meshtastic_protocol_strategy.h"

#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include <cstring>

namespace mesh
{
namespace meshtastic
{
namespace
{

RadioConfig mapRadioConfig(const ::chat::meshtastic::RadioConfig& config)
{
    RadioConfig out{};
    out.frequency_hz = static_cast<uint32_t>(config.freq_mhz * 1000000.0f);
    out.bandwidth_hz = static_cast<uint32_t>(config.bw_khz * 1000.0f);
    out.spreading_factor = config.sf;
    out.coding_rate = config.cr_denom;
    out.sync_word = config.sync_word;
    out.tx_power_dbm = config.tx_power_dbm;
    return out;
}

uint32_t packetIdFromContext(const ProtocolBuildContext& context)
{
    if (context.packet_id != 0)
    {
        return context.packet_id;
    }
    return context.now_ms != 0 ? context.now_ms : 1U;
}

uint8_t hopLimitFromContext(const ProtocolBuildContext& context)
{
    return context.hop_limit == 0 ? 2 : context.hop_limit;
}

uint32_t portFromCommand(const DirectMessageCommand& command)
{
    return command.application_port != 0
               ? command.application_port
               : static_cast<uint32_t>(meshtastic_PortNum_TEXT_MESSAGE_APP);
}

bool encodeAppData(const ProtocolBuildContext& context,
                   const DirectMessageCommand& command,
                   uint8_t* out,
                   size_t* out_size)
{
    if (!out || !out_size || command.payload.size > sizeof(meshtastic_Data::payload.bytes))
    {
        return false;
    }
    meshtastic_Data data = meshtastic_Data_init_default;
    data.portnum = static_cast<meshtastic_PortNum>(portFromCommand(command));
    data.want_response = command.request_ack;
    data.has_bitfield = true;
    data.bitfield = 0;
    if (context.include_payload_dest)
    {
        data.dest = command.to.value;
    }
    data.payload.size = static_cast<pb_size_t>(command.payload.size);
    std::memcpy(data.payload.bytes, command.payload.data, command.payload.size);

    pb_ostream_t stream = pb_ostream_from_buffer(out, *out_size);
    if (!pb_encode(&stream, meshtastic_Data_fields, &data))
    {
        return false;
    }
    *out_size = stream.bytes_written;
    return true;
}

} // namespace

MeshProtocolKind MeshtasticProtocolStrategy::kind() const
{
    return MeshProtocolKind::Meshtastic;
}

RadioConfig MeshtasticProtocolStrategy::deriveRadioConfig(const MeshRuntimeConfig& config)
{
    if (config.radio.frequency_hz != 0)
    {
        return config.radio;
    }
    ::chat::MeshConfig default_config;
    auto derived = ::chat::meshtastic::deriveRadioConfig(default_config);
    return mapRadioConfig(derived);
}

ProtocolResult MeshtasticProtocolStrategy::buildDirectMessage(
    const ProtocolBuildContext& context,
    const DirectMessageCommand& command,
    EncodedPacket& out)
{
    if (!context.local_node.isValidUnicast() ||
        (!command.to.isValidUnicast() && !command.to.isBroadcast()) ||
        command.payload.empty())
    {
        return ProtocolResult::fail(ProtocolFailure::InvalidInput);
    }

    uint8_t data[256]{};
    size_t data_size = sizeof(data);
    if (!encodeAppData(context, command, data, &data_size))
    {
        return ProtocolResult::fail(ProtocolFailure::EncodeFailed);
    }

    out.size = sizeof(out.bytes);
    const bool want_ack = context.has_air_want_ack
                              ? context.air_want_ack
                              : ::chat::meshtastic::shouldSetAirWantAck(command.to.value,
                                                                        command.request_ack);
    const uint8_t* psk = context.channel_key.data;
    const size_t psk_len = context.channel_key.size;
    if (!::chat::meshtastic::buildWirePacket(data,
                                             data_size,
                                             context.local_node.value,
                                             packetIdFromContext(context),
                                             command.to.value,
                                             context.channel_hash,
                                             hopLimitFromContext(context),
                                             want_ack,
                                             psk,
                                             psk_len,
                                             out.bytes,
                                             &out.size))
    {
        return ProtocolResult::fail(ProtocolFailure::EncodeFailed);
    }
    return ProtocolResult::success();
}

ProtocolResult MeshtasticProtocolStrategy::parseRadioPacket(const RadioRxPacket& packet,
                                                            MeshProtocolEvent& out)
{
    if (packet.size == 0 || packet.size > sizeof(packet.bytes))
    {
        return ProtocolResult::fail(ProtocolFailure::InvalidInput);
    }

    ::chat::meshtastic::PacketHeaderWire header{};
    uint8_t payload[256]{};
    size_t payload_size = sizeof(payload);
    if (!::chat::meshtastic::parseWirePacket(packet.bytes,
                                             packet.size,
                                             &header,
                                             payload,
                                             &payload_size))
    {
        return ProtocolResult::fail(ProtocolFailure::DecodeFailed);
    }

    meshtastic_Data data = meshtastic_Data_init_default;
    pb_istream_t stream = pb_istream_from_buffer(payload, payload_size);
    if (!pb_decode(&stream, meshtastic_Data_fields, &data))
    {
        return ProtocolResult::fail(ProtocolFailure::DecodeFailed);
    }

    out = MeshProtocolEvent{};
    out.kind = MeshProtocolEventKind::MessageReceived;
    out.peer = NodeId{header.from};
    out.packet_id = header.id;
    out.setPayload(data.payload.bytes, data.payload.size);
    return ProtocolResult::success();
}

} // namespace meshtastic
} // namespace mesh
