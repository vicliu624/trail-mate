#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace chat::mqtt311
{
inline constexpr size_t kMaxBodySize = 1024;
struct Packet
{
    std::array<uint8_t, kMaxBodySize + 5> bytes{};
    size_t size = 0;
};

// Caller-owned storage; embedded callers must keep Packet/Decoder off task stacks.
bool connect(Packet&, std::string_view client_id, std::string_view username,
             std::string_view password, uint16_t keepalive_seconds = 60);
bool subscribe(Packet&, uint16_t packet_id, std::string_view filter);
bool publish(Packet&, std::string_view topic, const uint8_t* payload, size_t size, bool retained = false);
bool puback(Packet&, uint16_t packet_id);
bool ping(Packet&);
bool disconnect(Packet&);
bool validText(std::string_view);
bool validTopic(std::string_view, bool filter);

enum class Type : uint8_t
{
    Connack = 2,
    Publish = 3,
    Puback = 4,
    Suback = 9,
    Pingresp = 13,
    Disconnect = 14
};
struct View
{
    Type type = Type::Disconnect;
    uint16_t packet_id = 0;
    uint8_t qos = 0, return_code = 0;
    bool retained = false, duplicate = false, session_present = false;
    std::string_view topic;
    const uint8_t* payload = nullptr;
    size_t size = 0;
};
enum class Result
{
    More,
    Packet,
    Error
};

// Client-side broker frames. Views remain valid only until the next push/reset.
// Errors latch until reset; byte-at-a-time input handles arbitrary TCP boundaries.
class Decoder
{
  public:
    Result push(uint8_t byte, View&);
    void reset();
    bool partial() const;

  private:
    enum class Stage
    {
        Header,
        Length,
        Body,
        Delivered,
        Failed
    };
    Result complete(View&);
    Result fail();
    Stage stage_ = Stage::Header;
    std::array<uint8_t, kMaxBodySize> body_{};
    uint32_t remaining_ = 0, multiplier_ = 1;
    size_t used_ = 0;
    uint8_t header_ = 0, digits_ = 0;
};
} // namespace chat::mqtt311
