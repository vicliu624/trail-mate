#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace walkie::core
{
constexpr unsigned sample_rate = 8000, samples_per_frame = 160, bytes_per_frame = 8;
constexpr unsigned frames_per_packet = 5, header_size = 14;
constexpr unsigned payload_size = frames_per_packet * bytes_per_frame, packet_size = header_size + payload_size;
struct Header
{
    uint32_t sender = 0;
    uint16_t session = 0, sequence = 0, first_frame = 0;
};
struct Packet
{
    Header header;
    std::array<uint8_t, payload_size> payload{};
};
void write_header(uint8_t* bytes, const Header& header);
std::array<uint8_t, packet_size> encode_packet(const Packet& packet);
bool decode_packet(const uint8_t* bytes, std::size_t count, Packet& output);

struct Frame
{
    std::array<uint8_t, bytes_per_frame> bytes{};
    bool missing = false;
};
// Fixed 500 ms receive ring. Overflow drops the oldest audio. Gaps insert at
// most one packet of erasures; stale/duplicate frames are rejected modulo 16 bits.
class ReceiveBuffer
{
  public:
    static constexpr unsigned capacity = 25;
    enum class Push
    {
        Rejected,
        Accepted,
        NewSession
    };
    Push push(const Packet& packet, uint32_t self);
    bool pop(Frame& frame);
    void clear();
    unsigned size() const { return size_; }
    uint32_t dropped() const { return dropped_; }

  private:
    void append(const Frame& frame);
    std::array<Frame, capacity> frames_{};
    unsigned head_ = 0, size_ = 0;
    uint32_t sender_ = 0, dropped_ = 0;
    uint16_t session_ = 0, next_ = 0;
};
} // namespace walkie::core
