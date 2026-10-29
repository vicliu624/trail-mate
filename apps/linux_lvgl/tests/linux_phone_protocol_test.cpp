#include "app/linux_app_services.h"
#include "app/linux_phone_facade.h"
#include "app/linux_phone_protocol.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "meshtastic/admin.pb.h"
#include "meshtastic/mesh.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "platform/linux/application_lifecycle.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <sys/wait.h>
#include <unistd.h>

using namespace trailmate::linux_app;
using Channel = platform::linux_runtime::PhoneGattChannel;
using Protocol = platform::linux_runtime::PhoneGattProtocol;
namespace
{
bool allow_clock_write = false;
unsigned clock_writes = 0;
time_t written_epoch = 0;
} // namespace
extern "C" int clock_settime(clockid_t clock, const struct timespec* value) noexcept
{
    assert(clock == CLOCK_REALTIME && value && value->tv_nsec == 0);
    ++clock_writes;
    written_epoch = value->tv_sec;
    errno = allow_clock_write ? 0 : EPERM;
    return allow_clock_write ? 0 : -1;
}
namespace
{
void restartBackend()
{
    const auto child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        const int result = platform::linux_runtime::run_application(nullptr, []
                                                                    {
            LinuxAppServices services;
            LinuxPhoneFacade facade(services);
            facade.restartDevice();
            assert(platform::linux_runtime::application_restart_requested());
            platform::linux_runtime::request_application_exit();
            return 42; });
        _exit(result);
    }
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 42);
}
std::vector<uint8_t> encode(const pb_msgdesc_t* type, const void* message, size_t maximum)
{
    std::vector<uint8_t> bytes(maximum);
    auto stream = pb_ostream_from_buffer(bytes.data(), bytes.size());
    assert(pb_encode(&stream, type, message));
    bytes.resize(stream.bytes_written);
    return bytes;
}
std::vector<uint8_t> toRadio(const meshtastic_ToRadio& message)
{
    return encode(meshtastic_ToRadio_fields, &message, meshtastic_ToRadio_size);
}
meshtastic_FromRadio decode(const std::vector<uint8_t>& bytes)
{
    meshtastic_FromRadio message = meshtastic_FromRadio_init_zero;
    auto stream = pb_istream_from_buffer(bytes.data(), bytes.size());
    assert(pb_decode(&stream, meshtastic_FromRadio_fields, &message));
    return message;
}
} // namespace
int main()
{
    restartBackend();
    char temporary[] = "/tmp/trailmate-phone-XXXXXX";
    assert(mkdtemp(temporary));
    setenv("TRAIL_MATE_SETTINGS_ROOT", temporary, 1);
    setenv("TRAIL_MATE_CACHE_ROOT", temporary, 1);
    setenv("TRAIL_MATE_SD_ROOT", temporary, 1);
    const auto absent_radio = std::filesystem::path(temporary) / "no-radio";
    setenv("TRAIL_MATE_LORA_SPI", absent_radio.c_str(), 1);
    setenv("TRAIL_MATE_DEMO_WORLD", "0", 1);
    LinuxAppServicesOptions options;
    options.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceLocal;
    {
        LinuxAppServices services(options);
        assert(services.initialize());
        assert(services.switchMeshProtocol(chat::MeshProtocol::Meshtastic, false));
        LinuxPhoneFacade facade(services);
        auto snapshot = facade.getMeshtasticPhoneConfig();
        std::strcpy(snapshot.node_name, "Phone configuration owner");
        std::strcpy(snapshot.short_name, "PH");
        snapshot.mesh.tx_power = 7;
        facade.setMeshtasticPhoneConfig(snapshot);
        facade.applyUserInfo();
        assert(std::string(services.config().node_name) == "Phone configuration owner");
        assert(services.config().meshtastic_config.tx_power == 7);
        const auto before_nodes = facade.phoneNodeCount();
        chat::contacts::NodeUpdate node{};
        node.has_protocol = true;
        node.protocol = static_cast<uint8_t>(chat::contacts::NodeProtocolType::Meshtastic);
        node.long_name = "Meshtastic peer";
        services.contacts().applyNodeUpdate(0x10203040, node);
        node.protocol = static_cast<uint8_t>(chat::contacts::NodeProtocolType::MeshCore);
        node.long_name = "Other protocol peer";
        services.contacts().applyNodeUpdate(0x10203041, node);
        phone::PhoneNodeView view{};
        assert(facade.phoneNodeCount() == before_nodes + 1);
        assert(facade.findPhoneNode(0x10203040, view) && view.node_id == 0x10203040);
        assert(!facade.findPhoneNode(0x10203041, view));
        uint8_t mac[6];
        assert(!facade.getDeviceMacAddress(mac));
        facade.setDeviceMac({1, 2, 3, 4, 5, 6});
        assert(facade.getDeviceMacAddress(mac) && mac[0] == 1 && mac[5] == 6);
        chat::MeshIncomingData data{};
        for (unsigned i = 0; i < 16; ++i) assert(facade.enqueueIncomingData(data));
        assert(!facade.enqueueIncomingData(data));
        for (unsigned i = 0; i < 16; ++i) assert(facade.pollIncomingPhoneData(data));
        assert(!facade.pollIncomingPhoneData(data));
        // No synthetic MeshCore identity or success when the backend is absent.
        uint8_t key[32]{};
        assert(!facade.meshCoreExportIdentityPublicKey(key, sizeof(key)));
        LinuxPhoneProtocol protocol(services);
        std::vector<uint8_t> output;
        assert(!protocol.read(Channel::FromRadio, output));
        protocol.subscribe(Channel::FromNum, true);
        protocol.connect(Protocol::Meshtastic, {1, 2, 3, 4, 5, 6});
        meshtastic_ToRadio request = meshtastic_ToRadio_init_zero;
        request.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
        request.want_config_id = 0x78563412;
        assert(protocol.write(Channel::ToRadio, toRadio(request)));
        assert(!protocol.write(Channel::MeshCoreRx, {1}));
        LinuxPhoneProtocol::Notification notification, retry;
        assert(protocol.nextNotification(notification) && notification.channel == Channel::FromNum);
        assert(protocol.nextNotification(retry) && retry.token == notification.token && retry.bytes == notification.bytes);
        protocol.commitNotification(notification.token);
        assert(!protocol.nextNotification(retry));
        bool complete = false, self_seen = false;
        for (unsigned count = 0; count < 200 && !complete; ++count)
        {
            protocol.pump();
            assert(protocol.read(Channel::FromRadio, output) && !output.empty());
            const auto message = decode(output);
            if (message.which_payload_variant == meshtastic_FromRadio_my_info_tag)
            {
                assert(message.my_info.my_node_num == services.selfNodeId());
                self_seen = true;
            }
            if (message.which_payload_variant == meshtastic_FromRadio_config_complete_id_tag)
            {
                assert(message.config_complete_id == request.want_config_id);
                complete = true;
            }
        }
        assert(self_seen && complete);
        meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
        admin.which_payload_variant = meshtastic_AdminMessage_set_owner_tag;
        std::strcpy(admin.set_owner.long_name, "Configured by shared protocol");
        std::strcpy(admin.set_owner.short_name, "BLE");
        const auto admin_bytes = encode(meshtastic_AdminMessage_fields, &admin, meshtastic_AdminMessage_size);
        request = meshtastic_ToRadio_init_zero;
        request.which_payload_variant = meshtastic_ToRadio_packet_tag;
        request.packet.id = 918;
        request.packet.to = services.selfNodeId();
        request.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        request.packet.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
        request.packet.decoded.payload.size = admin_bytes.size();
        std::copy(admin_bytes.begin(), admin_bytes.end(), request.packet.decoded.payload.bytes);
        assert(protocol.write(Channel::ToRadio, toRadio(request)));
        assert(std::string(services.config().node_name) == "Configured by shared protocol");
        assert(std::string(services.config().short_name) == "BLE");
        protocol.subscribe(Channel::FromRadioSync, true);
        // Do not consume a queued frame until transport acknowledges it.
        while (protocol.nextNotification(notification) && notification.channel == Channel::FromNum)
            protocol.commitNotification(notification.token);
        assert(notification.token && notification.channel == Channel::FromRadioSync);
        assert(protocol.nextNotification(retry) && retry.token == notification.token && retry.bytes == notification.bytes);
        // A normal read can recover the same frame after an MTU-limited notify fails.
        assert(protocol.read(Channel::FromRadio, output) && output == notification.bytes);
        protocol.commitNotification(notification.token); // Stale acknowledgement must be harmless.
        admin = meshtastic_AdminMessage_init_zero;
        admin.which_payload_variant = meshtastic_AdminMessage_set_time_only_tag;
        admin.set_time_only = 1700000000;
        const auto time_bytes = encode(meshtastic_AdminMessage_fields, &admin, meshtastic_AdminMessage_size);
        request.packet.id++;
        request.packet.decoded.payload.size = time_bytes.size();
        std::copy(time_bytes.begin(), time_bytes.end(), request.packet.decoded.payload.bytes);
        assert(!protocol.write(Channel::ToRadio, toRadio(request)));
        assert(clock_writes == 1 && written_epoch == 1700000000);
        allow_clock_write = true;
        request.packet.id++;
        assert(protocol.write(Channel::ToRadio, toRadio(request)));
        assert(clock_writes == 2 && written_epoch == 1700000000);
        protocol.disconnect();
        assert(!protocol.read(Channel::FromRadio, output));
        assert(!protocol.write(Channel::ToRadio, toRadio(request)));
        protocol.connect(Protocol::Meshtastic, {});
        assert(protocol.read(Channel::FromRadio, output) && output.empty());
        assert(!protocol.nextNotification(notification));
        request = meshtastic_ToRadio_init_zero;
        request.which_payload_variant = meshtastic_ToRadio_disconnect_tag;
        request.disconnect = true;
        assert(protocol.write(Channel::ToRadio, toRadio(request)));
        assert(protocol.takeDisconnectRequest() && !protocol.takeDisconnectRequest());
        protocol.disconnect();
        services.shutdown();
    }
    std::filesystem::remove_all(temporary);
}
