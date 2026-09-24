#include "geocaching/storage/transaction.h"
#include <array>
#include <vector>

using namespace geocaching;
using namespace geocaching::storage;

int main()
{
    const uint8_t key[] = {0x42};
    const uint8_t value[] = {0x11, 0x22};
    const uint8_t expected[] = {0x93, 1, 0, 0x91, 0x93, 1, 0xc4, 1, 0x42, 0xc4, 2, 0x11, 0x22};
    MutationView mutation{1, {key, sizeof(key)}, {value, sizeof(value)}, false};
    TransactionEncoding stream;
    if (!stream.open(0, &mutation, 1) || stream.size() != sizeof(expected)) return 1;
    // All offsets and slice sizes, including empty and final partial slices.
    for (size_t offset = 0; offset <= sizeof(expected); ++offset)
    {
        for (size_t capacity = 0; capacity <= sizeof(expected) + 1; ++capacity)
        {
            std::array<uint8_t, 32> output;
            output.fill(0xee);
            size_t written = 99;
            const size_t available = sizeof(expected) - offset;
            const size_t want = available < capacity ? available : capacity;
            if (!stream.readSlice(offset, output.data(), capacity, written) || written != want ||
                std::memcmp(output.data(), expected + offset, want) || output[want] != 0xee) return 2;
            if (!stream.matchesSlice(offset, {output.data(), want})) return 13;
            for (size_t i = 0; i < want; ++i)
            {
                output[i] ^= 1;
                if (stream.matchesSlice(offset, {output.data(), want})) return 14;
                output[i] ^= 1;
            }
        }
    }
    size_t written = 99;
    if (stream.readSlice(sizeof(expected) + 1, nullptr, 0, written) || written) return 3;
    size_t calls = 0;
    if (stream.visit([&](ByteView)
                     { ++calls; return false; }) ||
        calls != 1) return 4;

    // Host-only large fixture: device encoder must retain no copy of it.
    std::vector<uint8_t> large(32768);
    for (size_t i = 0; i < large.size(); ++i) large[i] = static_cast<uint8_t>(i);
    MutationView mutations[] = {
        {1, {key, 1}, {large.data(), large.size()}, false},
        {2, {key, 1}, {}, true},
        {3, {key, 1}, {}, false}};
    if (!stream.open(UINT64_MAX, mutations, 3)) return 5;
    std::vector<uint8_t> assembled(stream.size());
    std::array<uint8_t, 512> slice;
    for (size_t offset = 0; offset < assembled.size(); offset += written)
    {
        if (!stream.readSlice(offset, slice.data(), slice.size(), written) || !written || written > 512) return 6;
        std::memcpy(assembled.data() + offset, slice.data(), written);
    }
    MutationView decoded[3];
    TransactionView transaction;
    if (!decodeTransaction({assembled.data(), assembled.size()}, UINT64_MAX, decoded, 3, transaction) ||
        transaction.count != 3 || !decoded[1].erase || decoded[2].erase || decoded[2].value.size ||
        decoded[0].value.size != large.size() || std::memcmp(decoded[0].value.data, large.data(), large.size())) return 7;
    std::vector<uint8_t> contiguous(assembled.size());
    if (!encodeTransaction(UINT64_MAX, mutations, 3, contiguous.data(), contiguous.size(), written) ||
        written != assembled.size() || contiguous != assembled) return 8;
    if (encodeTransaction(UINT64_MAX, mutations, 3, contiguous.data(), contiguous.size() - 1, written) || written) return 9;

    MutationView duplicates[] = {mutation, mutation};
    if (stream.open(0, duplicates, 2) || stream.size() || stream.readSlice(0, nullptr, 0, written)) return 10;
    mutations[1] = mutations[0];
    mutations[1].table = 2;
    if (stream.open(0, mutations, 2) || stream.size()) return 11; // payload exceeds 65536
    mutation.erase = true;
    if (stream.open(0, &mutation, 1)) return 12;
    return 0;
}
