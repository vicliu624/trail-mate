#include "platform/esp/arduino_common/geocaching/sd_checkpoint_build.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_reader.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_rotation.h"
#include "platform/esp/arduino_common/geocaching/sd_index_initialize.h"
#include "platform/esp/arduino_common/geocaching/sd_index_repair.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_recovery.h"
#include "runtime_environment.h"
#include <cstdio>

namespace gc = geocaching::storage;
namespace sd = platform::esp::arduino_common::geocaching;
namespace test = runtime_test;
using Digest = platform::esp::common::meshcore_runtime::Sha256Digest;
static int allocation_failure_countdown = -1;
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    if (allocation_failure_countdown == 0) return nullptr;
    if (allocation_failure_countdown > 0) --allocation_failure_countdown;
    try
    {
        return ::operator new(size);
    }
    catch (...)
    {
        return nullptr;
    }
}
void require(bool ok, const char* message)
{
    if (!ok)
    {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
template <class Job, class State>
State pump(Job& job, State working)
{
    auto status = working;
    for (unsigned i = 0; i < 1000000 && status == working; ++i)
    {
        test::io_bytes = 0;
        status = job.step();
        require(test::io_bytes <= 512, "checkpoint work exceeded public transfer budget");
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
    std::array<uint8_t, 8192> frame{}, scratch{};
    {
        sd::SdIndexInitialize initialize(volume);
        require(initialize.begin(roots[0]), "initialize index");
        require(pump(initialize, sd::IndexRootWriteStep::Working) == sd::IndexRootWriteStep::Verified, "publish initial index");
        require(gc::decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, root), "read initial root");
    }
    const auto commit = [&](unsigned id, bool erase, bool edited)
    {
        std::array<uint8_t, 32> key{};
        key[0] = static_cast<uint8_t>(id);
        std::vector<uint8_t> value{0x92, 0x90, 0x90};
        if (edited)
        {
            value[2] = 0x91;
            value.insert(value.end(), {0xc4, 32});
            value.insert(value.end(), 32, 0x73);
        }
        gc::MutationView row{11, {key.data(), key.size()}, erase ? geocaching::ByteView{} : geocaching::ByteView{value.data(), value.size()}, erase};
        sd::SdIndexedCommit transaction(volume);
        require(transaction.begin(root, copy, &row, 1, frame.data(), frame.size(), roots[1 - copy]), "begin transaction");
        require(pump(transaction, sd::IndexedCommitStep::Working) == sd::IndexedCommitStep::Verified, "commit transaction");
        require(transaction.committed(root), "read committed root");
        copy = 1 - copy;
    };
    // More than two fixed runs, adversarial insertion order, an obsolete value
    // and an erased key. No complete ledger is given to the implementation.
    for (unsigned i = 70; i > 0; --i) commit(i, false, false);
    commit(10, false, true);
    commit(5, true, false);
    const auto authoritative = test::files;
    // Fail each allocation site independently: sorter, scanner, writer and
    // value reader. Resource pressure must never look like corrupt storage.
    for (const int fail_after : {0, 1, 14, 15})
    {
        sd::SdCheckpointBuild<Digest> build(volume);
        allocation_failure_countdown = fail_after;
        build.begin(root, frame.data(), frame.size(), scratch.data(), scratch.size());
        const auto state = pump(build, sd::CheckpointBuildStep::Working);
        allocation_failure_countdown = -1;
        require(state == sd::CheckpointBuildStep::OutOfMemory, "allocation failure misclassified as storage corruption");
        require(!test::open_files && !test::open_dirs, "allocation failure leaked storage handles");
        for (const auto& original : authoritative) require(test::files.at(original.first) == original.second, "allocation failure changed authoritative input");
    }
    {
        sd::SdCheckpointBuild<Digest> build(volume);
        require(!build.begin(root, frame.data(), frame.size(), frame.data(), frame.size()), "overlapping sort workspace accepted");
    }
    {
        sd::SdCheckpointBuild<Digest> build(volume);
        require(build.begin(root, frame.data(), frame.size(), scratch.data(), scratch.size()), "begin checkpoint build");
        auto state = sd::CheckpointBuildStep::Working;
        for (unsigned i = 0; i < 1000000 && state == sd::CheckpointBuildStep::Working; ++i)
        {
            if (i % 37 == 0)
            {
                test::external_owner = true;
                test::io_bytes = 0;
                require(build.step() == sd::CheckpointBuildStep::Busy && !test::io_bytes, "external storage owner was bypassed");
                test::external_owner = false;
            }
            test::io_bytes = 0;
            state = build.step();
            require(test::io_bytes <= 512, "checkpoint build exceeded 512 bytes");
        }
        require(state == sd::CheckpointBuildStep::Complete, "checkpoint build did not complete");
    }
    for (const auto& original : authoritative) require(test::files.at(original.first) == original.second, "checkpoint build changed authoritative input");
    require(!test::files.count("/trailmate/geocaching/.state/checkpoint/a.gcs") &&
                !test::files.count("/trailmate/geocaching/.state/checkpoint/b.gcs"),
            "builder published a checkpoint slot");
    // Independent reader verifies the complete canonical checkpoint, digest,
    // key order, row count and latest values. Copying here is test setup only.
    test::files["/trailmate/geocaching/.state/checkpoint/a.gcs"] = test::files.at(sd::SdCheckpointWriter<Digest>::path);
    {
        Digest digest;
        sd::SdCheckpointReader<Digest> reader(digest);
        require(reader.open('a'), "open built checkpoint");
        unsigned count = 0, last = 0;
        bool edited = false;
        auto state = sd::CheckpointReadStep::Reading;
        for (unsigned i = 0; i < 100000 && state == sd::CheckpointReadStep::Reading; ++i)
        {
            gc::CheckpointPageCursor cursor;
            test::io_bytes = 0;
            state = reader.stepCursor(frame.data(), frame.size(), cursor);
            require(test::io_bytes <= 512, "reader exceeded transfer budget");
            gc::MutationView row;
            for (size_t j = 0; j < cursor.count(); ++j)
            {
                require(cursor.next(row), "checkpoint cursor failed");
                const unsigned id = row.key.data[0];
                require(row.table == 11 && id > last && id != 5, "checkpoint is unordered or resurrected an erased row");
                last = id;
                ++count;
                if (id == 10)
                {
                    require(row.value.size == 37, "obsolete row selected");
                    edited = true;
                }
            }
        }
        require(state == sd::CheckpointReadStep::Verified && reader.sequence() == root.sequence && count == 69 && edited,
                "checkpoint failed independent verification");
    }
    {
        const auto files = test::files;
        const auto directories = test::directories;
        const auto saved_roots = roots;
        const auto saved_copy = copy;
        for (unsigned cut : {0u, 1u, 10u, 50u, 500u, 2000u, 5000u, 10000u, 20000u})
        {
            test::files = files;
            test::directories = directories;
            roots = saved_roots;
            copy = saved_copy;
            require(gc::decodeIndexRoot({roots[copy].data(), roots[copy].size()}, volume, root), "restore yield test root");
            sd::SdCheckpointRotation<Digest> rotation(volume);
            require(rotation.begin(roots[0], roots[1], copy, frame.data(), frame.size(), scratch.data(), scratch.size()), "begin yield test");
            auto state = sd::CheckpointRotationStep::Working;
            for (unsigned i = 0; i < cut && state == sd::CheckpointRotationStep::Working; ++i) state = rotation.step();
            if (state == sd::CheckpointRotationStep::Working)
            {
                unsigned waited = 0;
                while (!rotation.yieldToForeground() && waited++ < 100) state = rotation.step();
                require(waited < 100 && rotation.step() == sd::CheckpointRotationStep::Yielded, "maintenance could not yield within bounded root publication");
            }
            else require(state == sd::CheckpointRotationStep::Complete, "rotation failed before yield cut");
            require(rotation.selected(root, copy), "yield did not return usable parent root");
            require(!test::open_files && !test::open_dirs, "yield leaked handles");
            commit(10, false, true);
            const auto sequence = root.sequence;
            std::array<gc::IndexRootBytes, 2> recovered;
            gc::MutationView mutation;
            sd::SdIndexedRecovery<Digest> recovery(volume, recovered[0], recovered[1], frame.data(), frame.size(), scratch.data(), scratch.size(), &mutation, 1);
            require(pump(recovery, sd::IndexedRecoveryStep::Working) == sd::IndexedRecoveryStep::Restored, "yield plus foreground write failed recovery");
            gc::IndexRootView selected;
            unsigned selected_copy;
            require(recovery.selected(selected, selected_copy) && selected.sequence == sequence, "yield lost foreground write");
        }
        test::files = files;
        test::directories = directories;
        roots = saved_roots;
        copy = saved_copy;
        require(gc::decodeIndexRoot({roots[copy].data(), roots[copy].size()}, volume, root), "restore rotation baseline");
    }
    {
        const auto files = test::files;
        const auto directories = test::directories;
        const auto metadata = roots;
        // A recreated coordinator represents another session. Both must read
        // the durable checkpoint and skip maintenance without modifying disk.
        for (unsigned restart = 0; restart < 2; ++restart)
        {
            sd::SdCheckpointRotation<Digest> rotation(volume);
            require(rotation.begin(roots[0], roots[1], copy, frame.data(), frame.size(), scratch.data(), scratch.size(), 256), "begin interval test");
            require(pump(rotation, sd::CheckpointRotationStep::Working) == sd::CheckpointRotationStep::Complete, "recent checkpoint was not accepted");
            require(test::files == files && test::directories == directories && roots == metadata, "recent checkpoint unnecessarily rewrote storage");
        }
    }
    {
        sd::SdCheckpointRotation<Digest> interrupted(volume);
        require(interrupted.begin(roots[0], roots[1], copy, frame.data(), frame.size(), scratch.data(), scratch.size()), "begin external ownership test");
        test::external_owner = true;
        require(interrupted.step() == sd::CheckpointRotationStep::Busy, "rotation ignored external owner");
        require(!interrupted.yieldToForeground(), "external interruption returned unverified roots to foreground");
        test::external_owner = false;
        require(interrupted.step() == sd::CheckpointRotationStep::Invalid, "external interruption reused checkpoint proof");
    }
    for (unsigned cycle = 0; cycle < 3; ++cycle)
    {
        commit(10, false, cycle % 2 == 0);
        const auto sequence = root.sequence;
        const auto old_slot = root.slot;
        const auto before = test::files;
        sd::SdCheckpointRotation<Digest> rotation(volume);
        require(rotation.begin(roots[0], roots[1], copy, frame.data(), frame.size(), scratch.data(), scratch.size()), "begin checkpoint rotation");
        require(pump(rotation, sd::CheckpointRotationStep::Working) == sd::CheckpointRotationStep::Complete, "checkpoint rotation failed");
        require(rotation.selected(root, copy) && root.sequence == sequence && root.slot != old_slot, "rotation did not replace root");
        require(roots[0] == roots[1], "rotation did not mirror replacement metadata");
        require(rotation.retainedCheckpointSequence() == sequence - 1, "rotation lost fallback checkpoint");
        const std::string old_path = std::string("/trailmate/geocaching/.state/index/") + old_slot;
        require(!test::directories.count(old_path), "rotation retained obsolete index slot");
        for (const auto& file : before)
            if (file.first.find("/journal/") != std::string::npos)
            {
                const auto start = std::stoull(file.first.substr(file.first.find_last_of('/') + 1, 16), nullptr, 16);
                if (start > rotation.retainedCheckpointSequence())
                    require(test::files.at(file.first) == file.second, "rotation removed retained recovery suffix");
                else
                    require(!test::files.count(file.first), "rotation retained covered journal prefix");
            }
        std::array<gc::IndexRootBytes, 2> recovered;
        gc::MutationView mutation;
        sd::SdIndexedRecovery<Digest> recovery(volume, recovered[0], recovered[1], frame.data(), frame.size(), scratch.data(), scratch.size(), &mutation, 1);
        require(pump(recovery, sd::IndexedRecoveryStep::Working) == sd::IndexedRecoveryStep::Restored, "rotated index failed restart recovery");
        gc::IndexRootView selected;
        unsigned selected_copy = 0;
        require(recovery.selected(selected, selected_copy) && selected.sequence == sequence && selected.slot == root.slot, "restart chose wrong rotated state");
        const auto compacted = test::files;
        const auto compacted_directories = test::directories;
        // Damage the newly published checkpoint. Repair must use the older
        // checkpoint plus the retained suffix, including the latest mutation.
        for (const auto& file : compacted)
            if (file.first.find("/checkpoint/") != std::string::npos &&
                (!before.count(file.first) || before.at(file.first) != file.second))
                test::files[file.first].back() ^= 1;
        {
            sd::SdIndexRepair<Digest> repair(volume, recovered[0], recovered[1], frame.data(), frame.size(), scratch.data(), scratch.size(), &mutation, 1);
            require(pump(repair, sd::IndexedRecoveryStep::Working) == sd::IndexedRecoveryStep::Restored, "older checkpoint cannot recover after journal reclamation");
            require(repair.selected(selected, selected_copy) && selected.sequence == sequence, "fallback lost newest transaction");
        }
        test::files = compacted;
        test::directories = compacted_directories;
    }
    {
        const std::string target = "/trailmate/geocaching/.state/checkpoint/a.gcs";
        const auto old_target = test::files.at(target);
        const auto retained = test::files.at("/trailmate/geocaching/.state/checkpoint/b.gcs");
        const auto retained_sequence = root.sequence;
        commit(10, false, true);
        sd::SdCheckpointRotation<Digest> interrupted(volume);
        require(interrupted.begin(roots[0], roots[1], copy, frame.data(), frame.size(), scratch.data(), scratch.size()), "begin published interruption");
        unsigned steps = 0;
        while (steps++ < 100000 && (!test::files.count(target) || test::files.at(target) == old_target))
        {
            test::io_bytes = 0;
            require(interrupted.step() == sd::CheckpointRotationStep::Working, "rotation failed before publishing replacement");
            require(test::io_bytes <= 512, "published interruption exceeded transfer budget");
        }
        require(steps < 100000 && interrupted.yieldToForeground() && interrupted.selected(root, copy), "cannot yield after checkpoint publication");
        commit(10, false, false);
        const auto expected_sequence = root.sequence;
        sd::SdCheckpointRotation<Digest> retry(volume);
        require(retry.begin(roots[0], roots[1], copy, frame.data(), frame.size(), scratch.data(), scratch.size()), "begin published retry");
        require(pump(retry, sd::CheckpointRotationStep::Working) == sd::CheckpointRotationStep::Complete && retry.selected(root, copy), "published interruption permanently deferred maintenance");
        require(root.sequence == expected_sequence && retry.retainedCheckpointSequence() == retained_sequence &&
                    test::files.at("/trailmate/geocaching/.state/checkpoint/b.gcs") == retained,
                "retry overwrote referenced recovery baseline");
    }
    {
        const auto before = test::files;
        sd::SdSortedIndex sort(volume);
        require(sort.begin(root, frame.data(), frame.size(), scratch.data(), scratch.size()), "begin volume-change case");
        auto changed = volume;
        changed[0] = 1;
        const auto header = gc::encodeVolumeHeader(changed);
        test::files["/trailmate/geocaching/.state/format.bin"] = {header.begin(), header.end()};
        require(sort.step() == sd::SortedIndexStep::VolumeChanged, "changed volume was accepted");
        test::files = before;
    }
    require(!test::open_files && !test::open_dirs, "checkpoint build leaked SD handles");
    std::puts("Bounded external sort and staging checkpoint: 69 live rows, two merge passes, latest values, Busy and volume fencing passed");
}
