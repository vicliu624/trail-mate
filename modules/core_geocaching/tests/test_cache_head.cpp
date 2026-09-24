#include "geocaching/storage/cache_head.h"
#include "geocaching/protocol/cmp_writer.h"
int main()
{
    using namespace geocaching;
    uint8_t key[32]{}, hash[32]{}, value[128]{};
    protocol::CmpWriter writer(value, sizeof(value));
    if (!writer.array(4) || !writer.binary({hash, 32}) || !writer.unsignedInteger(0) ||
        !writer.unsignedInteger(1) || !writer.unsignedInteger(3)) return 1;
    storage::CacheHeadView head;
    if (!storage::decodeCacheHead({key, 32}, {value, writer.size()}, head) || head.install_generation != 1 || head.highest_seen_revision != 3) return 2;
    for (size_t n = 0; n < writer.size(); ++n)
        if (storage::decodeCacheHead({key, 32}, {value, n}, head) || head.current_hash.data) return 3;
    uint8_t empty[] = {0x94, 0xc0, 0, 1, 0};
    if (!storage::decodeCacheHead({key, 32}, {empty, sizeof(empty)}, head) || head.current_hash.size) return 4;
    empty[3] = 0;
    if (storage::decodeCacheHead({key, 32}, {empty, sizeof(empty)}, head)) return 5;
    empty[3] = 1; empty[2] = 3;
    if (storage::decodeCacheHead({key, 32}, {empty, sizeof(empty)}, head)) return 6;
    return 0;
}
