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
        return steps;
    };
    const auto fallback = scan(144, false);
    const auto optimized = scan(frame.size(), false);
    require(optimized * 2 < fallback, "adjacent obsolete references still repeat full lookups");
    char path[80];
    require(sd::indexShardPathForBucket(root.slot, 11, bucket, path, sizeof(path)), "shard path");
    test::files.at(path)[148] ^= 1;
    scan(frame.size(), true);
    scan(144, true);
    require(!test::open_files && !test::open_dirs, "scan leaked handles");
    std::printf("Colliding keys, 60 obsolete versions, tombstone and old CRC fault: fallback=%u optimized=%u steps\n", fallback, optimized);
}
