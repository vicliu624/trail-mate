#include "chat/infra/mqtt/mqtt311_codec.h"
#include <cstring>

namespace chat::mqtt311
{
bool validText(std::string_view text)
{
    if (text.size() > 65535) return false;
    for (size_t index = 0; index < text.size();)
    {
        const auto first = static_cast<uint8_t>(text[index++]);
        if (!first) return false;
        if (first < 0x80) continue;
        unsigned following = 0;
        uint32_t code = 0, minimum = 0;
        if (first >= 0xC2 && first <= 0xDF)
        {
            following = 1;
            code = first & 31;
            minimum = 0x80;
        }
        else if (first >= 0xE0 && first <= 0xEF)
        {
            following = 2;
            code = first & 15;
            minimum = 0x800;
        }
        else if (first >= 0xF0 && first <= 0xF4)
        {
            following = 3;
            code = first & 7;
            minimum = 0x10000;
        }
        else return false;
        if (following > text.size() - index) return false;
        while (following--)
        {
            const auto next = static_cast<uint8_t>(text[index++]);
            if ((next & 0xC0) != 0x80) return false;
            code = (code << 6) | (next & 63);
        }
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
    }
    return true;
}
bool validTopic(std::string_view text, bool filter)
{
    if (text.empty() || !validText(text)) return false;
    for (size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] != '+' && text[index] != '#') continue;
        if (!filter || (index && text[index - 1] != '/')) return false;
        if (text[index] == '#' && index + 1 != text.size()) return false;
        if (text[index] == '+' && index + 1 < text.size() && text[index + 1] != '/') return false;
    }
    return true;
}
namespace
{
class Writer
{
  public:
    explicit Writer(Packet& value) : packet(value)
    {
        packet.size = 0;
        packet.bytes.fill(0);
    }
    void byte(uint8_t value)
    {
        if (position >= packet.bytes.size())
        {
            good = false;
            return;
        }
        packet.bytes[position++] = value;
    }
    void number(uint16_t value)
    {
        byte(value >> 8);
        byte(value & 255);
    }
    void bytes(const uint8_t* data, size_t size)
    {
        if ((!data && size) || size > packet.bytes.size() - position)
        {
            good = false;
            return;
        }
        if (size) std::memcpy(packet.bytes.data() + position, data, size);
        position += size;
    }
    void string(std::string_view text)
    {
        if (text.size() > 65535)
        {
            good = false;
            return;
        }
        number(static_cast<uint16_t>(text.size()));
        bytes(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }
    bool finish(uint8_t header)
    {
        if (!good) return false;
        const size_t body_size = position - 5;
        size_t remaining = body_size, prefix = 1;
        packet.bytes[0] = header;
        do
        {
            uint8_t value = remaining % 128;
            remaining /= 128;
            packet.bytes[prefix++] = value | (remaining ? 128 : 0);
        } while (remaining);
        std::memmove(packet.bytes.data() + prefix, packet.bytes.data() + 5, body_size);
        packet.size = prefix + body_size;
        return true;
    }

  private:
    Packet& packet;
    size_t position = 5;
    bool good = true;
};
uint16_t number(const uint8_t* bytes) { return uint16_t((uint16_t(bytes[0]) << 8) | bytes[1]); }
bool fixedHeader(uint8_t byte)
{
    const auto type = byte >> 4, flags = byte & 15;
    if (type == 3)
    {
        const unsigned qos = (byte >> 1) & 3;
        return qos <= 1 && (qos || !(byte & 8));
    }
    return !flags && (type == 2 || type == 4 || type == 9 || type == 13 || type == 14);
}
} // namespace
bool connect(Packet& out, std::string_view id, std::string_view user, std::string_view password, uint16_t keepalive)
{
    out.size = 0;
    if (id.empty() || !validText(id) || !validText(user) || password.size() > 65535) return false;
    Writer writer(out);
    writer.string("MQTT");
    writer.byte(4);
    const bool has_password = !password.empty(), has_user = !user.empty() || has_password;
    writer.byte(2 | (has_password ? 64 : 0) | (has_user ? 128 : 0));
    writer.number(keepalive);
    writer.string(id);
    if (has_user) writer.string(user);
    if (has_password) writer.string(password); // MQTT password is binary, not a UTF-8 field.
    return writer.finish(0x10);
}
bool subscribe(Packet& out, uint16_t id, std::string_view filter)
{
    out.size = 0;
    if (!id || !validTopic(filter, true)) return false;
    Writer writer(out);
    writer.number(id);
    writer.string(filter);
    writer.byte(0);
    return writer.finish(0x82);
}
bool publish(Packet& out, std::string_view topic, const uint8_t* payload, size_t size, bool retained)
{
    out.size = 0;
    if (!validTopic(topic, false) || (!payload && size)) return false;
    Writer writer(out);
    writer.string(topic);
    writer.bytes(payload, size);
    return writer.finish(0x30 | (retained ? 1 : 0));
}
bool puback(Packet& out, uint16_t id)
{
    out.size = 0;
    if (!id) return false;
    Writer writer(out);
    writer.number(id);
    return writer.finish(0x40);
}
bool ping(Packet& out)
{
    Writer writer(out);
    return writer.finish(0xC0);
}
bool disconnect(Packet& out)
{
    Writer writer(out);
    return writer.finish(0xE0);
}

void Decoder::reset()
{
    body_.fill(0);
    stage_ = Stage::Header;
    remaining_ = used_ = header_ = digits_ = 0;
    multiplier_ = 1;
}
bool Decoder::partial() const { return stage_ == Stage::Length || stage_ == Stage::Body; }
Result Decoder::fail()
{
    stage_ = Stage::Failed;
    return Result::Error;
}
Result Decoder::push(uint8_t byte, View& view)
{
    view = {};
    if (stage_ == Stage::Failed) return Result::Error;
    if (stage_ == Stage::Delivered) reset();
    if (stage_ == Stage::Header)
    {
        if (!fixedHeader(byte)) return fail();
        header_ = byte;
        stage_ = Stage::Length;
        return Result::More;
    }
    if (stage_ == Stage::Length)
    {
        if (++digits_ > 4) return fail();
        remaining_ += (byte & 127) * multiplier_;
        if (remaining_ > body_.size()) return fail();
        if (byte & 128)
        {
            if (digits_ == 4) return fail();
            multiplier_ *= 128;
            return Result::More;
        }
        if (digits_ > 1 && !(byte & 127)) return fail();
        stage_ = Stage::Body;
        return remaining_ ? Result::More : complete(view);
    }
    body_[used_++] = byte;
    return used_ == remaining_ ? complete(view) : Result::More;
}
Result Decoder::complete(View& view)
{
    view.type = static_cast<Type>(header_ >> 4);
    if (view.type == Type::Connack)
    {
        if (remaining_ != 2 || (body_[0] & ~1) || body_[1] > 5 || (body_[1] && body_[0])) return fail();
        view.session_present = body_[0] & 1;
        view.return_code = body_[1];
    }
    else if (view.type == Type::Puback || view.type == Type::Suback)
    {
        if ((view.type == Type::Puback && remaining_ != 2) || (view.type == Type::Suback && remaining_ < 3)) return fail();
        view.packet_id = number(body_.data());
        if (!view.packet_id) return fail();
        if (view.type == Type::Suback)
        {
            for (size_t index = 2; index < remaining_; ++index)
                if (body_[index] > 2 && body_[index] != 128) return fail();
            view.payload = body_.data() + 2;
            view.size = remaining_ - 2;
        }
    }
    else if (view.type == Type::Publish)
    {
        if (remaining_ < 3) return fail();
        const size_t topic_size = number(body_.data());
        if (!topic_size || topic_size > remaining_ - 2) return fail();
        view.topic = {reinterpret_cast<const char*>(body_.data() + 2), topic_size};
        if (!validTopic(view.topic, false)) return fail();
        size_t offset = 2 + topic_size;
        view.qos = (header_ >> 1) & 3;
        view.duplicate = header_ & 8;
        view.retained = header_ & 1;
        if (view.qos)
        {
            if (remaining_ - offset < 2 || !(view.packet_id = number(body_.data() + offset))) return fail();
            offset += 2;
        }
        view.payload = body_.data() + offset;
        view.size = remaining_ - offset;
    }
    else if (remaining_) return fail();
    stage_ = Stage::Delivered;
    return Result::Packet;
}
} // namespace chat::mqtt311
