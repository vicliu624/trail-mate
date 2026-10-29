#include "walkie/packet.h"
#include <algorithm>
#include <stdexcept>
using namespace walkie::core;
void require(bool value)
{
    if (!value) throw std::runtime_error("walkie wire/ring contract failed");
}
int main()
{
    Packet packet;
    packet.header = {0x12345678, 0xABCD, 0x0102, 0xFFFC};
    for (unsigned index = 0; index < payload_size; ++index) packet.payload[index] = index;
    auto encoded = encode_packet(packet);
    constexpr uint8_t golden[] = {'W', 'T', 2, 1, 0x78, 0x56, 0x34, 0x12, 0xCD, 0xAB, 2, 1, 0xFC, 0xFF};
    require(std::equal(std::begin(golden), std::end(golden), encoded.begin()));
    Packet parsed;
    require(decode_packet(encoded.data(), encoded.size(), parsed));
    require(parsed.payload == packet.payload && parsed.header.first_frame == 0xFFFC);
    require(!decode_packet(encoded.data(), encoded.size() - 1, parsed));
    encoded[3] = 2;
    require(!decode_packet(encoded.data(), encoded.size(), parsed));
    ReceiveBuffer buffer;
    require(buffer.push(packet, 0x12345678) == ReceiveBuffer::Push::Rejected);
    require(buffer.push(packet, 9) == ReceiveBuffer::Push::NewSession);
    require(buffer.push(packet, 9) == ReceiveBuffer::Push::Rejected);
    packet.header.first_frame = 2; // wrap plus one missing frame
    require(buffer.push(packet, 9) == ReceiveBuffer::Push::Accepted && buffer.size() == 11);
    Frame frame;
    for (unsigned index = 0; index < 5; ++index) require(buffer.pop(frame) && !frame.missing);
    require(buffer.pop(frame) && frame.missing);
    for (unsigned index = 0; index < 20; ++index)
    {
        packet.header.first_frame = uint16_t(7 + index * 5);
        buffer.push(packet, 9);
    }
    require(buffer.size() == ReceiveBuffer::capacity && buffer.dropped() > 0);
    packet.header.session++;
    require(buffer.push(packet, 9) == ReceiveBuffer::Push::NewSession && buffer.size() == 5);
    buffer.clear();
    require(buffer.size() == 0 && !buffer.pop(frame));
}
