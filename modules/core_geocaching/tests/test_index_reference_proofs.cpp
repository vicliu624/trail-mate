#include "geocaching/protocol/query_request.h"
#include "geocaching/storage/queued_request.h"
#include "platform/esp/arduino_common/geocaching/indexed_dispatch_store.h"
#include "platform/esp/arduino_common/geocaching/sd_index_initialize.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_stop_task.h"
#include "runtime_environment.h"
#include <cstdio>

namespace gc = geocaching::storage;
namespace sd = platform::esp::arduino_common::geocaching;
namespace test = runtime_test;
static void require(bool ok, const char* message)
{
    if (!ok)
    {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
template <class Job, class State>
static State pump(Job& job, State working)
{
    auto state = working;
    for (unsigned i = 0; i < 100000 && state == working; ++i)
    {
        test::io_bytes = 0;
        state = job.step();
        require(test::io_bytes <= 512, "reference proof exceeded transfer budget");
    }
    return state;
}
int main()
{
    gc::VolumeInstance volume{}, confirmed;
    require(sd::createNewSdVolume(volume, confirmed) == sd::SdVolumeResult::Ready, "create volume");
    std::array<gc::IndexRootBytes, 2> roots;
    gc::IndexRootView root;
    unsigned copy = 0;
    std::array<uint8_t, 8192> frame{}, encoded{};
    {
        sd::SdIndexInitialize init(volume);
        require(init.begin(roots[0]) && pump(init, sd::IndexRootWriteStep::Working) == sd::IndexRootWriteStep::Verified, "initialize roots");
        require(gc::decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, root), "decode root");
    }
    std::array<std::array<uint8_t, 16>, 7> task_ids{};
    std::array<std::array<uint8_t, 48>, 7> request_keys{};
    // More tasks than cache slots forces eviction; every task remains checked.
    for (unsigned n = 0; n < task_ids.size(); ++n)
    {
        geocaching::RequestId id;
        id.bytes.fill(static_cast<uint8_t>(n + 1));
        task_ids[n].fill(static_cast<uint8_t>(n + 11));
        std::array<uint8_t, 64> request{};
        std::array<uint8_t, 512> outgoing{};
        gc::QueuedRequestWorkspace scratch(outgoing.data(), outgoing.size());
        size_t request_size = 0, size = 0;
        require(geocaching::protocol::encodeCapabilitiesRequest(id, request.data(), request.size(), request_size), "encode request");
        require(gc::encodeNewRequestTask(root.sequence, {}, {}, id, task_ids[n], 3, {request.data(), request_size}, {}, scratch, encoded.data(), encoded.size(), size), "encode task");
        gc::MutationView rows[2];
        gc::TransactionView tx;
        require(gc::decodeTransaction({encoded.data(), size}, root.sequence, rows, 2, tx), "decode task transaction");
        std::copy(rows[0].key.data, rows[0].key.data + 48, request_keys[n].begin());
        sd::SdIndexedCommit commit(volume);
        require(commit.begin(root, copy, rows, 2, frame.data(), frame.size(), roots[1 - copy]), "begin task commit");
        require(pump(commit, sd::IndexedCommitStep::Working) == sd::IndexedCommitStep::Verified && commit.committed(root), "commit task");
        copy = 1 - copy;
    }
    const auto verify = [&](const gc::MutationView* rows, size_t count, sd::IndexScanStep expected)
    {
        sd::SdIndexReferences references(volume);
        require(references.begin(root, frame.data(), frame.size(), rows, count), "begin reference proof");
        require(pump(references, sd::IndexScanStep::Working) == expected, "reference proof accepted a dangling or mismatched relation");
        require(!test::open_files && !test::open_dirs, "reference proof leaked handles");
    };
    verify(nullptr, 0, sd::IndexScanStep::End);
    for (unsigned n = 0; n < task_ids.size(); ++n)
    {
        gc::MutationView removed[] = {{5, {request_keys[n].data(), 48}, {}, true}, {10, {task_ids[n].data(), 16}, {}, true}};
        verify(removed, 1, sd::IndexScanStep::Invalid);
        verify(removed + 1, 1, sd::IndexScanStep::Invalid);
        verify(removed, 2, sd::IndexScanStep::End);
        // A task with one valid and one wrong child must not pass because one
        // relationship was already proven in the outgoing scan.
        gc::TaskView task;
        task.kind = 3;
        task.request_count = 2;
        task.requests[0] = {request_keys[n].data(), 48};
        task.requests[1] = {request_keys[(n + 1) % task_ids.size()].data(), 48};
        std::array<uint8_t, 256> value{};
        size_t size = 0;
        require(gc::encodeTask({task_ids[n].data(), 16}, task, value.data(), value.size(), size), "encode mismatched task");
        gc::MutationView changed{10, {task_ids[n].data(), 16}, {value.data(), size}, false};
        verify(&changed, 1, sd::IndexScanStep::Invalid);
    }
    // Reuse the real pending request fixture to compare the send payload lease
    // against small and aliased workspace fallbacks.
    sd::IndexWorkspaceOwner owner;
    gc::QueuedRequestWorkspace workspace(encoded.data(), encoded.size());
    {
        sd::IndexedDispatchStore store(volume, root, copy, roots[0], roots[1], owner, workspace, frame.data(), frame.size());
        std::array<uint8_t, 16> attempt{};
        attempt.fill(0x71);
        require(store.beginAttempt({}, {request_keys[0].data(), 48}, attempt, {}) == sd::JournalWriteResult::InProgress, "begin send lease attempt");
        auto state = sd::JournalWriteResult::InProgress;
        for (unsigned i = 0; i < 100000 && state == sd::JournalWriteResult::InProgress; ++i) state = store.stepCommit();
        require(state == sd::JournalWriteResult::Verified, "persist send lease attempt");
    }
    geocaching::RequestId expected_id;
    expected_id.bytes.fill(1);
    std::array<uint8_t, 64> expected{};
    size_t expected_size = 0;
    require(geocaching::protocol::encodeCapabilitiesRequest(expected_id, expected.data(), expected.size(), expected_size), "encode expected send");
    const auto readSend = [&](unsigned mode, bool stopped)
    {
        workspace.outgoing = mode == 2 ? frame.data() : encoded.data();
        workspace.outgoing_capacity = mode == 1 ? 1 : encoded.size();
        sd::IndexedDispatchStore store(volume, root, copy, roots[0], roots[1], owner, workspace, frame.data(), frame.size());
        test::profile_reads = true;
        test::read_bytes_by_path.clear();
        sd::DispatchSendView send;
        auto state = sd::DispatchReadResult::Pending;
        for (unsigned i = 0; i < 100000 && state == sd::DispatchReadResult::Pending; ++i)
        {
            test::io_bytes = 0;
            state = store.readForSend({request_keys[0].data(), 48}, send);
            require(test::io_bytes <= 512, "send lease exceeded transfer budget");
        }
        require(state == sd::DispatchReadResult::Ready && send.stopped == stopped, "send lease lost stop or readiness state");
        if (!stopped)
        {
            require(send.request.size == expected_size && !std::memcmp(send.request.data, expected.data(), expected_size), "task lookup overwrote send body");
            if (!mode) require(send.request.data == encoded.data(), "send did not reuse encoding workspace");
        }
        else require(!send.request.size, "stopped task exposed a send body");
        require(!owner.holder() && !test::open_files && !test::open_dirs, "send lease retained owner or handles");
        uint64_t bytes = 0;
        for (const auto& file : test::read_bytes_by_path) bytes += file.second;
        test::profile_reads = false;
        return bytes;
    };
    const auto reused = readSend(0, false);
    const auto small = readSend(1, false);
    const auto aliased = readSend(2, false);
    require(reused < small && small == aliased, "send lease did not remove a request reload");
    {
        sd::SdIndexedStopTask stop(volume);
        require(stop.begin(root, copy, {task_ids[0].data(), 16}, false, frame.data(), frame.size(), roots[1 - copy]), "begin stopped task");
        require(pump(stop, sd::IndexedCommitStep::Working) == sd::IndexedCommitStep::Verified && stop.committed(root), "persist stopped task");
        copy = 1 - copy;
    }
    readSend(0, true);
    std::printf("Send preparation read bytes: reused=%llu fallback=%llu\n", static_cast<unsigned long long>(reused), static_cast<unsigned long long>(small));
    std::printf("Reference validator size: %u bytes; eviction and overlay mismatch checks passed\n", static_cast<unsigned>(sizeof(sd::SdIndexReferences)));
    return 0;
}
