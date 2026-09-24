#pragma once
#include <array>
#include <cstdint>

namespace geocaching::storage
{
enum class CheckpointCandidateState : uint8_t { Missing, Unavailable, Invalid, Verified };
struct CheckpointCandidate
{
    // Verified may be set only after full file framing, tail and EOF checks.
    CheckpointCandidateState state = CheckpointCandidateState::Unavailable;
    uint64_t sequence = 0;
    std::array<uint8_t, 32> digest{};
};
enum class CheckpointChoice : uint8_t { NoCheckpoint, SlotA, SlotB, RetryLater, Corrupt };

inline CheckpointChoice selectCheckpoint(const CheckpointCandidate& a, const CheckpointCandidate& b)
{
    using State = CheckpointCandidateState;
    // An unreadable slot could contain a newer committed checkpoint. It is not
    // equivalent to a missing or fully inspected invalid candidate.
    if (a.state == State::Unavailable || b.state == State::Unavailable) return CheckpointChoice::RetryLater;
    if (a.state == State::Verified && b.state == State::Verified)
    {
        if (a.sequence == b.sequence)
            return a.digest == b.digest ? CheckpointChoice::SlotA : CheckpointChoice::Corrupt;
        return a.sequence > b.sequence ? CheckpointChoice::SlotA : CheckpointChoice::SlotB;
    }
    if (a.state == State::Verified) return CheckpointChoice::SlotA;
    if (b.state == State::Verified) return CheckpointChoice::SlotB;
    if (a.state == State::Missing && b.state == State::Missing) return CheckpointChoice::NoCheckpoint;
    return CheckpointChoice::Corrupt;
}
} // namespace geocaching::storage
