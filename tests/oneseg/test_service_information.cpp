#include "platform/linux/oneseg_service_information.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
using platform::linux_runtime::OneSegServiceInformation;
using Bytes = std::vector<uint8_t>;
static uint32_t crc(const Bytes& data)
{
    uint32_t value = 0xffffffff;
    for (auto byte : data)
    {
        value ^= uint32_t(byte) << 24;
        for (int i = 0; i < 8; ++i) value = (value << 1) ^ ((value & 0x80000000) ? 0x04c11db7 : 0);
    }
    return value;
}
static Bytes section(Bytes bytes)
{
    const auto length = bytes.size() + 1;
    bytes[1] = 0xf0 | (length >> 8);
    bytes[2] = length;
    const auto value = crc(bytes);
    for (int shift : {24, 16, 8, 0}) bytes.push_back(value >> shift);
    assert(crc(bytes) == 0);
    return bytes;
}
static void feed(OneSegServiceInformation& parser, unsigned pid, const Bytes& data, unsigned& cc, uint64_t now_ms = 0)
{
    size_t offset = 0;
    while (offset < data.size())
    {
        std::array<uint8_t, 188> packet;
        packet.fill(0xff);
        packet[0] = 0x47;
        packet[1] = (pid >> 8) | (offset ? 0 : 0x40);
        packet[2] = pid;
        packet[3] = 0x10 | (cc++ & 15);
        size_t pos = 4;
        if (!offset) packet[pos++] = 0;
        const auto count = std::min(data.size() - offset, packet.size() - pos);
        std::copy_n(data.data() + offset, count, packet.data() + pos);
        assert(parser.accept(packet.data(), packet.size(), now_ms));
        offset += count;
    }
}
static Bytes sdt(uint8_t flags = 0xc1)
{
    // TEST FIXTURE - NO RF. Explicit ARIB locking shift to alphanumeric.
    return section({0x42, 0, 0, 0, 1, flags, 0, 0, 0, 2, 0xff, 0, 8, 0xff, 0xf0, 10,
                    0x48, 8, 1, 0, 5, 0x0e, 'T', 'E', 'S', 'T'});
}
static Bytes eit(unsigned part, bool empty = false, unsigned service = 8)
{
    Bytes bytes{0x4e, 0, 0, 0, static_cast<uint8_t>(service), 0xc1, static_cast<uint8_t>(part), 1, 0, 1, 0, 2, 1, 0x4e};
    if (!empty)
    {
        const Bytes event{0, 10, 0xeb, 0x96, 0x12, 0x30, 0, 1, 0, 0, 0x80, 12,
                          0x4d, 10, 'j', 'p', 'n', 5, 0x0e, 'N', 'E', 'W', 'S', 0};
        bytes.insert(bytes.end(), event.begin(), event.end());
    }
    return section(bytes);
}
int main()
{
    auto parser = std::make_unique<OneSegServiceInformation>();
    parser->select(8, 1);
    unsigned scc = 0, ecc = 0;
    feed(*parser, 0x11, sdt(), scc);
    assert(parser->snapshot().service_valid && parser->snapshot().name.size == 5);
    assert(parser->snapshot().name.bytes[0] == 0x0e);
    feed(*parser, 0x27, eit(0), ecc);
    assert(parser->snapshot().present.valid && parser->snapshot().present.id == 10);
    assert(parser->snapshot().present.title.size == 5 && parser->snapshot().present.language[0] == 'j');
    feed(*parser, 0x27, eit(1), ecc);
    assert(parser->snapshot().following.valid);
    auto corrupted = sdt();
    corrupted.back() ^= 1;
    feed(*parser, 0x11, corrupted, scc);
    assert(parser->snapshot().crc_errors == 1 && parser->snapshot().service_valid);
    feed(*parser, 0x27, eit(0, true), ecc);
    assert(!parser->snapshot().present.valid && parser->snapshot().following.valid);
    feed(*parser, 0x27, eit(0, false, 9), ecc);
    assert(!parser->snapshot().present.valid);
    feed(*parser, 0x27, eit(0), ecc);
    assert(parser->snapshot().present.valid);
    parser->select(9, 1);
    assert(!parser->snapshot().service_valid && !parser->snapshot().present.valid);
    feed(*parser, 0x11, sdt(), scc);
    assert(!parser->snapshot().service_valid);
    parser->select(8, 1);
    feed(*parser, 0x11, sdt(0xc0), scc);
    assert(!parser->snapshot().service_valid); // Future table must not publish.
    auto malformed = sdt();
    malformed.resize(malformed.size() - 4);
    malformed[17] = 250;
    feed(*parser, 0x11, section(malformed), scc);
    assert(!parser->snapshot().service_valid && parser->snapshot().malformed);
    // Long unrelated descriptor forces real multi-packet section assembly.
    Bytes large{0x42, 0, 0, 0, 1, 0xc1, 0, 0, 0, 2, 0xff, 0, 8, 0xff, 0xf1, 12, 0x80, 255};
    large.resize(large.size() + 255, 0);
    const Bytes tail{0x48, 9, 1, 0, 6, 0x0e, 'L', 'A', 'R', 'G', 'E'};
    large.insert(large.end(), tail.begin(), tail.end());
    feed(*parser, 0x11, section(large), scc);
    assert(parser->snapshot().service_valid && parser->snapshot().name.size == 6);
    // Different languages are valid. Prefer Japanese regardless of wire order.
    for (bool japanese_first : {false, true})
    {
        auto multilingual = eit(0);
        multilingual.resize(multilingual.size() - 4);
        Bytes english{0x4d, 10, 'e', 'n', 'g', 5, 0x0e, 'E', 'N', 'G', 'L', 0};
        multilingual.insert(japanese_first ? multilingual.end() : multilingual.begin() + 26, english.begin(), english.end());
        multilingual[25] = 24;
        feed(*parser, 0x27, section(multilingual), ecc, 1000);
        assert(parser->snapshot().present.language[0] == 'j');
        assert(parser->snapshot().present.title.bytes[1] == 'N');
    }
    // The same language twice is invalid and cannot replace the prior event.
    auto duplicate = eit(0);
    duplicate.resize(duplicate.size() - 4);
    const Bytes repeated(duplicate.begin() + 26, duplicate.end());
    duplicate.insert(duplicate.end(), repeated.begin(), repeated.end());
    duplicate[25] = 24;
    const auto before_bad = parser->snapshot().malformed;
    feed(*parser, 0x27, section(duplicate), ecc, 2000);
    assert(parser->snapshot().malformed == before_bad + 1);
    assert(parser->snapshot().present.received_ms == 1000);
    feed(*parser, 0x11, sdt(), scc, 1000);
    feed(*parser, 0x27, eit(1), ecc, 2000);
    auto expiring = parser->snapshot();
    OneSegServiceInformation::expire(expiring, 30999);
    assert(expiring.service_valid && expiring.present.valid && expiring.following.valid);
    OneSegServiceInformation::expire(expiring, 31000);
    assert(!expiring.service_valid && !expiring.name.size && !expiring.present.valid && expiring.following.valid);
    OneSegServiceInformation::expire(expiring, 32000);
    assert(!expiring.following.valid);
    auto backwards = parser->snapshot();
    OneSegServiceInformation::expire(backwards, 999);
    assert(!backwards.service_valid && !backwards.present.valid && !backwards.following.valid);
    parser->reset();
    assert(!parser->snapshot().service && !parser->snapshot().service_valid);
    std::cout << "SDT/EIT current/following raw ARIB metadata boundaries passed (TEST FIXTURE - NO RF)\n";
}
