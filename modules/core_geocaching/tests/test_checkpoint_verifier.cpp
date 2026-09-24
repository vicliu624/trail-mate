#include "chat/infra/reticulum/reticulum_wire.h"
#include "geocaching/storage/checkpoint_encoding.h"
#include "geocaching/storage/checkpoint_verifier.h"
#include <vector>
struct Digest
{
    std::vector<uint8_t> bytes;
    void update(const uint8_t* data, size_t size) { bytes.insert(bytes.end(), data, data + size); }
    bool finalize(uint8_t* output, size_t)
    {
        chat::reticulum::fullHash(bytes.data(), bytes.size(), output);
        return true;
    }
};
std::vector<uint8_t> wrap(geocaching::storage::RecordKind kind, geocaching::ByteView payload)
{
    geocaching::storage::RecordHeader header;
    if (!geocaching::storage::makeRecordHeader(kind, 7, payload, header)) return {};
    std::vector<uint8_t> result(header.begin(), header.end());
    result.insert(result.end(), payload.data, payload.data + payload.size);
    return result;
}
int main()
{
    using namespace geocaching::storage;
    const uint8_t page_data[] = {0x92, 0, 0x91, 0x93, 5, 0xc4, 1, 1, 0xc4, 0};
    auto page = wrap(RecordKind::CheckpointPage, {page_data, sizeof(page_data)});
    uint8_t tail_data[37] = {0x93, 1, 1, 0xc4, 32};
    chat::reticulum::fullHash(page.data(), page.size(), tail_data + 5);
    auto tail = wrap(RecordKind::CheckpointTail, {tail_data, sizeof(tail_data)});
    Digest digest;
    CheckpointVerifier<Digest> verifier(digest);
    MutationView entry;
    size_t count = 0;
    if (!verifier.accept({page.data(), page.size()}, &entry, 1, count) || count != 1 || verifier.verified() ||
        !verifier.accept({tail.data(), tail.size()}, &entry, 1, count) || verifier.verified() ||
        !verifier.finish() || verifier.sequence() != 7) return 1;
    if (verifier.accept({page.data(), page.size()}, &entry, 1, count) || verifier.verified()) return 2;
    Digest missing_digest;
    CheckpointVerifier<Digest> missing(missing_digest);
    if (!missing.accept({page.data(), page.size()}, &entry, 1, count) || missing.finish()) return 3;
    tail_data[5] ^= 1;
    tail = wrap(RecordKind::CheckpointTail, {tail_data, sizeof(tail_data)});
    Digest bad_digest;
    CheckpointVerifier<Digest> bad(bad_digest);
    if (!bad.accept({page.data(), page.size()}, &entry, 1, count) || bad.accept({tail.data(), tail.size()}, &entry, 1, count)) return 4;
    // Writer output must be consumable by the same verifier used on restart.
    Digest generated_digest;
    Digest reader_digest;
    CheckpointVerifier<Digest> generated_reader(reader_digest);
    const uint8_t keys[] = {1, 2};
    const uint8_t value[] = {0x91, 0xc0};
    MutationView rows[] = {{5, {keys, 1}, {value, sizeof(value)}, false},
                           {5, {keys + 1, 1}, {value, sizeof(value)}, false}};
    for (unsigned i = 0; i < 2; ++i)
    {
        CheckpointPageEncoding encoding;
        if (!encoding.open(i, rows + i, 1, i ? rows : nullptr)) return 5;
        std::vector<uint8_t> payload(encoding.size());
        size_t written = 0;
        if (!encoding.readSlice(0, payload.data(), payload.size(), written) || written != payload.size()) return 6;
        const auto frame = wrap(RecordKind::CheckpointPage, {payload.data(), payload.size()});
        generated_digest.update(frame.data(), frame.size());
        if (!generated_reader.accept({frame.data(), frame.size()}, &entry, 1, count) || count != 1 ||
            entry.table != 5 || entry.key.size != 1 || entry.key.data[0] != keys[i] ||
            entry.value.size != sizeof(value) || std::memcmp(entry.value.data, value, sizeof(value))) return 7;
    }
    uint8_t hash[32], generated_tail[64];
    size_t tail_size = 0;
    if (!generated_digest.finalize(hash, sizeof(hash)) ||
        !encodeCheckpointTail(2, 2, {hash, sizeof(hash)}, generated_tail, sizeof(generated_tail), tail_size)) return 8;
    const auto final_frame = wrap(RecordKind::CheckpointTail, {generated_tail, tail_size});
    if (!generated_reader.accept({final_frame.data(), final_frame.size()}, &entry, 1, count) || count ||
        !generated_reader.finish() || generated_reader.sequence() != 7 ||
        std::memcmp(generated_reader.digest().data(), hash, sizeof(hash))) return 9;
    return 0;
}
