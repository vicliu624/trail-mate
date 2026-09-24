#pragma once
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_selection.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_loader.h"
#include "platform/esp/arduino_common/geocaching/sd_journal_replay.h"
#include <optional>

namespace platform::esp::arduino_common::geocaching
{
enum class StateRecoveryStep : uint8_t { Working, JournalRestored, RetryRequired, Corrupt, VolumeChanged, StateRejected, TailNeedsRepair };

// The supplied state is an isolated recovery state, never the UI's live state.
// A fresh object is required after RetryRequired. One step advances one existing
// component; the storage owner supplies operation budgeting and cancellation.
// JournalRestored is followed by GPX/install validation before business readiness.
template<class Digest>
class SdStateRecovery
{
  public:
    SdStateRecovery(const ::geocaching::storage::VolumeInstance& volume,
                      ::geocaching::storage::LogicalState& recovery_state)
        : volume_(volume), state_(recovery_state), selection_(volume) {}
    uint64_t replayedSequence() const { return sequence_; }

    template<class Validate>
    StateRecoveryStep step(uint8_t* bytes, size_t capacity,
                            ::geocaching::storage::MutationView* entries, size_t entry_capacity, Validate validate)
    {
        if (result_ != StateRecoveryStep::Working) return result_;
        if (phase_ == Phase::Select)
        {
            switch (selection_.step(bytes, capacity, entries, entry_capacity))
            {
            case CheckpointSelectionStep::Reading: return result_;
            case CheckpointSelectionStep::RetryLater: return result_ = StateRecoveryStep::RetryRequired;
            case CheckpointSelectionStep::Corrupt: return result_ = StateRecoveryStep::Corrupt;
            case CheckpointSelectionStep::VolumeChanged: return result_ = StateRecoveryStep::VolumeChanged;
            case CheckpointSelectionStep::NoCheckpoint:
                if (!state_.beginSnapshot() || !state_.commitSnapshot(validate))
                { state_.discardSnapshot(); return result_ = StateRecoveryStep::StateRejected; }
                inventory_.emplace(volume_, 0);
                phase_ = Phase::Inventory;
                return result_;
            case CheckpointSelectionStep::Selected: break;
            }
            const bool slot_b = selection_.choice() == ::geocaching::storage::CheckpointChoice::SlotB;
            const auto& selected = selection_.candidate(slot_b);
            sequence_ = selected.sequence;
            loader_.emplace(load_digest_, state_, volume_, slot_b ? 'b' : 'a', selected);
            phase_ = Phase::Load;
            return result_;
        }
        if (phase_ == Phase::Load)
        {
            switch (loader_->step(bytes, capacity, entries, entry_capacity, validate))
            {
            case CheckpointLoadStep::Loading: return result_;
            case CheckpointLoadStep::IoError: return result_ = StateRecoveryStep::RetryRequired;
            case CheckpointLoadStep::Invalid: return result_ = StateRecoveryStep::Corrupt;
            case CheckpointLoadStep::StateRejected: return result_ = StateRecoveryStep::StateRejected;
            case CheckpointLoadStep::VolumeChanged: return result_ = StateRecoveryStep::VolumeChanged;
            case CheckpointLoadStep::Applied: break;
            }
            loader_.reset();
            inventory_.emplace(volume_, sequence_);
            phase_ = Phase::Inventory;
            return result_;
        }
        if (phase_ == Phase::Inventory)
        {
            switch (inventory_->step())
            {
            case InventoryStep::Scanning:
            case InventoryStep::RetryLater: return result_;
            case InventoryStep::Corrupt: return result_ = StateRecoveryStep::Corrupt;
            case InventoryStep::VolumeChanged: return result_ = StateRecoveryStep::VolumeChanged;
            case InventoryStep::Complete: break;
            }
            replay_.emplace(volume_, sequence_, inventory_->range(), bytes, capacity, entries, entry_capacity);
            inventory_.reset();
            phase_ = Phase::Replay;
            return result_;
        }
        switch (replay_->applyNext(state_, validate))
        {
        case ReplayStep::Applied:
            sequence_ = replay_->appliedSequence();
            return result_;
        case ReplayStep::Advancing: return result_;
        case ReplayStep::JournalComplete: return result_ = StateRecoveryStep::JournalRestored;
        case ReplayStep::RetryLater: return result_ = StateRecoveryStep::RetryRequired;
        case ReplayStep::VolumeChanged: return result_ = StateRecoveryStep::VolumeChanged;
        case ReplayStep::ApplicationRejected: return result_ = StateRecoveryStep::StateRejected;
        case ReplayStep::TailTruncated: return result_ = StateRecoveryStep::TailNeedsRepair;
        case ReplayStep::Transaction:
        case ReplayStep::Corrupt: return result_ = StateRecoveryStep::Corrupt;
        }
        return result_ = StateRecoveryStep::Corrupt;
    }

  private:
    enum class Phase : uint8_t { Select, Load, Inventory, Replay };
    const ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::LogicalState& state_;
    SdCheckpointSelection<Digest> selection_;
    Digest load_digest_;
    std::optional<SdCheckpointLoader<Digest>> loader_;
    std::optional<SdJournalInventory> inventory_;
    std::optional<SdJournalReplay> replay_;
    uint64_t sequence_ = 0;
    Phase phase_ = Phase::Select;
    StateRecoveryStep result_ = StateRecoveryStep::Working;
};
} // namespace platform::esp::arduino_common::geocaching
