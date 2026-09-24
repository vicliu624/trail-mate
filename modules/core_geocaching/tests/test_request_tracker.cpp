#include "geocaching/usecase/request_tracker.h"
#include <cassert>

int main()
{
    using namespace geocaching;
    Destination local, remote, stranger;
    local.bytes.fill(1);
    remote.bytes.fill(2);
    stranger.bytes.fill(3);
    RequestId id;
    id.bytes.fill(4);
    std::array<std::uint8_t, 32> digest{};
    RequestTracker tracker;
    assert(tracker.begin(local, remote, id, Operation::Get, 8192));
    assert(!tracker.begin(local, remote, id, Operation::Get, 8192));
    assert(tracker.startAttempt());
    assert(!tracker.startAttempt());
    tracker.delivered();
    assert(tracker.phase() == RequestPhase::AwaitingResult);
    assert(tracker.classifyResult(stranger, local, id, Operation::Get, 100, digest) == ResultAdmission::WrongRequest);
    tracker.unconfirmed();
    assert(tracker.startAttempt());
    tracker.stopFurtherAttempts();
    tracker.unconfirmed();
    assert(!tracker.startAttempt());
    // Stopping retries does not discard a legitimate late committed result.
    assert(tracker.recordResult(remote, local, id, Operation::Get, 100, digest));
    tracker.delivered();
    tracker.unconfirmed();
    assert(tracker.phase() == RequestPhase::ResultRecorded);
    assert(tracker.classifyResult(remote, local, id, Operation::Get, 100, digest) == ResultAdmission::Duplicate);
    digest[0] = 1;
    assert(tracker.classifyResult(remote, local, id, Operation::Get, 100, digest) == ResultAdmission::ConflictingResult);
    assert(!tracker.recordResult(remote, local, id, Operation::Get, 100, digest));
    assert(!tracker.continueRequested());
}
