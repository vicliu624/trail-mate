#include "geocaching/storage/index_entry.h"
#include "geocaching/storage/index_root.h"
#include "geocaching/storage/index_shard_head.h"
#include "geocaching/storage/transaction_index_cursor.h"
#include <vector>

int main()
{
    using namespace geocaching;
    using namespace geocaching::storage;
    const uint8_t keys[] = {1, 2, 3};
    std::vector<uint8_t> large(12000, 0x5a), payload(12500);
    const MutationView changes[] = {{5, {keys, 1}, {large.data(), large.size()}, false},
                                    {4, {keys + 1, 1}, {}, false},
                                    {7, {keys + 2, 1}, {}, true}};
    size_t size = 0;
    if (!encodeTransaction(6, changes, 3, payload.data(), payload.size(), size)) return 1;
    payload.resize(size);
    RecordHeader header;
    if (!makeRecordHeader(RecordKind::Transaction, 7, {payload.data(), payload.size()}, header)) return 2;
    std::vector<uint8_t> segment(37, 0);
    segment.insert(segment.end(), header.begin(), header.end());
    segment.insert(segment.end(), payload.begin(), payload.end());
    const ByteView frame{segment.data() + 37, segment.size() - 37};
    TransactionIndexCursor cursor;
    IndexedMutation entry;
    if (!cursor.open(frame, 6, 5, 37) || !cursor.next(entry) || entry.table != 5 || entry.erase ||
        entry.location.segment_first_sequence != 5 || entry.location.record_sequence != 7 || entry.location.frame_offset != 37 ||
        entry.location.value_size != large.size() || entry.location.value_offset + entry.location.value_size > segment.size() ||
        std::memcmp(segment.data() + entry.location.value_offset, large.data(), large.size())) return 3;
    if (!cursor.next(entry) || entry.table != 4 || entry.erase || entry.location.value_size || !entry.location.value_offset) return 4;
    if (!cursor.next(entry) || entry.table != 7 || !entry.erase || entry.location.value_size || entry.location.value_offset || cursor.next(entry)) return 5;
    if (cursor.open(frame, 5, 5, 37) || cursor.next(entry) || cursor.open(frame, 6, 8, 37) ||
        cursor.open(frame, 6, 5, UINT32_MAX - 8)) return 6;
    segment.back() ^= 1;
    if (cursor.open(frame, 6, 5, 37) || cursor.next(entry)) return 7;

    // A CRC-valid transaction with duplicate keys must yield no index entries.
    uint8_t duplicate[64];
    protocol::CmpWriter writer(duplicate, sizeof(duplicate));
    if (!writer.array(3) || !writer.unsignedInteger(1) || !writer.unsignedInteger(6) || !writer.array(2)) return 8;
    for (unsigned i = 0; i < 2; ++i)
        if (!writer.array(3) || !writer.unsignedInteger(4) || !writer.binary({keys, 1}) || !writer.nil()) return 9;
    if (!makeRecordHeader(RecordKind::Transaction, 7, {duplicate, writer.size()}, header)) return 10;
    std::vector<uint8_t> bad(header.begin(), header.end());
    bad.insert(bad.end(), duplicate, duplicate + writer.size());
    if (cursor.open({bad.data(), bad.size()}, 6, 7, 0) || cursor.next(entry)) return 11;
    VolumeInstance volume{};
    IndexedMutation locator;
    locator.table = 5;
    locator.key = {keys, 1};
    locator.location = {5, 7, 37, 90, 12000};
    IndexEntryBytes encoded;
    if (!encodeIndexEntry(volume, locator, encoded) || !decodeIndexEntry({encoded.data(), encoded.size()}, volume, entry) ||
        entry.table != locator.table || entry.location.value_offset != 90 || entry.location.value_size != 12000 || entry.key.data[0] != keys[0]) return 12;
    const auto original = encoded;
    if (encodeIndexEntry(volume, entry, encoded) || encoded != original) return 13;
    for (size_t i = 0; i < encoded.size(); ++i)
    {
        encoded[i] ^= 1;
        if (decodeIndexEntry({encoded.data(), encoded.size()}, volume, entry) || entry.key.data) return 14;
        encoded[i] ^= 1;
    }
    volume[0] = 1;
    if (decodeIndexEntry({encoded.data(), encoded.size()}, volume, entry)) return 15;
    volume[0] = 0;
    encoded[147] = 1;
    const auto crc = ::sys::crc32(encoded.data(), 148);
    for (unsigned i = 0; i < 4; ++i) encoded[148 + i] = static_cast<uint8_t>(crc >> ((3 - i) * 8));
    if (decodeIndexEntry({encoded.data(), encoded.size()}, volume, entry)) return 16;
    locator.location.value_offset = 38;
    if (encodeIndexEntry(volume, locator, encoded)) return 17;
    locator.erase = true;
    locator.location.value_offset = locator.location.value_size = 0;
    if (!encodeIndexEntry(volume, locator, encoded) || !decodeIndexEntry({encoded.data(), encoded.size()}, volume, entry) || !entry.erase) return 18;
    IndexShardHead first{11, 1, kIndexEntrySize, 5, 17}, second{11, 2, 2 * kIndexEntrySize, 5, 17}, selected;
    IndexShardHeadBytes head_bytes;
    if (!encodeIndexShardHead(volume, second, head_bytes) ||
        !decodeIndexShardHead({head_bytes.data(), head_bytes.size()}, volume, 11, 5, 17, selected) || selected.length != second.length) return 19;
    if (!selectIndexShardHead(first, second, 1, selected) || selected.sequence != 1 ||
        !selectIndexShardHead(first, second, 2, selected) || selected.sequence != 2 ||
        selectIndexShardHead(first, second, 0, selected)) return 20;
    auto conflicting_head = second;
    conflicting_head.length += kIndexEntrySize;
    if (selectIndexShardHead(second, conflicting_head, 2, selected)) return 21;
    conflicting_head = second;
    conflicting_head.epoch = 12;
    if (selectIndexShardHead(first, conflicting_head, 2, selected)) return 22;
    for (size_t i = 0; i < head_bytes.size(); ++i)
    {
        head_bytes[i] ^= 1;
        if (decodeIndexShardHead({head_bytes.data(), head_bytes.size()}, volume, 11, 5, 17, selected)) return 23;
        head_bytes[i] ^= 1;
    }
    if (decodeIndexShardHead({head_bytes.data(), head_bytes.size()}, volume, 12, 5, 17, selected)) return 24;
    std::array<uint8_t, kIndexShardBitmapSize> bitmap{};
    IndexRootView initial{11, 0, 1, 'a', {bitmap.data(), bitmap.size()}}, root;
    IndexRootBytes root_bytes;
    if (!encodeIndexRoot(volume, initial, root_bytes) || !decodeIndexRoot({root_bytes.data(), root_bytes.size()}, volume, root)) return 25;
    bitmap[(5 - 1) * 32 + 17 / 8] |= 1u << (17 % 8);
    IndexRootView next{11, 1, 2, 'a', {bitmap.data(), bitmap.size()}}, chosen;
    if (!indexHasShard(next, 5, 17) || indexHasShard(next, 5, 18) || !selectIndexRoot(root, next, chosen) || chosen.sequence != 1) return 26;
    auto bad_root = next;
    bad_root.sequence = 2;
    if (selectIndexRoot(root, bad_root, chosen)) return 27;
    bad_root = next;
    bad_root.epoch = 12;
    if (selectIndexRoot(root, bad_root, chosen)) return 28;
    bad_root.slot = 'b';
    if (!selectIndexRoot(root, bad_root, chosen)) return 29;
    if (!encodeIndexRoot(volume, next, root_bytes) || !decodeIndexRoot({root_bytes.data(), root_bytes.size()}, volume, root) ||
        !encodeIndexRoot(volume, root, root_bytes) || !decodeIndexRoot({root_bytes.data(), root_bytes.size()}, volume, root) || !indexHasShard(root, 5, 17)) return 30;
    for (size_t i = 0; i < root_bytes.size(); ++i)
    {
        root_bytes[i] ^= 1;
        if (decodeIndexRoot({root_bytes.data(), root_bytes.size()}, volume, chosen)) return 31;
        root_bytes[i] ^= 1;
    }
    return 0;
}
