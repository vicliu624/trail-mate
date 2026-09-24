#include "geocaching/storage/transaction.h"
#include <array>
#include <cstring>
#include <vector>

int main()
{
    using namespace geocaching;
    uint8_t key = 0xab;
    storage::MutationView mutations[] = {{5, {&key, 1}, {}, false}, {6, {&key, 1}, {}, true}};
    std::array<uint8_t, 64> output{};
    size_t written = 99;
    const uint8_t expected[] = {0x93, 1, 0, 0x92, 0x93, 5, 0xc4, 1, 0xab, 0xc4, 0,
                                0x93, 6, 0xc4, 1, 0xab, 0xc0};
    if (!storage::encodeTransaction(0, mutations, 2, output.data(), output.size(), written) ||
        written != sizeof(expected) || std::memcmp(output.data(), expected, written)) return 1;
    mutations[1].table = 5;
    if (storage::encodeTransaction(0, mutations, 2, output.data(), output.size(), written) || written) return 2;
    mutations[1].table = 6;
    if (storage::encodeTransaction(0, mutations, 2, output.data(), sizeof(expected) - 1, written) || written) return 3;
    mutations[0].table = 14;
    if (storage::encodeTransaction(0, mutations, 1, output.data(), output.size(), written)) return 4;
    mutations[0].table = 5;
    std::vector<uint8_t> value(32768, 0);
    std::vector<uint8_t> large(70000);
    mutations[0].value = {value.data(), value.size()};
    mutations[1].value = mutations[0].value;
    mutations[1].erase = false;
    if (storage::encodeTransaction(0, mutations, 2, large.data(), large.size(), written) || written) return 5;
    if (!storage::encodeTransaction(UINT64_MAX, mutations, 1, large.data(), large.size(), written)) return 6;
    return 0;
}
