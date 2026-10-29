#include "chat/infra/lxmf/lxmf_propagation_sync.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <memory>

namespace state = chat::lxmf::runtime;
using Stage = state::PropagationSyncStage;
int main()
{
    auto runtime = std::make_unique<state::PropagationRuntime>();
    std::array<uint8_t, 32> first{}, second{}, message{};
    first[0] = 1;
    second[0] = 2;
    message[0] = 3;
    std::array<uint8_t, 16> request_id{};
    request_id[0] = 4;
    assert(state::propagationSyncDue(*runtime, 100, 60));
    assert(state::startPropagationSyncIfDue(*runtime, 100, 10, 60));
    assert(runtime->sync_stage == Stage::NeedList);
    assert(!state::startPropagationSyncIfDue(*runtime, 100, 10, 60));
    state::markPropagationSyncRequestSent(*runtime, request_id.data(), Stage::Listing);
    state::LinkPendingRequest request;
    request.request_id.assign(request_id.begin(), request_id.end());
    assert(state::propagationSyncRequestMatches(*runtime, request));
    request.request_id[0] ^= 1;
    assert(!state::propagationSyncRequestMatches(*runtime, request));
    state::rememberPropagationTransient(*runtime, first.data(), true, 100, 32);
    state::PropagationIdList remote;
    remote.emplace_back(first.begin(), first.end());
    remote.emplace_back(second.begin(), second.end());
    remote.emplace_back(1, 0); // Malformed IDs cannot enter wants/haves.
    state::notePropagationListingResult(*runtime, remote, 1);
    assert(runtime->sync_stage == Stage::NeedMessages && runtime->sync_haves.size() == 1 && runtime->sync_wants.size() == 1);
    assert(state::awaitPropagationDeliveryCommit(*runtime, second.data(), message.data(), 2));
    state::notePropagationDownloadResult(*runtime, false, 20);
    assert(runtime->sync_stage == Stage::AwaitingPersistence);
    assert(!state::pollPropagationPersistence(*runtime, 21, 1000));
    // A rejected local store must never add the downloaded transient to the
    // remote deletion acknowledgement list.
    assert(state::notePropagationDeliveryCommit(*runtime, message.data(), false, 100, 32));
    assert(runtime->sync_stage == Stage::Failed && runtime->sync_haves.size() == 1);
    assert(!state::hasSeenPropagationTransient(*runtime, second.data()));
    state::finishPropagationSyncFailed(*runtime, 100);
    assert(!state::propagationSyncDue(*runtime, 101, 60));
    assert(state::startPropagationSyncIfDue(*runtime, 160, 30, 60));
    state::notePropagationListingResult(*runtime, remote, 1);
    assert(state::awaitPropagationDeliveryCommit(*runtime, second.data(), message.data(), 2));
    state::notePropagationDownloadResult(*runtime, false, 31);
    assert(state::notePropagationDeliveryCommit(*runtime, message.data(), true, 160, 32));
    assert(runtime->sync_stage == Stage::NeedAcknowledge && runtime->sync_haves.size() == 2);
    bool delivered = false;
    assert(state::hasSeenPropagationTransient(*runtime, second.data(), &delivered) && delivered);
    state::markPropagationAcknowledged(*runtime);
    assert(runtime->sync_stage == Stage::Complete);
    state::finishPropagationSyncComplete(*runtime, 160);
    assert(runtime->sync_stage == Stage::Idle && runtime->sync_haves.empty() && runtime->pending_deliveries.empty());
    assert(!state::propagationSyncDue(*runtime, 161, 60));
    assert(state::propagationSyncDue(*runtime, 159, 60)); // Clock correction must not strand sync.
    assert(state::startPropagationSyncIfDue(*runtime, 220, 0xfffffff0U, 60));
    assert(state::awaitPropagationDeliveryCommit(*runtime, first.data(), message.data(), 2));
    state::notePropagationDownloadResult(*runtime, false, 0xfffffff0U);
    assert(!state::pollPropagationPersistence(*runtime, 8, 32));
    assert(state::pollPropagationPersistence(*runtime, 32, 32) && runtime->sync_stage == Stage::Failed);
    assert(runtime->sync_haves.empty());
    state::finishPropagationSyncFailed(*runtime, 220);
    assert(runtime->sync_stage == Stage::Idle);
    std::puts("Shared propagation sync: request binding, bounded listing, durable acceptance, rejected/expired persistence, clock correction and rollover passed");
}
