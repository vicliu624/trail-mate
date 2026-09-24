#include "geocaching/storage/checkpoint.h"
#include "geocaching/storage/checkpoint_encoding.h"
#include <vector>
int main()
{
    using namespace geocaching::storage;
    uint8_t page[] = {0x92, 0, 0x92, 0x93, 5, 0xc4, 1, 1, 0xc4, 0, 0x93, 5, 0xc4, 1, 2, 0xc4, 0};
    MutationView entries[2];
    size_t count = 0;
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
    CheckpointPageEncoding encoder;
    if (!decodeCheckpointPage({page, sizeof(page)}, 0, entries, 2, count) ||
        !encoder.open(0, entries, count) || encoder.size() != sizeof(page)) return 8;
    uint8_t output[64];
    size_t written = 0;
    for (size_t slice = 1; slice <= sizeof(page); ++slice)
    {
        size_t offset = 0;
        while (offset < encoder.size())
        {
            if (!encoder.readSlice(offset, output, slice, written) || !written ||
                std::memcmp(output, page + offset, written)) return 9;
            offset += written;
        }
    }
    if (encoder.readSlice(0, page, sizeof(page), written) || written) return 10;
    if (!encoder.readSlice(encoder.size(), nullptr, 0, written) || written ||
        encoder.readSlice(encoder.size() + 1, output, sizeof(output), written)) return 11;
    if (encoder.open(1, entries, count, &prior) || encoder.size()) return 12;
    entries[1] = entries[0];
    if (encoder.open(0, entries, 2)) return 13;
    entries[0].erase = true;
    if (encoder.open(0, entries, 1)) return 14;
    if (!encoder.open(UINT64_MAX, nullptr, 0) ||
        !encoder.readSlice(0, output, sizeof(output), written)) return 15;
    CheckpointPageCursor empty;
    if (!empty.open({output, written}, UINT64_MAX) || !empty.complete() || empty.count()) return 16;
    // A legal large value is sliced without a page-sized encoder allocation.
    std::vector<uint8_t> large(32768, 0x5a);
    const uint8_t key[] = {1, 2};
    MutationView large_rows[] = {{1, {key, 1}, {large.data(), large.size()}, false},
                                 {1, {key + 1, 1}, {large.data(), large.size()}, false}};
    if (!encoder.open(0, large_rows, 1)) return 17;
    std::vector<uint8_t> encoded(encoder.size());
    for (size_t offset = 0; offset < encoded.size();)
    {
        if (!encoder.readSlice(offset, output, sizeof(output), written) || !written) return 18;
        std::memcpy(encoded.data() + offset, output, written);
        offset += written;
    }
    if (!decodeCheckpointPage({encoded.data(), encoded.size()}, 0, entries, 2, count) || count != 1 ||
        entries[0].value.size != large.size() || std::memcmp(entries[0].value.data, large.data(), large.size())) return 19;
    if (encoder.open(0, large_rows, 2) || encoder.size()) return 20;
    uint8_t digest[32] = {0x42};
    if (!encodeCheckpointTail(UINT64_MAX, UINT64_MAX, {digest, sizeof(digest)}, output, sizeof(output), written) ||
        !decodeCheckpointTail({output, written}, decoded) || decoded.page_count != UINT64_MAX ||
        decoded.entry_count != UINT64_MAX || std::memcmp(decoded.digest.data, digest, sizeof(digest))) return 21;
    if (encodeCheckpointTail(1, 2, {digest, 31}, output, sizeof(output), written) || written ||
        encodeCheckpointTail(1, 2, {digest, 32}, output, 1, written) || written) return 22;
    return 0;
}
