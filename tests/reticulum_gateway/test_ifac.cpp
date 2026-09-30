#include "platform/esp/arduino_common/chat/infra/reticulum/ifac_codec.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

const void* discovery_test_psram = nullptr;
using chat::reticulum::IfacCodec;

std::vector<uint8_t> unhex(const std::string& hex)
{
    std::vector<uint8_t> result;
    for (size_t i = 0; i < hex.size(); i += 2) result.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return result;
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    IfacCodec codec;
    assert(!codec.configure("trail", "password", 16)); // ESP internal RAM rejected.
    discovery_test_psram = &codec;
    std::ifstream input(argv[1]);
    assert(input.good());
    std::string line;
    size_t count = 0;
    std::array<uint8_t, 565> frame{};
    while (std::getline(input, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::array<std::string, 5> field;
        for (auto& item : field) assert(bool(std::getline(fields, item, '\t')));
        const auto name_bytes = unhex(field[0]), pass_bytes = unhex(field[1]);
        const std::string name(name_bytes.begin(), name_bytes.end()), password(pass_bytes.begin(), pass_bytes.end());
        const auto tag = std::stoul(field[2]);
        const auto plain = unhex(field[3]), wire = unhex(field[4]);
        assert(codec.configure(name.c_str(), password.c_str(), tag));
        std::copy(plain.begin(), plain.end(), frame.begin());
        size_t size = plain.size();
        assert(codec.encode(frame.data(), size, frame.size()));
        assert(size == wire.size() && std::equal(wire.begin(), wire.end(), frame.begin()));
        assert(codec.decode(frame.data(), size, frame.size()));
        assert(size == plain.size() && std::equal(plain.begin(), plain.end(), frame.begin()));
        // Received Python wire, independently of our outgoing path.
        std::copy(wire.begin(), wire.end(), frame.begin());
        size = wire.size();
        assert(codec.decode(frame.data(), size, frame.size()));
        assert(size == plain.size() && std::equal(plain.begin(), plain.end(), frame.begin()));
        std::copy(plain.begin(), plain.end(), frame.begin());
        size = plain.size();
        assert(!codec.encode(frame.data(), size, plain.size() + tag - 1) && size == 0);
        size = plain.size();
        assert(!codec.decode(frame.data(), size, frame.size()) && size == 0); // Missing flag.
        if (tag == 16)
        {
            for (size_t position : {size_t(1), size_t(2), wire.size() - 1})
            {
                std::copy(wire.begin(), wire.end(), frame.begin());
                frame[position] ^= 1;
                size = wire.size();
                assert(!codec.decode(frame.data(), size, frame.size()) && size == 0);
                assert(std::all_of(frame.begin(), frame.begin() + wire.size(), [](auto byte)
                                   { return byte == 0; }));
            }
            assert(codec.configure("wrong", "credentials", tag));
            std::copy(wire.begin(), wire.end(), frame.begin());
            size = wire.size();
            assert(!codec.decode(frame.data(), size, frame.size()) && size == 0);
        }
        ++count;
    }
    assert(count == 24);
    assert(!codec.configure("trail", "password", 0));
    size_t size = 3;
    assert(!codec.encode(frame.data(), size, frame.size()) && size == 0);
    assert(!codec.configure("trail", "password", 65));
    std::string too_long(129, 'a');
    assert(!codec.configure(too_long.c_str(), "", 16));
    assert(codec.configure(nullptr, "", 0));
    frame[0] = 0x14;
    size = 3;
    assert(codec.encode(frame.data(), size, frame.size()) && size == 3);
    assert(codec.decode(frame.data(), size, frame.size()) && size == 3);
    frame[0] |= 0x80;
    assert(!codec.decode(frame.data(), size, frame.size()) && size == 0);
    assert(codec.configure("trail", "", 16));
    size = 18;
    assert(!codec.decode(frame.data(), size, frame.size()) && size == 0);
    size = 565;
    assert(!codec.decode(frame.data(), size, frame.size()) && size == 0);
    frame[0] = 0x14;
    size = 501;
    assert(!codec.encode(frame.data(), size, frame.size()) && size == 0);
    size = 3;
    assert(!codec.encode(nullptr, size, frame.size()) && size == 0);
    codec.clear();
    size = 3;
    assert(!codec.decode(frame.data(), size, frame.size()) && size == 0);
}
