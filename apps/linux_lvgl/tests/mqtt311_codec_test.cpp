#include "chat/infra/mqtt/mqtt311_codec.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
namespace mqtt = chat::mqtt311;
using Bytes = std::vector<uint8_t>;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
Bytes bytes(const mqtt::Packet& packet) { return {packet.bytes.begin(), packet.bytes.begin() + packet.size}; }
mqtt::View decode(mqtt::Decoder& decoder, const Bytes& input)
{
    mqtt::View view;
    for (size_t index = 0; index < input.size(); ++index)
    {
        const auto result = decoder.push(input[index], view);
        require(result == (index + 1 == input.size() ? mqtt::Result::Packet : mqtt::Result::More), "unexpected packet boundary");
    }
    return view;
}
void rejected(const Bytes& input)
{
    mqtt::Decoder decoder;
    mqtt::View view;
    auto result = mqtt::Result::More;
    for (const auto byte : input) result = decoder.push(byte, view);
    require(result == mqtt::Result::Error, "malformed broker packet accepted");
    require(decoder.push(0xD0, view) == mqtt::Result::Error, "parser error did not latch");
    decoder.reset();
    require(decode(decoder, {0xD0, 0}).type == mqtt::Type::Pingresp, "parser did not reset");
}
} // namespace
int main()
{
    mqtt::Packet packet;
    require(mqtt::connect(packet, "tm", "u", "p", 60), "CONNECT encoding failed");
    require(bytes(packet) == Bytes({0x10, 20, 0, 4, 'M', 'Q', 'T', 'T', 4, 0xC2, 0, 60, 0, 2, 't', 'm', 0, 1, 'u', 0, 1, 'p'}), "CONNECT differs from golden bytes");
    require(mqtt::connect(packet, "tm", "", std::string_view("\0p", 2)), "binary MQTT password rejected");
    require(packet.bytes[9] == 0xC2 && packet.bytes[packet.size - 2] == 0 && packet.bytes[packet.size - 1] == 'p', "password flags or binary bytes changed");
    require(mqtt::subscribe(packet, 7, "a/#") && bytes(packet) == Bytes({0x82, 8, 0, 7, 0, 3, 'a', '/', '#', 0}), "SUBSCRIBE differs from golden bytes");
    const uint8_t payload[]{1, 0, 2};
    require(mqtt::publish(packet, "a", payload, sizeof(payload)) && bytes(packet) == Bytes({0x30, 6, 0, 1, 'a', 1, 0, 2}), "PUBLISH differs from golden bytes");
    require(mqtt::puback(packet, 0x1234) && bytes(packet) == Bytes({0x40, 2, 0x12, 0x34}), "PUBACK byte order wrong");
    require(mqtt::ping(packet) && bytes(packet) == Bytes({0xC0, 0}), "PINGREQ encoding wrong");
    require(mqtt::disconnect(packet) && bytes(packet) == Bytes({0xE0, 0}), "DISCONNECT encoding wrong");
    require(!mqtt::connect(packet, "", "", "") && !packet.size, "empty client ID accepted");
    require(!mqtt::subscribe(packet, 0, "a/#") && !packet.size, "zero packet ID accepted");
    require(!mqtt::puback(packet, 0) && !packet.size, "zero PUBACK accepted");
    require(mqtt::validText("轨迹😀") && mqtt::validText("\xEF\xBB\xBF"), "valid UTF-8 or BOM rejected");
    for (const auto& invalid : {std::string("\0", 1), std::string("\xC0\x80", 2), std::string("\xED\xA0\x80", 3), std::string("\xF4\x90\x80\x80", 4), std::string("\xE4\xB8", 2)})
        require(!mqtt::validText(invalid), "invalid MQTT UTF-8 accepted");
    require(!mqtt::validText(std::string(65536, 'a')), "oversized UTF-8 field accepted");
    for (auto filter : {"#", "+", "a/+/b/#", "/", "a//b"}) require(mqtt::validTopic(filter, true), "valid filter rejected");
    for (auto filter : {"", "a+", "a/#/b", "a/b#", "/#/"}) require(!mqtt::validTopic(filter, true), "invalid filter accepted");
    require(!mqtt::publish(packet, "a/+", payload, 3) && !packet.size, "PUBLISH wildcard accepted");
    mqtt::Decoder decoder;
    auto view = decode(decoder, {0x20, 2, 0, 0});
    require(view.type == mqtt::Type::Connack && !view.session_present && !view.return_code, "CONNACK projection wrong");
    view = decode(decoder, {0x20, 2, 0, 5});
    require(view.return_code == 5, "CONNACK refusal lost");
    view = decode(decoder, {0x90, 3, 0, 7, 0x80});
    require(view.packet_id == 7 && view.size == 1 && view.payload[0] == 128, "SUBACK refusal lost");
    view = decode(decoder, {0x3B, 8, 0, 1, 'a', 0, 17, 1, 0, 2});
    require(view.type == mqtt::Type::Publish && view.topic == "a" && view.qos == 1 && view.packet_id == 17 && view.duplicate && view.retained && view.size == 3 && view.payload[1] == 0, "QoS1 PUBLISH projection wrong");
    view = decode(decoder, {0x30, 3, 0, 1, 'a'});
    require(!view.size && !view.qos, "empty binary application payload rejected");
    unsigned packets = 0;
    const Bytes coalesced{0x20, 2, 0, 0, 0x40, 2, 0, 17, 0xD0, 0};
    for (const auto byte : coalesced)
        if (decoder.push(byte, view) == mqtt::Result::Packet) ++packets;
    require(packets == 3 && !decoder.partial(), "coalesced TCP packets lost");
    const Bytes maximum(mqtt::kMaxBodySize - 3, 0x80);
    require(mqtt::publish(packet, "a", maximum.data(), maximum.size()), "maximum packet rejected");
    view = decode(decoder, bytes(packet));
    require(view.size == maximum.size() && std::equal(maximum.begin(), maximum.end(), view.payload), "multi-byte remaining length corrupted");
    require(!mqtt::publish(packet, "a", maximum.data(), maximum.size() + 1) && !packet.size, "packet capacity exceeded");
    for (const Bytes& invalid : {Bytes{0x21}, Bytes{0x34}, Bytes{0x36}, Bytes{0x38}, Bytes{0x20, 2, 2, 0}, Bytes{0x20, 2, 1, 5}, Bytes{0x20, 2, 0, 6},
                                 Bytes{0x90, 3, 0, 0, 0}, Bytes{0x90, 3, 0, 1, 3}, Bytes{0x40, 2, 0, 0}, Bytes{0x30, 0}, Bytes{0x30, 3, 0, 0, 'a'},
                                 Bytes{0x30, 3, 0, 1, 0}, Bytes{0x30, 3, 0, 9, 'a'}, Bytes{0xD0, 1, 0}, Bytes{0x30, 0x80, 0}, Bytes{0x30, 0x81, 8}})
        rejected(invalid);
}
