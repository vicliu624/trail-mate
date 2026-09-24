#include "geocaching/storage/transaction.h"
#include <array>

int main()
{
    using namespace geocaching;
    uint8_t bytes[] = {0x93, 1, 7, 0x92, 0x93, 5, 0xc4, 1, 0xab, 0xc4, 0,
                       0x93, 6, 0xc4, 1, 0xab, 0xc0};
    std::array<storage::MutationView, 2> scratch;
    storage::TransactionView transaction;
    auto decode = [&](size_t size, uint64_t previous = 7, size_t capacity = 2) {
        return storage::decodeTransaction({bytes, size}, previous, scratch.data(), capacity, transaction);
    };
    if (!decode(sizeof(bytes)) || transaction.count != 2 || scratch[0].erase ||
        scratch[0].value.size != 0 || !scratch[1].erase) return 1;
    for (size_t size = 0; size < sizeof(bytes); ++size)
        if (decode(size) || transaction.count || transaction.mutations) return 2;
    if (decode(sizeof(bytes), 6) || decode(sizeof(bytes), 7, 1)) return 3;
    bytes[12] = 5;
    if (decode(sizeof(bytes))) return 4;
    bytes[12] = 14;
    if (decode(sizeof(bytes))) return 5;
    bytes[12] = 6; bytes[1] = 2;
    if (decode(sizeof(bytes))) return 6;
    bytes[1] = 1;
    std::array<uint8_t, sizeof(bytes) + 1> trailing{};
    std::memcpy(trailing.data(), bytes, sizeof(bytes));
    if (storage::decodeTransaction({trailing.data(), trailing.size()}, 7, scratch.data(), 2, transaction)) return 7;
    return 0;
}
