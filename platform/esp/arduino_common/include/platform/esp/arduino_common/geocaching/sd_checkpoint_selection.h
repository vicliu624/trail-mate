#pragma once
#include "geocaching/storage/checkpoint_selection.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_reader.h"
#include "platform/esp/arduino_common/geocaching/sd_volume.h"

namespace platform::esp::arduino_common::geocaching
{
enum class CheckpointSelectionStep : uint8_t
{
    Reading,
    Selected,
    NoCheckpoint,
    RetryLater,
    Corrupt,
    VolumeChanged
};

// One pass over both slots, one framed record per step. RetryLater requires a
// fresh pass (fresh digest contexts). Caller buffers are scratch only: selected
// contents are loaded in a subsequent verified pass before becoming live state.
template <class Digest>
class SdCheckpointSelection
{
  public:
    explicit SdCheckpointSelection(const ::geocaching::storage::VolumeInstance& volume)
        : volume_(volume), reader_a_(digest_a_), reader_b_(digest_b_) {}
    ::geocaching::storage::CheckpointChoice choice() const
    {
        return state_ == CheckpointSelectionStep::Selected || state_ == CheckpointSelectionStep::NoCheckpoint
                   ? ::geocaching::storage::selectCheckpoint(candidates_[0], candidates_[1])
                   : ::geocaching::storage::CheckpointChoice::RetryLater;
    }
    const ::geocaching::storage::CheckpointCandidate& candidate(bool slot_b) const { return candidates_[slot_b ? 1 : 0]; }

    CheckpointSelectionStep stepCursor(uint8_t* bytes, size_t capacity)
    {
        return step(bytes, capacity, nullptr, 0, true);
    }

    CheckpointSelectionStep step(uint8_t* bytes, size_t capacity,
                                 ::geocaching::storage::MutationView* entries, size_t entry_capacity, bool streaming = false)
    {
        if (state_ != CheckpointSelectionStep::Reading) return state_;
        if (!volume_checked_)
        {
            ::geocaching::storage::VolumeInstance current;
            const auto volume = inspectSdVolume(current);
            if (volume == SdVolumeResult::Missing || volume == SdVolumeResult::Unavailable || volume == SdVolumeResult::IoError)
                return state_ = CheckpointSelectionStep::RetryLater;
            if (volume != SdVolumeResult::Ready) return state_ = CheckpointSelectionStep::Corrupt;
            if (current != volume_) return state_ = CheckpointSelectionStep::VolumeChanged;
            volume_checked_ = true;
            return state_;
        }
        volume_checked_ = false;
        auto& reader = slot_ ? reader_b_ : reader_a_;
        auto& candidate = candidates_[slot_];
        using CandidateState = ::geocaching::storage::CheckpointCandidateState;
        if (!opened_)
        {
            if (probed_)
            {
                if (!reader.open(slot_ ? 'b' : 'a')) return state_ = CheckpointSelectionStep::RetryLater;
                opened_ = true;
                return state_;
            }
            uint8_t probe[24];
            const auto result = storage::sd_read_file(slot_ ? "/trailmate/geocaching/.state/checkpoint/b.gcs" : "/trailmate/geocaching/.state/checkpoint/a.gcs", probe, sizeof(probe));
            if (result.status == storage::SdFileReadStatus::Missing)
            {
                candidate.state = CandidateState::Missing;
                return advance();
            }
            if (result.status != storage::SdFileReadStatus::Ready && result.status != storage::SdFileReadStatus::Invalid)
                return state_ = CheckpointSelectionStep::RetryLater;
            probed_ = true;
            return state_;
        }
        size_t count = 0;
        ::geocaching::storage::CheckpointPageCursor cursor;
        switch (streaming ? reader.stepCursor(bytes, capacity, cursor) : reader.step(bytes, capacity, entries, entry_capacity, count))
        {
        case CheckpointReadStep::Reading:
            return state_;
        case CheckpointReadStep::WorkspaceTooSmall:
        case CheckpointReadStep::IoError:
            return state_ = CheckpointSelectionStep::RetryLater;
        case CheckpointReadStep::Invalid:
            candidate.state = CandidateState::Invalid;
            break;
        case CheckpointReadStep::Verified:
            candidate.state = CandidateState::Verified;
            candidate.sequence = reader.sequence();
            candidate.digest = reader.digest();
            break;
        }
        return advance();
    }

  private:
    CheckpointSelectionStep advance()
    {
        opened_ = false;
        probed_ = false;
        if (++slot_ < 2) return state_;
        using Choice = ::geocaching::storage::CheckpointChoice;
        switch (::geocaching::storage::selectCheckpoint(candidates_[0], candidates_[1]))
        {
        case Choice::SlotA:
        case Choice::SlotB:
            return state_ = CheckpointSelectionStep::Selected;
        case Choice::NoCheckpoint:
            return state_ = CheckpointSelectionStep::NoCheckpoint;
        case Choice::RetryLater:
            return state_ = CheckpointSelectionStep::RetryLater;
        case Choice::Corrupt:
            return state_ = CheckpointSelectionStep::Corrupt;
        }
        return state_ = CheckpointSelectionStep::Corrupt;
    }
    const ::geocaching::storage::VolumeInstance volume_;
    Digest digest_a_, digest_b_;
    SdCheckpointReader<Digest> reader_a_, reader_b_;
    std::array<::geocaching::storage::CheckpointCandidate, 2> candidates_{};
    unsigned slot_ = 0;
    bool opened_ = false;
    bool volume_checked_ = false, probed_ = false;
    CheckpointSelectionStep state_ = CheckpointSelectionStep::Reading;
};
} // namespace platform::esp::arduino_common::geocaching
