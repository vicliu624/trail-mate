#include "chat/infra/reticulum/path_request_wire.h"
#include <array>
#include <cassert>
#include <cstring>
namespace rt = chat::reticulum;
int main()
{
    std::array<uint8_t, 16> destination{}, tag{};
    for (unsigned i = 0; i < 16; ++i)
    {
        destination[i] = i;
        tag[i] = i + 16;
    }
    const uint8_t control[] = {0x6b, 0x9f, 0x66, 0x01, 0x4d, 0x98, 0x53, 0xfa, 0xab, 0x22, 0x0f, 0xba, 0x47, 0xd0, 0x27, 0x61};
    std::array<uint8_t, rt::kPathRequestPacketSize> bytes{};
    size_t size = bytes.size();
    assert(rt::buildPathRequest(destination.data(), tag.data(), bytes.data(), &size));
    assert(size == 51 && bytes[0] == 0x08 && bytes[1] == 0 && !std::memcmp(bytes.data() + 2, control, 16) && bytes[18] == 0);
    assert(!std::memcmp(bytes.data() + 19, destination.data(), 16) && !std::memcmp(bytes.data() + 35, tag.data(), 16));
    rt::ParsedPacket packet;
    assert(rt::parsePacket(bytes.data(), size, &packet) && packet.packet_type == rt::PacketType::Data && packet.destination_type == rt::DestinationType::Plain);
    size = bytes.size() - 1;
    assert(!rt::buildPathRequest(destination.data(), tag.data(), bytes.data(), &size) && !size);
    size = bytes.size();
    assert(!rt::buildPathRequest(nullptr, tag.data(), bytes.data(), &size) && !size);
    size = bytes.size();
    assert(!rt::buildPathRequest(destination.data(), tag.data(), nullptr, &size) && !size);
    tag.fill(0);
    size = bytes.size();
    assert(!rt::buildPathRequest(destination.data(), tag.data(), bytes.data(), &size) && !size);
    assert(!rt::buildPathRequest(destination.data(), tag.data(), bytes.data(), nullptr));
}
