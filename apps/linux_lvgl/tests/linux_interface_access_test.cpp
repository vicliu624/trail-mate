#include "chat/infra/reticulum/interface_access_codec.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/ifac_codec.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <esp_memory_utils.h>
#include <string>

template <class Codec>
void verify()
{
    // Frozen from the original ESP implementation before extraction.
    const char* vectors[] = {
        "a46100126317d8794e37601b3d73cb2982ddb10c5a",
        "ada881db84643aa06900e28d34681004d940187fa5519ef697b8996f",
        "bbf55e2c91f9db28359e81db84643aa06900e39d6a63ac277dd7530b2cbcc164954e396a",
        "8d290dbf6e6e9170349714588aeec368f792146a81469df81089b1fdac8528d1064ae74ce33f08461f3e8f274bef1acc3c705e2c91f9db28359e81db84643aa069000b529faf6c92a701c5489b41a6eb7bf53fe4"};
    const unsigned tags[] = {1, 8, 16, 64};
    for (unsigned case_index = 0; case_index < 4; ++case_index)
    {
        const auto tag = tags[case_index];
        Codec encoder, decoder, foreign;
        assert(encoder.configure("trail-mate-fixture", "access-fixture", tag));
        assert(decoder.configure("trail-mate-fixture", "access-fixture", tag));
        assert(foreign.configure("trail-mate-fixture", "wrong-fixture", tag));
        std::array<uint8_t, 564> plain{}, encrypted{};
        for (unsigned index = 0; index < 20; ++index) plain[index] = uint8_t(index * 7);
        encrypted = plain;
        size_t size = 20;
        assert(encoder.encode(encrypted.data(), size, encrypted.size()) && size == 20 + tag);
        std::string hex;
        constexpr char digits[] = "0123456789abcdef";
        for (size_t index = 0; index < size; ++index)
        {
            hex += digits[encrypted[index] >> 4];
            hex += digits[encrypted[index] & 15];
        }
        assert(hex == vectors[case_index]);
        for (size_t index = 0; index < size; ++index)
        {
            // An 8-bit compatibility tag can collide. Its exact encoding and
            // round trip are checked, without promising collision-free MACs.
            if (tag == 1) break;
            auto damaged = encrypted;
            damaged[index] ^= 1;
            auto damaged_size = size;
            const bool accepted = decoder.decode(damaged.data(), damaged_size, damaged.size());
            assert(!accepted && damaged_size == 0);
        }
        auto wrong = encrypted;
        auto wrong_size = size;
        if (tag > 1) assert(!foreign.decode(wrong.data(), wrong_size, wrong.size()) && wrong_size == 0);
        assert(decoder.decode(encrypted.data(), size, encrypted.size()) && size == 20 && !std::memcmp(plain.data(), encrypted.data(), size));
        size = 20;
        assert(!encoder.encode(encrypted.data(), size, 20) && size == 0);
        size = 500;
        plain.fill(0x7e);
        plain[0] = 0;
        encrypted = plain;
        assert(encoder.encode(encrypted.data(), size, encrypted.size()));
        assert(decoder.decode(encrypted.data(), size, encrypted.size()) && size == 500 && !std::memcmp(plain.data(), encrypted.data(), size));
        assert(!encoder.configure("name", "pass", 0));
        size = 20;
        assert(!encoder.encode(encrypted.data(), size, encrypted.size()) && size == 0);
        const std::string too_long(129, 'x');
        assert(!encoder.configure(too_long.c_str(), "", tag));
        assert(encoder.configure("", "", 0));
        encrypted = plain;
        size = 500;
        assert(encoder.encode(encrypted.data(), size, encrypted.size()) && size == 500 && encrypted == plain);
        encrypted[0] |= 0x80;
        assert(!encoder.decode(encrypted.data(), size, encrypted.size()) && size == 0);
        encoder.clear();
        size = 20;
        assert(!encoder.encode(encrypted.data(), size, encrypted.size()) && size == 0);
    }
}
int main()
{
    verify<chat::reticulum::InterfaceAccessCodec>();
    verify<chat::reticulum::IfacCodec>();
    chat::reticulum::IfacCodec esp;
    assert(esp.configure("name", "pass", 8));
    ifac_test_external_memory = false;
    assert(!esp.configure("name", "pass", 8));
    std::array<uint8_t, 64> frame{};
    size_t size = 20;
    assert(!esp.encode(frame.data(), size, frame.size()) && size == 0);
    ifac_test_external_memory = true;
    assert(esp.configure("name", "pass", 8));
}
