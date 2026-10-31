#include "platform/linux/oneseg_arib_text.h"
#include "platform/linux/oneseg_service_text.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace platform::linux_runtime;
using Error = OneSegAribTextError;
static void converted(std::initializer_list<uint8_t> bytes, const char* expected)
{
    const auto result = decode_oneseg_arib_si(bytes.begin(), bytes.size());
    assert(result && result.text == expected);
}
static void rejected(std::initializer_list<uint8_t> bytes, Error expected)
{
    const auto result = decode_oneseg_arib_si(bytes.begin(), bytes.size());
    assert(!result && result.error == expected && result.text.empty());
}
int main()
{
    converted({0x0e, 'N', 'H', 'K'}, u8"ＮＨＫ");
    converted({0x46, 0x7c, 0x4b, 0x5c}, u8"日本");
    converted({0x19, 0x22}, u8"あ");
    converted({0x1d, 0x22}, u8"ア");
    converted({0xa2}, u8"あ");
    converted({0x1b, 0x28, 0x4a, 'A'}, u8"Ａ");
    converted({0x1b, 0x24, 0x42, 0x46, 0x7c}, u8"日");
    converted({0x1b, 0x24, 0x29, 0x42, 0x0e, 0x46, 0x7c}, u8"日");
    converted({0x1b, 0x7e, 0xce}, u8"Ｎ");
    converted({0x1b, 0x24, 0x29, 0x42, 0x1b, 0x7e, 0xc6, 0xfc}, u8"日");
    converted({0x1b, 0x6f, 0x22}, u8"ア");
    converted({}, "");
    rejected({0x46}, Error::Incomplete);
    rejected({0x46, 0x7c, 0x4b}, Error::Incomplete); // No partial valid prefix returned.
    rejected({0x1b}, Error::Incomplete);
    rejected({0x1b, 0x24}, Error::Incomplete);
    rejected({0x1b, 0x24, 0x29}, Error::Incomplete);
    rejected({0x1b, 0x2b, 0x20}, Error::Incomplete);
    rejected({0x19}, Error::Incomplete);
    rejected({0x1d}, Error::Incomplete);
    rejected({0x19, 0x0e, 'A'}, Error::Malformed);
    rejected({0x46, 0xa1}, Error::Malformed);
    rejected({0x1b, 0x24, 0x29, 0x42, 0x1b, 0x7e, 0xc6}, Error::Incomplete);
    rejected({0x1b, 0x24, 0x29, 0x42, 0x1b, 0x7e, 0xc6, 0x7c}, Error::Malformed);
    rejected({0x1b, 0x24, 0x29, 0x42, 0x0e, 0x46}, Error::Incomplete);
    rejected({0x1b, 0x2b, 0x20, 0x40}, Error::Unsupported);
    rejected({0x9b, '1', '2'}, Error::Unsupported);
    assert(decode_oneseg_arib_si(nullptr, 1).error == Error::Malformed);
    std::vector<uint8_t> maximum(255, 'N');
    maximum[0] = 0x0e;
    auto result = decode_oneseg_arib_si(maximum.data(), maximum.size());
    assert(result && result.text.size() == 254 * 3);
    maximum.push_back('N');
    assert(decode_oneseg_arib_si(maximum.data(), maximum.size()).error == Error::TooLong);
    std::vector<uint8_t> repeated;
    for (unsigned i = 0; i < 127; ++i)
    {
        repeated.push_back(0x46);
        repeated.push_back(0x7c);
    }
    for (size_t length = 0; length <= repeated.size(); ++length)
    {
        const auto prefix = decode_oneseg_arib_si(repeated.data(), length);
        if (length % 2) assert(!prefix && prefix.error == Error::Incomplete && prefix.text.empty());
        else assert(prefix && prefix.text.size() == length / 2 * 3);
    }
    // Deterministic bounded malformed descriptors: never publish partial text.
    uint32_t seed = 7;
    for (unsigned run = 0; run < 2000; ++run)
    {
        std::vector<uint8_t> input(run % 256);
        for (auto& byte : input)
        {
            seed = seed * 1664525 + 1013904223;
            byte = seed >> 24;
        }
        const auto decoded = decode_oneseg_arib_si(input.data(), input.size());
        assert(decoded || decoded.text.empty());
    }
    OneSegServiceTextCache cache;
    OneSegServiceInformation::Snapshot info;
    info.service = 8;
    info.transport = 1;
    info.network = 2;
    info.service_valid = info.present.valid = true;
    info.name.size = 2;
    info.name.bytes[0] = 0x46;
    info.name.bytes[1] = 0x7c;
    info.present.title.size = 3;
    info.present.title.bytes[0] = 0x0e;
    info.present.title.bytes[1] = 'A';
    info.present.title.bytes[2] = 'B';
    auto decoded = cache.update(info);
    assert(decoded.service == 8 && decoded.name.text == u8"日" && decoded.present_title.text == u8"ＡＢ");
    assert(cache.conversionCount() == 2);
    info.service_received_ms = info.present.received_ms = 1000;
    for (unsigned i = 0; i < 100; ++i) assert(cache.update(info).name.text == u8"日");
    assert(cache.conversionCount() == 2); // Repeated SI broadcasts do not decode again.
    info.present.title.bytes[1] = 'C';
    assert(cache.update(info).present_title.text == u8"ＣＢ" && cache.conversionCount() == 3);
    info.present.title.size = 1;
    info.present.title.bytes[0] = 0x46;
    assert(cache.update(info).present_title.error == Error::Incomplete);
    assert(cache.update(info).present_title.text.empty() && cache.conversionCount() == 4);
    OneSegServiceInformation::expire(info, 31000);
    decoded = cache.update(info);
    assert(!decoded.service_valid && !decoded.present_valid && decoded.name.text.empty() && decoded.present_title.text.empty());
    cache.reset();
    assert(cache.conversionCount() == 0 && cache.update({}).name.text.empty());
    std::cout << "Guarded native ARIB SI conversion, cache and malformed-input checks passed (TEST FIXTURE - NO RF)\n";
}
