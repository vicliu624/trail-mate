#include "geocaching/storage/checkpoint_verifier.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include <vector>
struct Digest
{
    std::vector<uint8_t> bytes;
    void update(const uint8_t* data, size_t size) { bytes.insert(bytes.end(), data, data + size); }
    bool finalize(uint8_t* output, size_t) { chat::reticulum::fullHash(bytes.data(), bytes.size(), output); return true; }
};
std::vector<uint8_t> wrap(geocaching::storage::RecordKind kind, geocaching::ByteView payload)
{
    geocaching::storage::RecordHeader header;
    if (!geocaching::storage::makeRecordHeader(kind, 7, payload, header)) return {};
    std::vector<uint8_t> result(header.begin(), header.end());
    result.insert(result.end(), payload.data, payload.data + payload.size); return result;
}
int main()
{
    using namespace geocaching::storage;
    const uint8_t page_data[] = {0x92, 0, 0x91, 0x93, 5, 0xc4, 1, 1, 0xc4, 0};
    auto page = wrap(RecordKind::CheckpointPage, {page_data, sizeof(page_data)});
    uint8_t tail_data[37] = {0x93, 1, 1, 0xc4, 32};
    chat::reticulum::fullHash(page.data(), page.size(), tail_data + 5);
    auto tail = wrap(RecordKind::CheckpointTail, {tail_data, sizeof(tail_data)});
    Digest digest; CheckpointVerifier<Digest> verifier(digest); MutationView entry; size_t count = 0;
    if (!verifier.accept({page.data(), page.size()}, &entry, 1, count) || count != 1 || verifier.verified() ||
        !verifier.accept({tail.data(), tail.size()}, &entry, 1, count) || verifier.verified() ||
        !verifier.finish() || verifier.sequence() != 7) return 1;
    if (verifier.accept({page.data(), page.size()}, &entry, 1, count) || verifier.verified()) return 2;
    Digest missing_digest; CheckpointVerifier<Digest> missing(missing_digest);
    if (!missing.accept({page.data(), page.size()}, &entry, 1, count) || missing.finish()) return 3;
    tail_data[5] ^= 1;
    tail = wrap(RecordKind::CheckpointTail, {tail_data, sizeof(tail_data)});
    Digest bad_digest; CheckpointVerifier<Digest> bad(bad_digest);
    if (!bad.accept({page.data(), page.size()}, &entry, 1, count) || bad.accept({tail.data(), tail.size()}, &entry, 1, count)) return 4;
    return 0;
}
