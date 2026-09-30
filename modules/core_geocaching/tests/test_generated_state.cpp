#include "geocaching/storage/logical_state.h"
#include <array>

int main()
{
    using namespace geocaching;
    using namespace geocaching::storage;
    std::array<uint8_t, 128> first{}, second{};
    LogicalState state(first.data(), second.data(), first.size());
    const uint8_t key = 1, other_key = 2, old_bytes[] = {7, 8, 9};
    const MutationView original{5, {&key, 1}, {old_bytes, 3}, false};
    const auto valid = [](const auto&)
    { return true; };
    if (!state.apply(&original, 1, valid)) return 1;
    const MutationView updates[] = {{5, {&key, 1}, {}, false}, {10, {&other_key, 1}, {old_bytes, 3}, false}};
    unsigned generated = 0;
    const auto encode = [&](uint8_t* out, size_t capacity, size_t& written)
    {
        ++generated;
        if (capacity < 6) return false;
        std::memcpy(out, "abcdef", 6);
        written = 6;
        return true;
    };
    ByteView value;
    const auto unchanged = [&]()
    {
        return state.view().size() == 1 && state.view().find(5, {&key, 1}, value) &&
               value.size == 3 && !std::memcmp(value.data, old_bytes, 3);
    };
    if (!state.prepareGenerated(updates, 2, 0, encode, valid) || generated != 1 || !unchanged()) return 2;
    if (!state.preparedView().find(5, {&key, 1}, value) || value.size != 6 || std::memcmp(value.data, "abcdef", 6)) return 3;
    if (state.withScratch([](uint8_t*, size_t) {}) || state.beginSnapshot() || state.prepare(updates, 2, valid)) return 4;
    state.discardPrepared();
    if (!unchanged()) return 5;
    if (state.prepareGenerated(
            updates, 2, 0, [](uint8_t* out, size_t, size_t& n)
            {
            out[0] = 0xff; n = 1; return false; },
            valid) ||
        !unchanged()) return 6;
    if (state.prepareGenerated(
            updates, 2, 0, [](uint8_t*, size_t capacity, size_t& n)
            {
            n = capacity + 1; return true; },
            valid) ||
        !unchanged()) return 7;
    if (state.prepareGenerated(updates, 2, 0, encode, [](const auto&)
                               { return false; }) ||
        !unchanged()) return 8;
    // Exhaust remaining capacity after a generated value, never publish a prefix.
    if (state.prepareGenerated(
            updates, 2, 0, [](uint8_t* out, size_t capacity, size_t& n)
            {
            std::memset(out, 1, capacity); n = capacity; return true; },
            valid) ||
        !unchanged()) return 9;
    if (!state.withScratch([](uint8_t* out, size_t n)
                           { std::memset(out, 0xa5, n); }) ||
        !unchanged()) return 10;
    if (!state.prepareGenerated(updates, 2, 0, encode, valid) || !state.commitPrepared() || state.view().size() != 2 ||
        !state.view().find(5, {&key, 1}, value) || value.size != 6 || std::memcmp(value.data, "abcdef", 6)) return 11;
    // Existing keys and a newly inserted generated key both use the same path.
    const uint8_t new_key = 3;
    const MutationView insert{5, {&new_key, 1}, {}, false};
    if (!state.prepareGenerated(&insert, 1, 0, encode, valid) || !state.commitPrepared() || state.view().size() != 3) return 12;
    return 0;
}
