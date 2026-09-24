#include "geocaching/storage/checkpoint_selection.h"
int main()
{
    using namespace geocaching::storage;
    CheckpointCandidate a, b;
    if (selectCheckpoint(a, b) != CheckpointChoice::RetryLater) return 1;
    a.state = b.state = CheckpointCandidateState::Missing;
    if (selectCheckpoint(a, b) != CheckpointChoice::NoCheckpoint) return 2;
    a.state = CheckpointCandidateState::Invalid;
    if (selectCheckpoint(a, b) != CheckpointChoice::Corrupt) return 3;
    b.state = CheckpointCandidateState::Verified;
    b.sequence = 4;
    if (selectCheckpoint(a, b) != CheckpointChoice::SlotB) return 4;
    a.state = CheckpointCandidateState::Verified;
    a.sequence = 5;
    if (selectCheckpoint(a, b) != CheckpointChoice::SlotA) return 5;
    a.sequence = 4;
    if (selectCheckpoint(a, b) != CheckpointChoice::SlotA) return 6;
    a.digest[0] = 1;
    if (selectCheckpoint(a, b) != CheckpointChoice::Corrupt) return 7;
    a.state = CheckpointCandidateState::Unavailable;
    if (selectCheckpoint(a, b) != CheckpointChoice::RetryLater) return 8;
    return 0;
}
