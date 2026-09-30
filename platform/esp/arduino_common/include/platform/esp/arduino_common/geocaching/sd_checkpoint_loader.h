#pragma once
#include "geocaching/storage/checkpoint_selection.h"
#include "geocaching/storage/logical_state.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_reader.h"
#include "platform/esp/arduino_common/geocaching/sd_volume.h"

namespace platform::esp::arduino_common::geocaching
{
enum class CheckpointLoadStep : uint8_t
{
    Loading,
    Applied,
    Invalid,
    IoError,
    StateRejected,
    VolumeChanged
};

// State and fresh digest outlive this one-shot loader. No tentative page is
// published. The selected candidate must be from a completed selection pass.
template <class Digest>
class SdCheckpointLoader
{
  public:
    SdCheckpointLoader(Digest& digest, ::geocaching::storage::LogicalState& state,
                       const ::geocaching::storage::VolumeInstance& volume, char slot,
                       const ::geocaching::storage::CheckpointCandidate& expected)
        : state_(state), volume_(volume), slot_(slot), expected_(expected), reader_(digest) {}
    SdCheckpointLoader(const SdCheckpointLoader&) = delete;
    SdCheckpointLoader& operator=(const SdCheckpointLoader&) = delete;
    ~SdCheckpointLoader()
    {
        if (staging_) state_.discardSnapshot();
    }

    template <class Validate>
    CheckpointLoadStep step(uint8_t* bytes, size_t capacity,
                            ::geocaching::storage::MutationView* entries, size_t entry_capacity, Validate validate)
    {
        if (result_ != CheckpointLoadStep::Loading) return result_;
        if (expected_.state != ::geocaching::storage::CheckpointCandidateState::Verified || (slot_ != 'a' && slot_ != 'b'))
            return fail(CheckpointLoadStep::Invalid);
        if (!volume_checked_)
        {
            ::geocaching::storage::VolumeInstance current;
            const auto volume = inspectSdVolume(current);
            if (volume != SdVolumeResult::Ready) return fail(volume == SdVolumeResult::Corrupt || volume == SdVolumeResult::Unsupported
                                                                 ? CheckpointLoadStep::Invalid
                                                                 : CheckpointLoadStep::IoError);
            if (current != volume_) return fail(CheckpointLoadStep::VolumeChanged);
            volume_checked_ = true;
            return result_;
        }
        volume_checked_ = false;
        if (!staging_)
        {
            if (!reader_.open(slot_)) return fail(CheckpointLoadStep::IoError);
            if (!state_.beginSnapshot()) return fail(CheckpointLoadStep::StateRejected);
            staging_ = true;
            return result_;
        }
        size_t count = 0;
        switch (reader_.step(bytes, capacity, entries, entry_capacity, count))
        {
        case CheckpointReadStep::Reading:
            if (count && !state_.appendSnapshot(entries, count)) return fail(CheckpointLoadStep::StateRejected);
            return result_;
        case CheckpointReadStep::Invalid:
            return fail(CheckpointLoadStep::Invalid);
        case CheckpointReadStep::WorkspaceTooSmall:
            return fail(CheckpointLoadStep::StateRejected);
        case CheckpointReadStep::IoError:
            return fail(CheckpointLoadStep::IoError);
        case CheckpointReadStep::Verified:
            break;
        }
        if (reader_.sequence() != expected_.sequence || reader_.digest() != expected_.digest) return fail(CheckpointLoadStep::Invalid);
        if (!state_.commitSnapshot(validate)) return fail(CheckpointLoadStep::StateRejected);
        staging_ = false;
        return result_ = CheckpointLoadStep::Applied;
    }

  private:
    CheckpointLoadStep fail(CheckpointLoadStep result)
    {
        if (staging_) state_.discardSnapshot();
        staging_ = false;
        return result_ = result;
    }
    ::geocaching::storage::LogicalState& state_;
    const ::geocaching::storage::VolumeInstance volume_;
    const char slot_;
    const ::geocaching::storage::CheckpointCandidate expected_;
    SdCheckpointReader<Digest> reader_;
    bool staging_ = false;
    bool volume_checked_ = false;
    CheckpointLoadStep result_ = CheckpointLoadStep::Loading;
};
} // namespace platform::esp::arduino_common::geocaching
