#include "geocaching/storage/checkpoint.h"
int main()
{
    using namespace geocaching::storage;
    uint8_t page[] = {0x92, 0, 0x92, 0x93, 5, 0xc4, 1, 1, 0xc4, 0, 0x93, 5, 0xc4, 1, 2, 0xc4, 0};
    MutationView entries[2]; size_t count = 0;
    if (!decodeCheckpointPage({page, sizeof(page)}, 0, entries, 2, count) || count != 2) return 1;
    page[14] = 1;
    if (decodeCheckpointPage({page, sizeof(page)}, 0, entries, 2, count) || count) return 2;
    page[14] = 2;
    const uint8_t prior_key = 3;
    MutationView prior{5, {&prior_key, 1}, {}, false};
    if (decodeCheckpointPage({page, sizeof(page)}, 0, entries, 2, count, &prior)) return 3;
    if (decodeCheckpointPage({page, sizeof(page)}, 1, entries, 2, count)) return 4;
    for (size_t n = 0; n < sizeof(page); ++n)
        if (decodeCheckpointPage({page, n}, 0, entries, 2, count)) return 5;
    uint8_t tail[37] = {0x93, 1, 2, 0xc4, 32};
    CheckpointTailView decoded;
    if (!decodeCheckpointTail({tail, sizeof(tail)}, decoded) || decoded.page_count != 1 || decoded.entry_count != 2 || decoded.digest.size != 32) return 6;
    if (decodeCheckpointTail({tail, sizeof(tail) - 1}, decoded) || decoded.digest.data) return 7;
    return 0;
}
