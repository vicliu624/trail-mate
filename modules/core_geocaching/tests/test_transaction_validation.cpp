#include "geocaching/storage/transaction.h"
#include <array>
int main()
{
    using namespace geocaching::storage;
    std::array<uint8_t, 64> keys{};
    std::array<MutationView, 64> mutations{}, decoded{};
    for (size_t i = 0; i < keys.size(); ++i) { keys[i] = static_cast<uint8_t>(i); mutations[i] = {5, {&keys[i], 1}, {}, true}; }
    std::array<uint8_t, 1024> encoded{}; size_t size = 0;
    if (!encodeTransaction(7, mutations.data(), mutations.size(), encoded.data(), encoded.size(), size)) return 1;
    TransactionView view;
    if (!validateTransaction({encoded.data(), size}, 7) || !decodeTransaction({encoded.data(), size}, 7, decoded.data(), decoded.size(), view)) return 2;
    for (size_t n = 0; n < size; ++n)
        if (validateTransaction({encoded.data(), n}, 7)) return 3;
    if (validateTransaction({encoded.data(), size}, 8)) return 4;
    // Corrupt the final decoded key in the fixture to duplicate the first key.
    const auto last_key_offset = static_cast<size_t>(decoded.back().key.data - encoded.data());
    encoded[last_key_offset] = 0;
    if (validateTransaction({encoded.data(), size}, 7) || decodeTransaction({encoded.data(), size}, 7, decoded.data(), decoded.size(), view)) return 5;
    return 0;
}
