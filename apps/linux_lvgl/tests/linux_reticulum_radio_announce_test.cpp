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
        constexpr uint32_t interval = chat::lxmf::kDefaultAnnounceIntervalMs;
        announce.tick(43000 + 600000, 1794130604, send, &sent);
        assert(sent.count == 4);
        // Unchanged settings cannot restart the initial-announcement timer.
        announce.configure(true, "Retry", 650000);
        announce.tick(655000, 1794130616, send, &sent);
        assert(sent.count == 4);
        announce.tick(43000 + interval - 1, 1794144403, send, &sent);
        assert(sent.count == 4);
        announce.tick(43000 + interval, 1794144404, send, &sent);
        assert(sent.count == 5);

        const auto label = [&]()
        {
            assert(chat::reticulum::parsePacket(sent.packet.data(), sent.packet.size(), &packet));
            assert(packet.payload_len >= 148);
            return std::string(reinterpret_cast<const char*>(packet.payload + 148), packet.payload_len - 148);
        };
        assert(label() == "Retry");
        announce.configure(true, "", 43000 + interval);
        announce.tick(48000 + interval, 1794144409, send, &sent);
        assert(sent.count == 6 && label() == "Anonymous Peer");
        // Saved text is preserved even when it matches the old factory name.
        announce.configure(true, "Cardputer Zero", 48000 + interval);
        announce.tick(53000 + interval, 1794144414, send, &sent);
        assert(sent.count == 7 && label() == "Cardputer Zero");

        // Re-enabling is an actual new session; the four-hour period still
        // works when the 32-bit millisecond counter wraps.
        constexpr uint32_t start = UINT32_MAX - 5000U;
        announce.configure(false, "Wrap", start - 1U);
        announce.configure(true, "Wrap", start);
        announce.tick(start + 5000U, 1794144500, send, &sent);
        assert(sent.count == 8);
        announce.tick(start + 5000U + interval - 1U, 1794158899, send, &sent);
        assert(sent.count == 8);
        announce.tick(start + 5000U + interval, 1794158900, send, &sent);
        assert(sent.count == 9);
    }
    std::filesystem::remove_all(path);
}
