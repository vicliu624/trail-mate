#include "chat/infra/reticulum/path_request_wire.h"
#include "chat/linux_reticulum_radio_announce.h"
#include <cassert>
#include <filesystem>
#include <unistd.h>
#include <vector>

int main()
{
    auto path = std::filesystem::temp_directory_path() / ("trailmate-radio-announce-" + std::to_string(getpid()));
    std::filesystem::create_directory(path);
    {
        platform::linux_runtime::LinuxReticulumIdentity identity(path / "identity.sqlite3");
        assert(identity.begin());
        trailmate::linux_app::LinuxReticulumRadioAnnounce announce(identity);
        struct Sent
        {
            int count = 0;
            bool ok = true;
            std::vector<uint8_t> packet;
        } sent;
        auto send = [](void* context, const uint8_t* bytes, size_t size)
        {
            auto& sent = *static_cast<Sent*>(context);
            ++sent.count;
            sent.packet.assign(bytes, bytes + size);
            return sent.ok;
        };
        announce.configure(true, "Radio only", 100);
        announce.tick(5099, 1794130000, send, &sent);
        assert(sent.count == 0);
        announce.tick(5100, 1794130000, send, &sent);
        assert(sent.count == 1);
        chat::reticulum::ParsedPacket packet;
        assert(chat::reticulum::parsePacket(sent.packet.data(), sent.packet.size(), &packet));
        assert(packet.packet_type == chat::reticulum::PacketType::Announce);
        assert(std::memcmp(packet.destination_hash, identity.destinationHash().data(), 16) == 0);
        announce.tick(5101, 1794130000, send, &sent);
        assert(sent.count == 1);
        uint8_t request[chat::reticulum::kPathRequestPacketSize], tag[16] = {1};
        size_t size = sizeof(request);
        assert(chat::reticulum::buildPathRequest(identity.destinationHash().data(), tag, request, &size));
        assert(announce.receive(request, size, 5200, 1794130001, send, &sent));
        assert(sent.count == 2);
        assert(chat::reticulum::parsePacket(sent.packet.data(), sent.packet.size(), &packet));
        assert(packet.context == uint8_t(chat::reticulum::PacketContext::PathResponse));
        announce.receive(request, size, 5201, 1794130001, send, &sent);
        assert(sent.count == 2);
        announce.configure(false, "Radio only", 6000);
        announce.tick(700000, 1794130002, send, &sent);
        assert(!announce.receive(request, size, 700000, 1794130002, send, &sent));
        assert(sent.count == 2);
        announce.configure(true, "Retry", 8000);
        sent.ok = false;
        announce.tick(13000, 1794130003, send, &sent);
        assert(sent.count == 3);
        announce.tick(13001, 1794130003, send, &sent);
        assert(sent.count == 3);
        sent.ok = true;
        announce.tick(43000, 1794130004, send, &sent);
        assert(sent.count == 4);
    }
    std::filesystem::remove_all(path);
}
