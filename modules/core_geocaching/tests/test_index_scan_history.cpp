#include "geocaching/storage/cache_head.h"
#include "geocaching/storage/draft_record.h"
#include "geocaching/storage/object_ref.h"
#include "platform/esp/arduino_common/geocaching/sd_index_get.h"
#include "platform/esp/arduino_common/geocaching/sd_index_initialize.h"
#include "platform/esp/arduino_common/geocaching/sd_index_scan.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"
#include "runtime_environment.h"
#include <cstdio>

namespace gc = geocaching::storage;
namespace sd = platform::esp::arduino_common::geocaching;
namespace test = runtime_test;
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
template <class Job, class Status>
Status pump(Job& job, Status working)
{
    auto status = working;
    for (unsigned i = 0; i < 100000 && status == working; ++i)
    {
        test::io_bytes = 0;
        status = job.step();
        require(test::io_bytes <= 512, "transfer budget exceeded");
    }
    return status;
}
int main()
{
    gc::VolumeInstance volume{}, confirmed;
    require(sd::createNewSdVolume(volume, confirmed) == sd::SdVolumeResult::Ready, "create volume");
    std::array<gc::IndexRootBytes, 2> roots;
    gc::IndexRootView root;
    unsigned copy = 0;
    std::array<uint8_t, 8192> frame;
    sd::SdIndexInitialize initialize(volume);
    require(initialize.begin(roots[0]) && pump(initialize, sd::IndexRootWriteStep::Working) == sd::IndexRootWriteStep::Verified, "initialize index");
    require(gc::decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, root), "initial root");
    std::array<uint8_t, 32> a{}, b{};
    const auto bucket = static_cast<uint8_t>(::sys::crc32(a.data(), a.size()));
    bool collided = false;
    for (unsigned candidate = 1; candidate < 65536; ++candidate)
    {
        b[0] = static_cast<uint8_t>(candidate);
        b[1] = static_cast<uint8_t>(candidate >> 8);
        if (static_cast<uint8_t>(::sys::crc32(b.data(), b.size())) == bucket)
        {
            collided = true;
            break;
        }
    }
    require(collided, "could not create distinct colliding keys");
    const auto commit = [&](const auto& key, bool erase, bool edited)
    {
        std::vector<uint8_t> value{0x92, 0x90, 0x90};
        if (edited)
        {
            value[2] = 0x91;
            value.insert(value.end(), {0xc4, 32});
            value.insert(value.end(), 32, 0x73);
        }
        const gc::MutationView row{11, {key.data(), key.size()}, erase ? geocaching::ByteView{} : geocaching::ByteView{value.data(), value.size()}, erase};
        sd::SdIndexedCommit transaction(volume);
        require(transaction.begin(root, copy, &row, 1, frame.data(), frame.size(), roots[1 - copy]), "begin update");
        require(pump(transaction, sd::IndexedCommitStep::Working) == sd::IndexedCommitStep::Verified && transaction.committed(root), "commit update");
        copy = 1 - copy;
    };
    commit(a, false, false);
    commit(b, false, false);
    for (unsigned i = 0; i < 40; ++i) commit(a, false, false);
    commit(b, false, true);
    for (unsigned i = 0; i < 20; ++i) commit(a, false, false);
    commit(a, true, false);
    const auto scan = [&](size_t capacity, bool corrupt)
    {
        const auto io_started = test::io_operations;
        const bool profile = test::profile_reads;
        test::profile_reads = true;
        test::read_bytes_by_path.clear();
        sd::SdIndexScan reader(volume);
        require(reader.begin(root, 11, frame.data(), capacity), "begin scan");
        unsigned steps = 0, rows = 0;
        auto state = sd::IndexScanStep::Working;
        while (steps++ < 100000)
        {
            test::io_bytes = 0;
            state = reader.step();
            require(test::io_bytes <= 512, "scan exceeded transfer budget");
            if (state == sd::IndexScanStep::Item)
            {
                gc::MutationView row;
                require(reader.item(row) && row.key.size == b.size() && !std::memcmp(row.key.data, b.data(), b.size()) && row.value.size == 37, "scan resurrected deleted or obsolete row");
                ++rows;
                require(reader.advance(), "advance scan");
            }
            else if (state != sd::IndexScanStep::Working) break;
        }
        require(state == (corrupt ? sd::IndexScanStep::Invalid : sd::IndexScanStep::End), "scan missed historical corruption or failed");
        if (!corrupt) require(rows == 1, "scan emitted duplicate keys");
        if (!corrupt && !test::file_busy_cycles)
        {
            char shard[80];
            require(sd::indexShardPathForBucket(root.slot, 11, bucket, shard, sizeof(shard)), "profile shard path");
            std::printf("History scan capacity=%zu live_rows=%u io_operations=%llu shard_bytes=%llu\n", capacity, rows,
                        static_cast<unsigned long long>(test::io_operations - io_started),
                        static_cast<unsigned long long>(test::read_bytes_by_path[shard]));
        }
        test::profile_reads = profile;
        return steps;
    };
    const auto fallback = scan(144, false);
    const auto optimized = scan(frame.size(), false);
    require(optimized * 2 < fallback, "adjacent obsolete references still repeat full lookups");
    // Hold every open/size/seek/read busy across multiple worker turns. Neither
    // scan nor lookup may advance its cursor before the operation succeeds.
    test::file_busy_cycles = 3;
    scan(frame.size(), false);
    scan(144, false);
    for (auto hits : test::file_busy_hits) require(hits > 0, "file operation contention was not exercised");
    test::file_busy_cycles = 0;
    char path[80];
    require(sd::indexShardPathForBucket(root.slot, 11, bucket, path, sizeof(path)), "shard path");
    test::files.at(path)[148] ^= 1;
    scan(frame.size(), true);
    scan(144, true);
    // The current generation contains only the remaining live reference. The
    // obsolete append file is intentionally still corrupt: neither scan nor
    // Get may inspect it once the selected head points at a current generation.
    test::files.at(path)[148] ^= 1;
    const auto legacy_length = test::files.at(path).size();
    sd::SdIndexLookup lookup(volume, root.slot, root.sequence, legacy_length);
    require(lookup.begin(11, {b.data(), b.size()}) &&
                pump(lookup, sd::IndexLookupStep::Working) == sd::IndexLookupStep::Found,
            "locate retained reference");
    gc::IndexedMutation retained;
    gc::IndexEntryBytes reference;
    require(lookup.result(retained) && gc::encodeIndexEntry(volume, retained, reference), "encode retained reference");
    gc::IndexShardHead current{root.epoch, root.sequence, gc::kIndexEntrySize, 11, bucket, true};
    char current_path[96];
    require(sd::indexShardDataPath(root.slot, current, current_path, sizeof(current_path)), "current generation path");
    test::files[current_path] = {reference.begin(), reference.end()};
    const auto publish = [&](const gc::IndexShardHead& head)
    {
        gc::IndexShardHeadBytes bytes;
        require(gc::encodeIndexShardHead(volume, head, bytes), "encode current head");
        for (unsigned copy = 0; copy < 2; ++copy)
        {
            char head_path[80];
            require(sd::indexShardHeadPathForBucket(root.slot, 11, bucket, copy, head_path, sizeof(head_path)), "current head path");
            test::files[head_path] = {bytes.begin(), bytes.end()};
        }
    };
    publish(current);
    test::files.at(path)[148] ^= 1;
    scan(frame.size(), false);
    require(test::read_bytes_by_path[path] == 0 && test::read_bytes_by_path[current_path] == gc::kIndexEntrySize,
            "current scan read obsolete history or repeated the live reference");
    std::printf("Current generation live_rows=1 obsolete_bytes=0 current_bytes=%llu\n",
                static_cast<unsigned long long>(test::read_bytes_by_path[current_path]));
    sd::SdIndexLookup current_lookup(volume, root.slot, current.sequence, current.length, nullptr, true);
    require(current_lookup.begin(11, {b.data(), b.size()}) &&
                pump(current_lookup, sd::IndexLookupStep::Working) == sd::IndexLookupStep::Found,
            "lookup current generation");
    sd::SdIndexLookup deleted_lookup(volume, root.slot, current.sequence, current.length, nullptr, true);
    require(deleted_lookup.begin(11, {a.data(), a.size()}) &&
                pump(deleted_lookup, sd::IndexLookupStep::Working) == sd::IndexLookupStep::NotFound,
            "current lookup resurrected deletion");
    test::files.at(current_path)[148] ^= 1;
    scan(frame.size(), true);
    test::files.at(current_path)[148] ^= 1;
    test::files.at(current_path).push_back(0);
    scan(frame.size(), true);
    test::files.at(current_path).pop_back();
    current.length = 0;
    publish(current);
    sd::SdIndexScan empty(volume);
    require(empty.begin(root, 11, frame.data(), frame.size()) &&
                pump(empty, sd::IndexScanStep::Working) == sd::IndexScanStep::End,
            "empty current generation");
    require(!test::open_files && !test::open_dirs, "scan leaked handles");
    // Exercise the production transaction writer, including two mutations in
    // one colliding shard and repeated replacements of the same live key.
    test::files.clear();
    test::directories = {"/", "/trailmate"};
    require(sd::createNewSdVolume(volume, confirmed) == sd::SdVolumeResult::Ready, "new current-write volume");
    sd::SdIndexInitialize current_initialize(volume);
    require(current_initialize.begin(roots[0]) && pump(current_initialize, sd::IndexRootWriteStep::Working) == sd::IndexRootWriteStep::Verified,
            "initialize current-write index");
    require(gc::decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, root), "current-write root");
    copy = 0;
    std::array<uint8_t, 32> hash{};
    hash[0] = 1;
    gc::CacheHeadView cache;
    cache.current_hash = {hash.data(), hash.size()};
    cache.install_generation = cache.highest_seen_revision = 1;
    std::array<uint8_t, 128> a_value{}, b_value{};
    size_t a_size = 0, b_size = 0;
    require(gc::encodeCacheHead({a.data(), a.size()}, cache, a_value.data(), a_value.size(), a_size) &&
                gc::encodeCacheHead({b.data(), b.size()}, cache, b_value.data(), b_value.size(), b_size),
            "current-write values");
    const auto current_commit = [&](bool erase_a, bool erase_b = false)
    {
        const gc::MutationView changes[] = {
            {2, {a.data(), a.size()}, erase_a ? geocaching::ByteView{} : geocaching::ByteView{a_value.data(), a_size}, erase_a},
            {2, {b.data(), b.size()}, erase_b ? geocaching::ByteView{} : geocaching::ByteView{b_value.data(), b_size}, erase_b}};
        sd::SdIndexedCommit transaction(volume);
        require(transaction.begin(root, copy, changes, 2, frame.data(), frame.size(), roots[1 - copy]), "begin colliding current update");
        require(pump(transaction, sd::IndexedCommitStep::Working) == sd::IndexedCommitStep::Verified && transaction.committed(root), "commit current update");
        copy = 1 - copy;
    };
    current_commit(false);
    const auto old_root = root;
    current_commit(false);
    sd::SdIndexGet old_read(volume);
    require(old_read.begin(old_root, 2, {a.data(), a.size()}, frame.data(), frame.size()) &&
                pump(old_read, sd::IndexGetStep::Working) == sd::IndexGetStep::Ready,
            "replacement lost pinned parent generation");
    for (unsigned i = 0; i < 40; ++i) current_commit(false);
    sd::SdIndexHeadReader head_reader(volume, root.slot, root.epoch, root.sequence);
    require(head_reader.beginBucket(2, bucket) && pump(head_reader, sd::IndexHeadReadStep::Working) == sd::IndexHeadReadStep::Ready &&
                head_reader.selected(current) && current.current_only && current.length == 2 * gc::kIndexEntrySize,
            "updates accumulated obsolete current references");
    test::profile_reads = true;
    test::read_bytes_by_path.clear();
    sd::SdIndexScan current_scan(volume);
    require(current_scan.begin(root, 2, frame.data(), frame.size()), "scan committed current keys");
    std::set<std::array<uint8_t, 32>> live;
    for (;;)
    {
        const auto status = pump(current_scan, sd::IndexScanStep::Working);
        if (status == sd::IndexScanStep::End) break;
        gc::MutationView row;
        require(status == sd::IndexScanStep::Item && current_scan.item(row) && row.key.size == a.size(), "current key scan failed");
        std::array<uint8_t, 32> id;
        std::memcpy(id.data(), row.key.data, id.size());
        require(live.insert(id).second && current_scan.advance(), "duplicate committed current key");
    }
    require(live.size() == 2 && live.count(a) && live.count(b), "colliding transaction dropped a key");
    require(sd::indexShardDataPath(root.slot, current, current_path, sizeof(current_path)) &&
                test::read_bytes_by_path[current_path] == 2 * gc::kIndexEntrySize,
            "committed current scan repeated references");
    for (const auto& read : test::read_bytes_by_path)
        if (read.first.find("/02/") != std::string::npos && read.first.find(".gci.c") != std::string::npos)
            require(read.first == current_path, "committed scan visited older generations");
    test::profile_reads = false;
    // Reconstruct a legacy append shard with all prior versions, then update
    // just A. Conversion must retain B's latest reference exactly once.
    char legacy_path[80], legacy_head_path[80];
    require(sd::indexShardPathForBucket(root.slot, 2, bucket, legacy_path, sizeof(legacy_path)), "legacy conversion path");
    std::vector<uint8_t> legacy;
    const auto prefix = std::string(legacy_path) + ".c";
    for (const auto& file : test::files)
        if (file.first.compare(0, prefix.size(), prefix) == 0) legacy.insert(legacy.end(), file.second.begin(), file.second.end());
    test::files[legacy_path] = legacy;
    auto legacy_head = current;
    legacy_head.current_only = false;
    legacy_head.length = legacy.size();
    gc::IndexShardHeadBytes legacy_header;
    require(gc::encodeIndexShardHead(volume, legacy_head, legacy_header), "encode conversion baseline");
    for (unsigned head_copy = 0; head_copy < 2; ++head_copy)
    {
        require(sd::indexShardHeadPathForBucket(root.slot, 2, bucket, head_copy, legacy_head_path, sizeof(legacy_head_path)), "conversion head path");
        test::files[legacy_head_path] = {legacy_header.begin(), legacy_header.end()};
    }
    const gc::MutationView one_change{2, {a.data(), a.size()}, {a_value.data(), a_size}, false};
    sd::SdIndexedCommit conversion(volume);
    require(conversion.begin(root, copy, &one_change, 1, frame.data(), frame.size(), roots[1 - copy]) &&
                pump(conversion, sd::IndexedCommitStep::Working) == sd::IndexedCommitStep::Verified && conversion.committed(root),
            "convert legacy head on update");
    copy = 1 - copy;
    sd::SdIndexHeadReader converted_head(volume, root.slot, root.epoch, root.sequence);
    require(converted_head.beginBucket(2, bucket) && pump(converted_head, sd::IndexHeadReadStep::Working) == sd::IndexHeadReadStep::Ready &&
                converted_head.selected(current) && current.current_only && current.length == 2 * gc::kIndexEntrySize,
            "legacy conversion retained obsolete references or lost colliding key");
    current_commit(true);
    sd::SdIndexGet erased_read(volume), kept_read(volume);
    require(erased_read.begin(root, 2, {a.data(), a.size()}, frame.data(), frame.size()) &&
                pump(erased_read, sd::IndexGetStep::Working) == sd::IndexGetStep::NotFound,
            "current erase resurrected key");
    require(kept_read.begin(root, 2, {b.data(), b.size()}, frame.data(), frame.size()) &&
                pump(kept_read, sd::IndexGetStep::Working) == sd::IndexGetStep::Ready,
            "current erase lost colliding key");
    current_commit(true, true);
    sd::SdIndexHeadReader empty_head(volume, root.slot, root.epoch, root.sequence);
    require(empty_head.beginBucket(2, bucket) && pump(empty_head, sd::IndexHeadReadStep::Working) == sd::IndexHeadReadStep::Ready &&
                empty_head.selected(current) && current.current_only && current.length == 0,
            "last deletion did not publish empty generation");
    sd::SdIndexScan empty_committed(volume);
    require(empty_committed.begin(root, 2, frame.data(), frame.size()) &&
                pump(empty_committed, sd::IndexScanStep::Working) == sd::IndexScanStep::End,
            "committed empty generation scanned history");
    // A failed read-back must not publish the new generation or disturb either
    // committed shard head. The journal suffix remains recovery evidence.
    // Objects and drafts retain one current reference despite repeated updates.
    for (uint8_t table : {uint8_t(1), uint8_t(4)})
    {
        const geocaching::ByteView key{a.data(), table == 4 ? size_t(16) : a.size()};
        std::array<uint8_t, 256> value;
        size_t size = 0;
        for (unsigned update = 0; update < 20; ++update)
        {
            if (table == 4)
            {
                gc::DraftView draft;
                draft.name = "Current local draft";
                draft.generation = update + 1;
                require(gc::encodeDraft(key, draft, value.data(), value.size(), size), "encode current draft");
            }
            else
            {
                gc::ObjectRefView object;
                object.cache_id = {b.data(), b.size()};
                object.revision = 1;
                object.created_at = update;
                require(gc::encodeObjectRef(key, object, value.data(), value.size(), size), "encode current object");
            }
            gc::MutationView change{table, key, {value.data(), size}, false};
            sd::SdIndexedCommit transaction(volume);
            require(transaction.begin(root, copy, &change, 1, frame.data(), frame.size(), roots[1 - copy]) &&
                        pump(transaction, sd::IndexedCommitStep::Working) == sd::IndexedCommitStep::Verified && transaction.committed(root),
                    "commit current local reference");
            copy = 1 - copy;
        }
        sd::SdIndexHeadReader reader(volume, root.slot, root.epoch, root.sequence);
        gc::IndexShardHead head;
        require(reader.begin(table, key) && pump(reader, sd::IndexHeadReadStep::Working) == sd::IndexHeadReadStep::Ready &&
                    reader.selected(head) && head.current_only && head.length == gc::kIndexEntrySize,
                "local updates retained historical references");
        test::profile_reads = true;
        test::read_bytes_by_path.clear();
        sd::SdIndexGet get(volume);
        require(get.begin(root, table, key, frame.data(), frame.size()) && pump(get, sd::IndexGetStep::Working) == sd::IndexGetStep::Ready &&
                    get.value().size == size && !std::memcmp(get.value().data, value.data(), size),
                "current local lookup failed");
        char data_path[96];
        require(sd::indexShardDataPath(root.slot, head, data_path, sizeof(data_path)) && test::read_bytes_by_path[data_path] == gc::kIndexEntrySize, "current local lookup repeated history");
        test::profile_reads = false;
    }
    char first_head[80], second_head[80];
    require(sd::indexShardHeadPathForBucket(root.slot, 2, bucket, 0, first_head, sizeof(first_head)) &&
                sd::indexShardHeadPathForBucket(root.slot, 2, bucket, 1, second_head, sizeof(second_head)),
            "failure head paths");
    const auto first_before = test::files.at(first_head), second_before = test::files.at(second_head);
    auto pending_head = current;
    pending_head.sequence = root.sequence + 1;
    require(sd::indexShardDataPath(root.slot, pending_head, current_path, sizeof(current_path)), "failure generation path");
    test::fail_read_path = current_path;
    const gc::MutationView failed_changes[] = {{2, {a.data(), a.size()}, {a_value.data(), a_size}, false},
                                               {2, {b.data(), b.size()}, {b_value.data(), b_size}, false}};
    sd::SdIndexedCommit failed_commit(volume);
    require(failed_commit.begin(root, copy, failed_changes, 2, frame.data(), frame.size(), roots[1 - copy]) &&
                pump(failed_commit, sd::IndexedCommitStep::Working) == sd::IndexedCommitStep::RecoveryRequired,
            "failed replacement verification was accepted");
    require(test::files.at(first_head) == first_before && test::files.at(second_head) == second_before,
            "failed replacement modified committed shard heads");
    sd::SdIndexGet after_failure(volume);
    require(after_failure.begin(root, 2, {a.data(), a.size()}, frame.data(), frame.size()) &&
                pump(after_failure, sd::IndexGetStep::Working) == sd::IndexGetStep::NotFound,
            "failed replacement changed parent view");
    require(!test::open_files && !test::open_dirs, "current writer leaked handles");
    std::printf("Colliding keys, 60 obsolete versions, tombstone and old CRC fault: fallback=%u optimized=%u steps\n", fallback, optimized);
}
