#include "fake_phone_runtime_context.h"
#include "phone/meshcore/meshcore_phone_core.h"
#include "phone/meshtastic/meshtastic_phone_session.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "meshtastic/admin.pb.h"
#include "meshtastic/portnums.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

namespace
{

constexpr uint8_t kMeshCoreResponseError = 1;
constexpr uint8_t kMeshCoreErrorIllegalArg = 6;

class FakeMeshtasticTransport final : public phone::meshtastic::MeshtasticPhoneTransport
{
  public:
    bool isBleConnected() const override { return connected; }
    void notifyFromNum(uint32_t from_num) override
    {
        last_from_num = from_num;
        notify_count++;
    }

    bool connected = true;
    uint32_t last_from_num = 0;
    int notify_count = 0;
};

void copyBounded(char* dst, size_t dst_len, const char* src)
{
    if (!dst || dst_len == 0)
    {
        return;
    }
    size_t i = 0;
    for (; src && src[i] != '\0' && i + 1 < dst_len; ++i)
    {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

bool encodeAdminSetChannelToRadio(uint8_t* out, size_t out_len, size_t& written, uint32_t packet_id)
{
    meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
    admin.which_payload_variant = meshtastic_AdminMessage_set_channel_tag;
    admin.set_channel.index = 1;
    admin.set_channel.has_settings = true;
    admin.set_channel.role = meshtastic_Channel_Role_SECONDARY;
    admin.set_channel.settings.channel_num = 1;
    admin.set_channel.settings.id = 0xAABBCCDD;
    admin.set_channel.settings.psk.size = 32;
    for (uint8_t index = 0; index < admin.set_channel.settings.psk.size; ++index)
    {
        admin.set_channel.settings.psk.bytes[index] = static_cast<uint8_t>(index + 1);
    }
    admin.set_channel.settings.uplink_enabled = true;
    admin.set_channel.settings.downlink_enabled = true;
    copyBounded(admin.set_channel.settings.name, sizeof(admin.set_channel.settings.name), "vic");

    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.id = packet_id;
    packet.to = 0x12345678;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded = meshtastic_Data_init_zero;
    packet.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    packet.decoded.dest = packet.to;
    packet.decoded.want_response = false;

    pb_ostream_t admin_stream = pb_ostream_from_buffer(packet.decoded.payload.bytes,
                                                       sizeof(packet.decoded.payload.bytes));
    if (!pb_encode(&admin_stream, meshtastic_AdminMessage_fields, &admin))
    {
        return false;
    }
    packet.decoded.payload.size = static_cast<pb_size_t>(admin_stream.bytes_written);

    meshtastic_ToRadio to_radio = meshtastic_ToRadio_init_zero;
    to_radio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    to_radio.packet = packet;

    pb_ostream_t to_radio_stream = pb_ostream_from_buffer(out, out_len);
    if (!pb_encode(&to_radio_stream, meshtastic_ToRadio_fields, &to_radio))
    {
        return false;
    }
    written = to_radio_stream.bytes_written;
    return true;
}

bool encodeWantConfigToRadio(uint8_t* out, size_t out_len, size_t& written, uint32_t nonce)
{
    meshtastic_ToRadio to_radio = meshtastic_ToRadio_init_zero;
    to_radio.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    to_radio.want_config_id = nonce;

    pb_ostream_t to_radio_stream = pb_ostream_from_buffer(out, out_len);
    if (!pb_encode(&to_radio_stream, meshtastic_ToRadio_fields, &to_radio))
    {
        return false;
    }
    written = to_radio_stream.bytes_written;
    return true;
}

bool decodeFromRadio(const phone::meshtastic::MeshtasticBleFrame& frame, meshtastic_FromRadio& out)
{
    out = meshtastic_FromRadio_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(frame.buf, frame.len);
    return pb_decode(&stream, meshtastic_FromRadio_fields, &out);
}

} // namespace

int main()
{
    phone::tests::FakePhoneRuntimeContext runtime;
    FakeMeshtasticTransport transport;

    phone::meshtastic::MeshtasticPhoneSession meshtastic_session(
        runtime, transport, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    const uint8_t invalid_meshtastic[] = {0xFF, 0xFF, 0xFF};
    assert(!meshtastic_session.handleToRadio(invalid_meshtastic, sizeof(invalid_meshtastic)));
    meshtastic_session.close();

    phone::tests::FakePhoneRuntimeContext admin_runtime;
    FakeMeshtasticTransport admin_transport;
    phone::meshtastic::MeshtasticPhoneSession admin_session(
        admin_runtime, admin_transport, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    constexpr uint32_t kSetChannelPacketId = 0xABCDEF12;
    uint8_t set_channel_to_radio[meshtastic_ToRadio_size] = {};
    size_t set_channel_to_radio_len = 0;
    assert(encodeAdminSetChannelToRadio(set_channel_to_radio,
                                        sizeof(set_channel_to_radio),
                                        set_channel_to_radio_len,
                                        kSetChannelPacketId));
    assert(admin_session.handleToRadio(set_channel_to_radio, set_channel_to_radio_len));
    assert(admin_runtime.save_config_count == 1);
    assert(admin_runtime.apply_mesh_config_count == 1);
    const auto saved_admin_config = admin_runtime.getMeshtasticPhoneConfig();
    assert(saved_admin_config.mesh.secondary_key_len == 32);
    assert(std::memcmp(saved_admin_config.mesh.secondary_key,
                       "\x01\x02\x03\x04\x05\x06\x07\x08"
                       "\x09\x0A\x0B\x0C\x0D\x0E\x0F\x10"
                       "\x11\x12\x13\x14\x15\x16\x17\x18"
                       "\x19\x1A\x1B\x1C\x1D\x1E\x1F\x20",
                       32) == 0);

    phone::meshtastic::MeshtasticBleFrame first_frame{};
    assert(admin_session.popToPhone(&first_frame));
    meshtastic_FromRadio first_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(first_frame, first_from));
    assert(first_frame.from_num == kSetChannelPacketId);
    assert(first_from.id == kSetChannelPacketId);
    assert(first_from.which_payload_variant == meshtastic_FromRadio_queueStatus_tag);
    assert(first_from.queueStatus.mesh_packet_id == kSetChannelPacketId);
    assert(first_from.queueStatus.res == 0);

    phone::meshtastic::MeshtasticBleFrame second_frame{};
    assert(admin_session.popToPhone(&second_frame));
    meshtastic_FromRadio second_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(second_frame, second_from));
    assert(second_from.id == second_frame.from_num);
    assert(second_from.which_payload_variant == meshtastic_FromRadio_packet_tag);
    assert(second_from.packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP);
    assert(second_from.packet.decoded.request_id == kSetChannelPacketId);
    meshtastic_AdminMessage admin_response = meshtastic_AdminMessage_init_zero;
    pb_istream_t admin_response_stream =
        pb_istream_from_buffer(second_from.packet.decoded.payload.bytes,
                               second_from.packet.decoded.payload.size);
    assert(pb_decode(&admin_response_stream, meshtastic_AdminMessage_fields, &admin_response));
    assert(admin_response.which_payload_variant == meshtastic_AdminMessage_get_channel_response_tag);
    assert(admin_response.get_channel_response.index == 1);
    assert(admin_response.get_channel_response.role == meshtastic_Channel_Role_SECONDARY);
    assert(std::strcmp(admin_response.get_channel_response.settings.name, "vic") == 0);
    assert(admin_response.get_channel_response.settings.psk.size == 32);
    assert(std::memcmp(admin_response.get_channel_response.settings.psk.bytes,
                       saved_admin_config.mesh.secondary_key,
                       32) == 0);
    assert(!admin_session.popToPhone(&second_frame));

    phone::tests::FakePhoneRuntimeContext config_runtime;
    FakeMeshtasticTransport config_transport;
    phone::meshtastic::MeshtasticPhoneSession config_session(
        config_runtime, config_transport, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    constexpr uint32_t kConfigNonce = 0x00010F2D;
    uint8_t want_config_to_radio[meshtastic_ToRadio_size] = {};
    size_t want_config_to_radio_len = 0;
    assert(encodeWantConfigToRadio(want_config_to_radio,
                                   sizeof(want_config_to_radio),
                                   want_config_to_radio_len,
                                   kConfigNonce));
    assert(config_session.handleToRadio(want_config_to_radio, want_config_to_radio_len));

    phone::meshtastic::MeshtasticBleFrame config_frame{};
    assert(config_session.popToPhone(&config_frame));
    meshtastic_FromRadio config_from = meshtastic_FromRadio_init_zero;
    assert(decodeFromRadio(config_frame, config_from));
    assert(config_frame.from_num == kConfigNonce);
    assert(config_from.id == 0);
    assert(config_from.which_payload_variant == meshtastic_FromRadio_my_info_tag);

    bool saw_config_complete = false;
    while (config_session.popToPhone(&config_frame))
    {
        config_from = meshtastic_FromRadio_init_zero;
        assert(decodeFromRadio(config_frame, config_from));
        if (config_from.which_payload_variant == meshtastic_FromRadio_config_complete_id_tag)
        {
            assert(config_frame.from_num == kConfigNonce);
            assert(config_from.id == 0);
            assert(config_from.config_complete_id == kConfigNonce);
            saw_config_complete = true;
            break;
        }
    }
    assert(saw_config_complete);

    phone::meshcore::MeshCorePhoneCore meshcore_core(runtime, "Trail Mate");
    assert(!meshcore_core.handleRxFrame(nullptr, 0));
    const uint8_t unknown_meshcore_cmd[] = {2, 0, 0, 0};
    assert(meshcore_core.handleRxFrame(unknown_meshcore_cmd, sizeof(unknown_meshcore_cmd)));

    uint8_t out[172] = {};
    size_t out_len = 0;
    assert(meshcore_core.popTxFrame(out, &out_len));
    assert(out_len == 2);
    assert(out[0] == kMeshCoreResponseError);
    assert(out[1] == kMeshCoreErrorIllegalArg);
    return 0;
}
