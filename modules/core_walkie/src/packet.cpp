#include "walkie/packet.h"
#include <algorithm>

namespace walkie::core
{
namespace
{
void put16(uint8_t* output, uint16_t value)
{
    output[0] = value;
    output[1] = value >> 8;
}
uint16_t get16(const uint8_t* input) { return uint16_t(input[0]) | uint16_t(input[1]) << 8; }
} // namespace
void write_header(uint8_t* bytes, const Header& header)
{
    bytes[0] = 'W';
    bytes[1] = 'T';
    bytes[2] = 2;
    bytes[3] = 1;
    for (unsigned index = 0; index < 4; ++index) bytes[4 + index] = header.sender >> (index * 8);
    put16(bytes + 8, header.session);
    put16(bytes + 10, header.sequence);
    put16(bytes + 12, header.first_frame);
}
std::array<uint8_t, packet_size> encode_packet(const Packet& packet)
{
    std::array<uint8_t, packet_size> bytes{};
    write_header(bytes.data(), packet.header);
    std::copy(packet.payload.begin(), packet.payload.end(), bytes.begin() + header_size);
    return bytes;
}
bool decode_packet(const uint8_t* bytes, std::size_t count, Packet& output)
{
    if (!bytes || count != packet_size || bytes[0] != 'W' || bytes[1] != 'T' || bytes[2] != 2 || bytes[3] != 1) return false;
    Packet packet;
    for (unsigned index = 0; index < 4; ++index) packet.header.sender |= uint32_t(bytes[4 + index]) << (index * 8);
    if (!packet.header.sender) return false;
    packet.header.session = get16(bytes + 8);
    packet.header.sequence = get16(bytes + 10);
    packet.header.first_frame = get16(bytes + 12);
    std::copy_n(bytes + header_size, payload_size, packet.payload.begin());
    output = packet;
    return true;
}
void ReceiveBuffer::clear()
{
    head_ = size_ = 0;
    sender_ = 0;
}
void ReceiveBuffer::append(const Frame& frame)
{
    if (size_ == capacity)
    {
        head_ = (head_ + 1) % capacity;
        --size_;
        ++dropped_;
    }
    frames_[(head_ + size_) % capacity] = frame;
    ++size_;
}
ReceiveBuffer::Push ReceiveBuffer::push(const Packet& packet, uint32_t self)
{
    if (!packet.header.sender || packet.header.sender == self) return Push::Rejected;
    const bool changed = sender_ != packet.header.sender || session_ != packet.header.session;
    if (changed)
    {
        clear();
        sender_ = packet.header.sender;
        session_ = packet.header.session;
    }
    else
    {
        const auto gap = uint16_t(packet.header.first_frame - next_);
        if (gap >= 0x8000) return Push::Rejected;
        if (gap > capacity)
        {
            head_ = size_ = 0;
            dropped_ += gap;
        }
        else
            for (unsigned count = 0; count < std::min<unsigned>(gap, frames_per_packet); ++count) append({{}, true});
    }
    for (unsigned index = 0; index < frames_per_packet; ++index)
    {
        Frame frame;
        std::copy_n(packet.payload.begin() + index * bytes_per_frame, bytes_per_frame, frame.bytes.begin());
        append(frame);
    }
    next_ = uint16_t(packet.header.first_frame + frames_per_packet);
    return changed ? Push::NewSession : Push::Accepted;
}
bool ReceiveBuffer::pop(Frame& frame)
{
    if (!size_) return false;
    frame = frames_[head_];
    head_ = (head_ + 1) % capacity;
    --size_;
    return true;
}
} // namespace walkie::core
