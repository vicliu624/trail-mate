#pragma once
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_build.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_index_import.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_references.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_selection.h"
#include "platform/esp/arduino_common/geocaching/sd_index_cleanup.h"
#include "platform/esp/arduino_common/geocaching/sd_journal_reclaim.h"

namespace platform::esp::arduino_common::geocaching
{
enum class CheckpointRotationStep : uint8_t
{
    Idle,
    Working,
    Complete,
    Yielded,
    Busy,
    Unavailable,
    OutOfMemory,
    Deferred,
    Invalid,
    IoError,
    VolumeChanged
};

// Exclusive workspace owner throughout: roots and backing files cannot be
// changed by another writer, and all older readers must already be drained.
// Caller supplies a fully validated recovered root pair. Every failure after
// begin requires normal index recovery before resuming application writes.
// Journal reclamation follows publication of both roots and proves the suffix
// needed by the older verified checkpoint before deleting covered segments.
template <class Digest>
class SdCheckpointRotation
{
  public:
    explicit SdCheckpointRotation(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(::geocaching::storage::IndexRootBytes& first, ::geocaching::storage::IndexRootBytes& second,
               unsigned current_copy, uint8_t* frame, size_t capacity, uint8_t* comparison, size_t comparison_capacity,
               uint64_t minimum_sequence_delta = 0)
    {
        using namespace ::geocaching::storage;
        if (result_ != CheckpointRotationStep::Idle || current_copy > 1 || !frame || !comparison ||
            capacity < 24 || comparison_capacity < sizeof(SdSortedIndex::Run)) return false;
        const ::geocaching::ByteView leases[] = {{first.data(), first.size()}, {second.data(), second.size()}, {frame, capacity}, {comparison, comparison_capacity}};
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = 0; j < i; ++j)
            {
                const auto a = reinterpret_cast<uintptr_t>(leases[i].data), b = reinterpret_cast<uintptr_t>(leases[j].data);
                if (a <= b ? b - a < leases[i].size : a - b < leases[j].size) return false;
            }
        IndexRootView a, b, selected;
        if (!decodeIndexRoot(leases[0], volume_, a) || !decodeIndexRoot(leases[1], volume_, b) ||
            !selectIndexRoot(a, b, selected) || !selected.sequence ||
            selected.revision != (current_copy ? b : a).revision) return false;
        roots_[0] = &first;
        roots_[1] = &second;
        copy_ = current_copy;
        root_ = current_copy ? b : a;
        frame_ = frame;
        capacity_ = capacity;
        comparison_ = comparison;
        comparison_capacity_ = comparison_capacity;
        minimum_sequence_delta_ = minimum_sequence_delta;
        result_ = CheckpointRotationStep::Working;
        return true;
    }
    bool selected(::geocaching::storage::IndexRootView& root, unsigned& copy) const
    {
        if (result_ != CheckpointRotationStep::Complete && result_ != CheckpointRotationStep::Yielded) return false;
        root = root_;
        copy = copy_;
        return true;
    }
    uint64_t retainedCheckpointSequence() const { return fallback_sequence_; }
    // No I/O. A root publication already in progress must finish before this
    // succeeds. All other work is disposable or touches only unused slots.
    bool yieldToForeground()
    {
        if (result_ != CheckpointRotationStep::Working || interrupted_ ||
            (phase_ == Phase::Mirror && writer_) || phase_ == Phase::MirrorReplacement ||
            (phase_ == Phase::Import && import_ && !import_->replacementUnpublished())) return false;
        fail(CheckpointRotationStep::Yielded);
        // Import borrows the non-current in-memory root as its candidate. Disk
        // roots still select root_; repair this scratch copy before clients use it.
        *roots_[1 - copy_] = *roots_[copy_];
        return true;
    }
    CheckpointRotationStep step()
    {
        using namespace ::geocaching::storage;
        using Result = CheckpointRotationStep;
        if (result_ != Result::Working) return result_;
        if (storage::sd_external_block_owner_active())
        {
            interrupted_ = true;
            return Result::Busy;
        }
        if (!storage::sd_card_ready())
        {
            interrupted_ = true;
            return Result::Unavailable;
        }
        // External ownership can invalidate checkpoint proofs as well as log
        // cursors. Require ordinary recovery before any further mutation.
        if (interrupted_) return fail(Result::Invalid);
        // Fence metadata operations as well as child file transfers. Keep the
        // volume header in a separate slice from a child's 512-byte read.
        if (check_volume_)
        {
            VolumeInstance current;
            const auto checked = inspectSdVolume(current);
            if (checked == SdVolumeResult::Unavailable)
            {
                interrupted_ = true;
                return Result::Unavailable;
            }
            if (checked != SdVolumeResult::Ready) return fail(Result::IoError);
            if (current != volume_) return fail(Result::VolumeChanged);
            check_volume_ = false;
            return result_;
        }
        check_volume_ = true;
        switch (phase_)
        {
        case Phase::Mirror:
        case Phase::MirrorReplacement:
            if (!writer_)
            {
                *roots_[1 - copy_] = *roots_[copy_];
                writer_.reset(new (std::nothrow) SdIndexRootWriter(volume_));
                if (!writer_) return fail(Result::OutOfMemory);
                if (!writer_->begin(1 - copy_, *roots_[1 - copy_])) return fail(Result::Invalid);
                return result_;
            }
            {
                const auto status = writer_->step();
                if (status == IndexRootWriteStep::Working) return result_;
                if (status != IndexRootWriteStep::Verified) return rootFailure(status);
                writer_.reset();
                phase_ = phase_ == Phase::Mirror ? Phase::References : Phase::CleanupOld;
                return result_;
            }
        case Phase::References:
            if (!references_)
            {
                references_.reset(new (std::nothrow) SdCheckpointReferences(volume_));
                if (!references_) return fail(Result::OutOfMemory);
                if (!references_->begin(root_, root_, frame_, capacity_)) return fail(Result::Invalid);
                return result_;
            }
            {
                const auto status = references_->step();
                if (status == IndexScanStep::Working) return result_;
                if (status != IndexScanStep::End) return fail(status == IndexScanStep::VolumeChanged ? Result::VolumeChanged : status == IndexScanStep::IoError ? Result::IoError
                                                                                                                                                                : Result::Invalid);
                if (references_->referenced(target_))
                {
                    // An interrupted rotation can publish a newer checkpoint
                    // before replacing the index. Preserve the older referenced
                    // baseline and reuse the newer unreferenced slot instead.
                    const char alternative = target_ == 'a' ? 'b' : 'a';
                    if (references_->referenced(alternative)) return fail(Result::Deferred);
                    if (candidate_.state != CheckpointCandidateState::Verified || candidate_.sequence > root_.sequence) return fail(Result::Invalid);
                    fallback_sequence_ = candidate_.sequence;
                    target_ = alternative;
                }
                references_.reset();
                phase_ = Phase::Build;
                return result_;
            }
        case Phase::Select:
        case Phase::VerifyPublished:
            if (!selection_)
            {
                selection_.reset(new (std::nothrow) SdCheckpointSelection<Digest>(volume_));
                if (!selection_) return fail(Result::OutOfMemory);
                return result_;
            }
            {
                const auto status = selection_->stepCursor(frame_, capacity_);
                if (status == CheckpointSelectionStep::Reading) return result_;
                if (status != CheckpointSelectionStep::Selected && status != CheckpointSelectionStep::NoCheckpoint)
                    return fail(status == CheckpointSelectionStep::VolumeChanged ? Result::VolumeChanged : status == CheckpointSelectionStep::RetryLater ? Result::IoError
                                                                                                                                                         : Result::Invalid);
                if (phase_ == Phase::Select)
                {
                    const auto choice = selection_->choice();
                    target_ = choice == CheckpointChoice::SlotA ? 'b' : 'a';
                    candidate_ = selection_->candidate(target_ == 'b');
                    if (choice == CheckpointChoice::SlotA || choice == CheckpointChoice::SlotB)
                        fallback_sequence_ = selection_->candidate(choice == CheckpointChoice::SlotB).sequence;
                    if (fallback_sequence_ > root_.sequence) return fail(Result::Invalid);
                    // Read and verify the durable baseline before touching any
                    // file. A fresh session must not compact recent data again.
                    if (root_.sequence - fallback_sequence_ < minimum_sequence_delta_) return fail(Result::Complete);
                    phase_ = Phase::Mirror;
                }
                else
                {
                    candidate_ = selection_->candidate(target_ == 'b');
                    if (candidate_.state != CheckpointCandidateState::Verified || candidate_.sequence != root_.sequence) return fail(Result::Invalid);
                    phase_ = Phase::CleanupTarget;
                }
                selection_.reset();
                return result_;
            }
        case Phase::Build:
            if (!build_)
            {
                build_.reset(new (std::nothrow) SdCheckpointBuild<Digest>(volume_));
                if (!build_) return fail(Result::OutOfMemory);
                if (!build_->begin(root_, frame_, capacity_, comparison_, comparison_capacity_))
                    return fail(build_->step() == CheckpointBuildStep::OutOfMemory ? Result::OutOfMemory : Result::Invalid);
                return result_;
            }
            {
                const auto status = build_->step();
                if (status == CheckpointBuildStep::Working) return result_;
                if (status == CheckpointBuildStep::Busy) return Result::Busy;
                if (status == CheckpointBuildStep::Unavailable) return Result::Unavailable;
                if (status != CheckpointBuildStep::Complete) return fail(status == CheckpointBuildStep::OutOfMemory ? Result::OutOfMemory : status == CheckpointBuildStep::VolumeChanged ? Result::VolumeChanged
                                                                                                                                        : status == CheckpointBuildStep::IoError         ? Result::IoError
                                                                                                                                                                                         : Result::Invalid);
                build_.reset();
                phase_ = Phase::RemoveTarget;
                return result_;
            }
        case Phase::RemoveTarget:
            if (storage::sd_exists(checkpointPath()) && !storage::sd_remove(checkpointPath())) return fail(Result::IoError);
            phase_ = Phase::Publish;
            return result_;
        case Phase::Publish:
            if (!storage::sd_rename(SdCheckpointWriter<Digest>::path, checkpointPath())) return fail(Result::IoError);
            phase_ = Phase::VerifyPublished;
            return result_;
        case Phase::CleanupTarget:
        case Phase::CleanupOld:
            if (!cleanup_)
            {
                cleanup_.reset(new (std::nothrow) SdIndexCleanup(volume_, root_, root_));
                if (!cleanup_) return fail(Result::OutOfMemory);
                return result_;
            }
            {
                const auto status = cleanup_->step();
                if (status == IndexCleanupStep::Working) return result_;
                if (status != IndexCleanupStep::Complete) return fail(status == IndexCleanupStep::VolumeChanged ? Result::VolumeChanged : status == IndexCleanupStep::IoError ? Result::IoError
                                                                                                                                                                              : Result::Invalid);
                cleanup_.reset();
                if (phase_ == Phase::CleanupOld)
                {
                    if (!fallback_sequence_) return fail(Result::Complete);
                    phase_ = Phase::Reclaim;
                    return result_;
                }
                phase_ = Phase::Import;
                return result_;
            }
        case Phase::Reclaim:
            if (!reclaim_)
            {
                reclaim_.reset(new (std::nothrow) SdJournalReclaim(volume_));
                if (!reclaim_) return fail(Result::OutOfMemory);
                if (!reclaim_->begin(fallback_sequence_, root_.sequence, frame_, capacity_)) return fail(Result::Invalid);
                return result_;
            }
            {
                const auto status = reclaim_->step();
                if (status == JournalReclaimStep::Working) return result_;
                if (status == JournalReclaimStep::Busy || status == JournalReclaimStep::Unavailable)
                {
                    interrupted_ = true;
                    return status == JournalReclaimStep::Busy ? Result::Busy : Result::Unavailable;
                }
                return fail(status == JournalReclaimStep::Complete ? Result::Complete : status == JournalReclaimStep::VolumeChanged ? Result::VolumeChanged
                                                                                    : status == JournalReclaimStep::IoError         ? Result::IoError
                                                                                                                                    : Result::Invalid);
            }
        case Phase::Import:
            if (!import_)
            {
                import_.reset(new (std::nothrow) SdCheckpointIndexImport<Digest>(volume_, digest_));
                if (!import_) return fail(Result::OutOfMemory);
                if (!import_->beginReplacement(target_, candidate_, root_, copy_, frame_, capacity_, *roots_[1 - copy_], comparison_, comparison_capacity_)) return fail(Result::Invalid);
                return result_;
            }
            {
                const auto status = import_->step();
                if (status == IndexRootWriteStep::Working) return result_;
                if (status != IndexRootWriteStep::Verified) return rootFailure(status);
                if (!import_->selected(root_)) return fail(Result::Invalid);
                copy_ = 1 - copy_;
                import_.reset();
                phase_ = Phase::MirrorReplacement;
                return result_;
            }
        }
        return fail(Result::Invalid);
    }

