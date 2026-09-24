#pragma once
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_index_import.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_selection.h"
#include "platform/esp/arduino_common/geocaching/sd_index_initialize.h"
#include "platform/esp/arduino_common/geocaching/sd_index_references.h"
#include "platform/esp/arduino_common/geocaching/sd_index_replay.h"
#include "platform/esp/arduino_common/geocaching/sd_index_root_reader.h"

namespace platform::esp::arduino_common::geocaching
{
enum class IndexedRecoveryStep : uint8_t
{
    Working,
    Restored,
    IoError,
    OutOfMemory,
    VolumeChanged,
    RecoveryRequired
};

// One storage owner, fresh instance per recovery. Buffers are disjoint caller
// leases; no logical table arena is used. Restored precedes GPX-install recovery
// and application readiness. Partial index generations are never overwritten.
template <class Digest>
class SdIndexedRecovery
{
  public:
    SdIndexedRecovery(const ::geocaching::storage::VolumeInstance& volume,
                      ::geocaching::storage::IndexRootBytes& first, ::geocaching::storage::IndexRootBytes& second,
                      uint8_t* frame, size_t capacity, uint8_t* validation_frame, size_t validation_capacity,
                      ::geocaching::storage::MutationView* mutations, size_t mutation_capacity)
        : volume_(volume), roots_{&first, &second}, frame_(frame), capacity_(capacity), validation_frame_(validation_frame),
          validation_capacity_(validation_capacity), mutations_(mutations), mutation_capacity_(mutation_capacity)
    {
        using ::geocaching::ByteView;
        if (!frame || !validation_frame || !mutations || !mutation_capacity || mutation_capacity > 64 || capacity < 24 || validation_capacity < 24)
        {
            result_ = IndexedRecoveryStep::RecoveryRequired;
            return;
        }
        const ByteView leases[] = {{first.data(), first.size()}, {second.data(), second.size()}, {frame, capacity}, {validation_frame, validation_capacity}, {reinterpret_cast<const uint8_t*>(mutations), mutation_capacity * sizeof(*mutations)}};
        for (size_t i = 0; i < 5; ++i)
            for (size_t j = 0; j < i; ++j)
            {
                const auto a = reinterpret_cast<uintptr_t>(leases[i].data), b = reinterpret_cast<uintptr_t>(leases[j].data);
                if (a <= b ? b - a < leases[i].size : a - b < leases[j].size) result_ = IndexedRecoveryStep::RecoveryRequired;
            }
    }
    bool selected(::geocaching::storage::IndexRootView& root, unsigned& copy) const
    {
        root = {};
        if (result_ != IndexedRecoveryStep::Restored) return false;
        root = root_;
        copy = copy_;
        return true;
    }
    IndexedRecoveryStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexedRecoveryStep::Working) return result_;
        switch (phase_)
        {
        case Phase::Volume:
        {
            VolumeInstance current;
            if (inspectSdVolume(current) != SdVolumeResult::Ready) return finish(IndexedRecoveryStep::IoError);
            if (current != volume_) return finish(IndexedRecoveryStep::VolumeChanged);
            phase_ = Phase::ProbeFirst;
            return result_;
        }
        case Phase::ProbeFirst:
        case Phase::ProbeSecond:
        {
            uint8_t probe[24];
            const auto read = storage::sd_read_file(phase_ == Phase::ProbeFirst ? "/trailmate/geocaching/.state/index/root.h0" : "/trailmate/geocaching/.state/index/root.h1", probe, sizeof(probe));
            if (read.status != storage::SdFileReadStatus::Missing && read.status != storage::SdFileReadStatus::Ready &&
                read.status != storage::SdFileReadStatus::Invalid) return finish(IndexedRecoveryStep::IoError);
            root_present_ |= read.status != storage::SdFileReadStatus::Missing;
            if (phase_ == Phase::ProbeFirst)
            {
                phase_ = Phase::ProbeSecond;
                return result_;
            }
            if (root_present_)
            {
                if (!io_.template emplace<SdIndexRootReader>(volume_).begin(*roots_[0], *roots_[1])) return finish(IndexedRecoveryStep::RecoveryRequired);
                phase_ = Phase::Roots;
            }
            else
            {
                io_.template emplace<SdCheckpointSelection<Digest>>(volume_);
                phase_ = Phase::Checkpoint;
            }
            return result_;
        }
        case Phase::Roots:
        {
            auto& read = std::get<SdIndexRootReader>(io_);
            const auto status = read.step();
            if (status == IndexRootReadStep::Working) return result_;
            if (status != IndexRootReadStep::Ready || !read.selected(root_)) return error(status);
            copy_ = static_cast<unsigned>(read.selectedCopy());
            return inventory();
        }
        case Phase::Checkpoint:
        {
            auto& selection = std::get<SdCheckpointSelection<Digest>>(io_);
            const auto status = selection.stepCursor(frame_, capacity_);
            if (status == CheckpointSelectionStep::Reading) return result_;
            if (status == CheckpointSelectionStep::NoCheckpoint)
            {
                if (!io_.template emplace<SdIndexInitialize>(volume_).begin(*roots_[0])) return finish(IndexedRecoveryStep::RecoveryRequired);
                phase_ = Phase::Initialize;
                return result_;
            }
            if (status == CheckpointSelectionStep::RetryLater) return finish(IndexedRecoveryStep::IoError);
            if (status == CheckpointSelectionStep::VolumeChanged) return finish(IndexedRecoveryStep::VolumeChanged);
            if (status != CheckpointSelectionStep::Selected) return finish(IndexedRecoveryStep::RecoveryRequired);
            const bool second = selection.choice() == CheckpointChoice::SlotB;
            const auto checkpoint = selection.candidate(second);
            if (!io_.template emplace<SdCheckpointIndexImport<Digest>>(volume_, import_digest_).begin(second ? 'b' : 'a', checkpoint, frame_, capacity_, *roots_[0])) return finish(IndexedRecoveryStep::RecoveryRequired);
            phase_ = Phase::Import;
            return result_;
        }
        case Phase::Initialize:
        case Phase::Import:
        {
            const auto status = phase_ == Phase::Initialize ? std::get<SdIndexInitialize>(io_).step() : std::get<SdCheckpointIndexImport<Digest>>(io_).step();
            if (status == IndexRootWriteStep::Working) return result_;
            if (status != IndexRootWriteStep::Verified) return error(status);
            *roots_[1] = *roots_[0];
            if (!decodeIndexRoot({roots_[0]->data(), roots_[0]->size()}, volume_, root_)) return finish(IndexedRecoveryStep::RecoveryRequired);
            copy_ = 0;
            return inventory();
        }
        case Phase::Inventory:
        {
            auto& scan = std::get<SdJournalInventory>(io_);
            const auto status = scan.step();
            if (status == InventoryStep::Scanning) return result_;
            if (status == InventoryStep::RetryLater) return finish(IndexedRecoveryStep::IoError);
            if (status == InventoryStep::VolumeChanged) return finish(IndexedRecoveryStep::VolumeChanged);
            if (status != InventoryStep::Complete) return finish(IndexedRecoveryStep::RecoveryRequired);
            const auto range = scan.range();
            io_.template emplace<SdIndexReplay>(volume_, root_, copy_, *roots_[0], *roots_[1], range,
                                                frame_, capacity_, mutations_, mutation_capacity_);
            phase_ = Phase::Replay;
            return result_;
        }
        case Phase::Replay:
        {
            auto& replay = std::get<SdIndexReplay>(io_);
            const auto status = replay.step();
            if (status == IndexReplayStep::Working) return result_;
            if (status == IndexReplayStep::NeedsValidation)
            {
                // The shared validator enforces stored row shapes, task/attempt
                // references and immutable author history before root publication.
                if (!replay.accept(true, validation_frame_, validation_capacity_)) return finish(IndexedRecoveryStep::RecoveryRequired);
                return result_;
            }
            if (status == IndexReplayStep::RetryLater || status == IndexReplayStep::IoError) return finish(IndexedRecoveryStep::IoError);
            if (status == IndexReplayStep::OutOfMemory) return finish(IndexedRecoveryStep::OutOfMemory);
            if (status == IndexReplayStep::VolumeChanged) return finish(IndexedRecoveryStep::VolumeChanged);
            if (status != IndexReplayStep::Complete || !replay.selected(root_, copy_)) return finish(IndexedRecoveryStep::RecoveryRequired);
            // Root selection and replay only prove the newly applied suffix.
            // Existing shards and their referenced values must also be readable
            // before the application can publish new work on this snapshot.
            if (!io_.template emplace<SdIndexScan>(volume_).begin(root_, audit_table_, frame_, capacity_)) return finish(IndexedRecoveryStep::RecoveryRequired);
            phase_ = Phase::Audit;
            return result_;
        }
        case Phase::Audit:
        {
            auto& scan = std::get<SdIndexScan>(io_);
            const auto status = scan.step();
            if (status == IndexScanStep::Working) return result_;
            if (status == IndexScanStep::Item)
            {
                if (!scan.advance()) return finish(IndexedRecoveryStep::RecoveryRequired);
                return result_;
            }
            if (status != IndexScanStep::End) return error(status);
            if (++audit_table_ <= 13)
            {
                if (!io_.template emplace<SdIndexScan>(volume_).begin(root_, audit_table_, frame_, capacity_)) return finish(IndexedRecoveryStep::RecoveryRequired);
                return result_;
            }
            if (!io_.template emplace<SdIndexReferences>(volume_).begin(root_, frame_, capacity_)) return finish(IndexedRecoveryStep::RecoveryRequired);
            phase_ = Phase::References;
            return result_;
        }
        case Phase::References:
        {
            const auto status = std::get<SdIndexReferences>(io_).step();
            if (status == IndexScanStep::Working) return result_;
            return status == IndexScanStep::End ? finish(IndexedRecoveryStep::Restored) : error(status);
        }
        }
        return finish(IndexedRecoveryStep::RecoveryRequired);
    }

  private:
    enum class Phase : uint8_t
    {
        Volume,
        ProbeFirst,
        ProbeSecond,
        Roots,
        Checkpoint,
        Initialize,
        Import,
        Inventory,
        Replay,
        Audit,
        References
    };
    IndexedRecoveryStep inventory()
    {
        io_.template emplace<SdJournalInventory>(volume_, root_.sequence);
        phase_ = Phase::Inventory;
        return result_;
    }
    template <class Status>
    IndexedRecoveryStep error(Status status)
    {
        return finish(status == Status::VolumeChanged ? IndexedRecoveryStep::VolumeChanged : status == Status::IoError ? IndexedRecoveryStep::IoError
                                                                                                                       : IndexedRecoveryStep::RecoveryRequired);
    }
    IndexedRecoveryStep finish(IndexedRecoveryStep status)
    {
        io_.template emplace<std::monostate>();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootBytes* roots_[2];
    ::geocaching::storage::IndexRootView root_;
    uint8_t* frame_;
    size_t capacity_;
    uint8_t* validation_frame_;
    size_t validation_capacity_;
    ::geocaching::storage::MutationView* mutations_;
    size_t mutation_capacity_;
    Digest import_digest_;
    std::variant<std::monostate, SdIndexRootReader, SdCheckpointSelection<Digest>, SdIndexInitialize,
                 SdCheckpointIndexImport<Digest>, SdJournalInventory, SdIndexReplay, SdIndexScan, SdIndexReferences>
        io_;
    unsigned copy_ = 0;
    uint8_t audit_table_ = 1;
    bool root_present_ = false;
    Phase phase_ = Phase::Volume;
    IndexedRecoveryStep result_ = IndexedRecoveryStep::Working;
};
} // namespace platform::esp::arduino_common::geocaching
