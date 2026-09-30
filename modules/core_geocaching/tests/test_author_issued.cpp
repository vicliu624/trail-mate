#include "geocaching/storage/author_issued.h"
int main()
{
    using namespace geocaching;
    uint8_t key[36]{}, public_key[64]{}, bytes[192]{};
    key[35] = 1;
    public_key[0] = 7;
    RevisionHash hash;
    hash.bytes[0] = 9;
    storage::StoredTime time;
    time.monotonic_ms = 123;
    size_t size = 0;
    if (!storage::encodeAuthorIssued(hash, {public_key, 64}, time, bytes, sizeof(bytes), size)) return 1;
    storage::AuthorIssuedView out;
    if (!storage::decodeAuthorIssued({key, 36}, {bytes, size}, out) || out.revision != 1 ||
        out.author_public_key.data[0] != 7 || out.revision_hash.data[0] != 9 || out.issued_at.monotonic_ms != 123) return 2;
    key[35] = 0;
    if (storage::decodeAuthorIssued({key, 36}, {bytes, size}, out)) return 3;
    time.utc_trusted = true;
    if (storage::encodeAuthorIssued(hash, {public_key, 64}, time, bytes, sizeof(bytes), size) || size) return 4;
    return 0;
}