  private:
    enum class Phase : uint8_t
    {
        Mirror,
        References,
        Select,
        Build,
        RemoveTarget,
        Publish,
        VerifyPublished,
        CleanupTarget,
        Import,
        MirrorReplacement,
        CleanupOld,
        Reclaim
    };
    const char* checkpointPath() const { return target_ == 'a' ? "/trailmate/geocaching/.state/checkpoint/a.gcs" : "/trailmate/geocaching/.state/checkpoint/b.gcs"; }
    CheckpointRotationStep rootFailure(IndexRootWriteStep status)
    {
        return fail(status == IndexRootWriteStep::VolumeChanged ? CheckpointRotationStep::VolumeChanged : status == IndexRootWriteStep::IoError ? CheckpointRotationStep::IoError
                                                                                                                                                : CheckpointRotationStep::Invalid);
    }
    CheckpointRotationStep fail(CheckpointRotationStep status)
    {
        writer_.reset();
        references_.reset();
        selection_.reset();
        build_.reset();
        cleanup_.reset();
        import_.reset();
        reclaim_.reset();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootBytes* roots_[2]{};
    ::geocaching::storage::IndexRootView root_;
    ::geocaching::storage::CheckpointCandidate candidate_;
    Digest digest_;
    std::unique_ptr<SdIndexRootWriter> writer_;
    std::unique_ptr<SdCheckpointReferences> references_;
    std::unique_ptr<SdCheckpointSelection<Digest>> selection_;
    std::unique_ptr<SdCheckpointBuild<Digest>> build_;
    std::unique_ptr<SdIndexCleanup> cleanup_;
    std::unique_ptr<SdCheckpointIndexImport<Digest>> import_;
    std::unique_ptr<SdJournalReclaim> reclaim_;
    uint8_t *frame_ = nullptr, *comparison_ = nullptr;
    size_t capacity_ = 0, comparison_capacity_ = 0;
    uint64_t fallback_sequence_ = 0, minimum_sequence_delta_ = 0;
    unsigned copy_ = 0;
    char target_ = 'a';
    bool check_volume_ = true, interrupted_ = false;
    Phase phase_ = Phase::Select;
    CheckpointRotationStep result_ = CheckpointRotationStep::Idle;
};
} // namespace platform::esp::arduino_common::geocaching
