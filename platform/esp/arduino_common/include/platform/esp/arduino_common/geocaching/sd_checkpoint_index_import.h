#pragma once
#include "geocaching/storage/checkpoint_selection.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_reader.h"
#include "platform/esp/arduino_common/geocaching/sd_index_append.h"
#include "platform/esp/arduino_common/geocaching/sd_index_equivalent.h"
#include "platform/esp/arduino_common/geocaching/sd_index_head_writer.h"
#include "platform/esp/arduino_common/geocaching/sd_index_references.h"
#include "platform/esp/arduino_common/geocaching/sd_index_root_writer.h"
#include <memory>
#include <variant>

namespace platform::esp::arduino_common::geocaching
{
// The owner first selects a verified checkpoint,
// pins that checkpoint slot, and later replays its journal suffix before use.
// A partial index directory is recovery evidence; this operation never erases it.
template <class Digest>
class SdCheckpointIndexImport
{
  public:
    SdCheckpointIndexImport(const ::geocaching::storage::VolumeInstance& volume, Digest& digest) : volume_(volume), reader_(digest) {}
    bool begin(char slot, const ::geocaching::storage::CheckpointCandidate& selected,
               uint8_t* frame, size_t capacity, ::geocaching::storage::IndexRootBytes& root)
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexRootWriteStep::Idle || (slot != 'a' && slot != 'b') ||
            selected.state != CheckpointCandidateState::Verified || !selected.sequence || !frame || capacity < 24) return false;
        const auto a = reinterpret_cast<uintptr_t>(frame), b = reinterpret_cast<uintptr_t>(root.data());
        if (a <= b ? b - a < capacity : a - b < root.size()) return false;
        selected_ = selected;
        slot_ = slot;
        frame_ = frame;
        capacity_ = capacity;
        root_ = &root;
        root.fill(0);
        result_ = IndexRootWriteStep::Working;
        return true;
    }
    bool selected(::geocaching::storage::IndexRootView& root) const
    {
        root = {};
        return result_ == IndexRootWriteStep::Verified &&
               ::geocaching::storage::decodeIndexRoot({root_->data(), root_->size()}, volume_, root);
    }
    // Only a replacement's unused slot has been modified before publication.
    // Its owner may discard this object, restore its candidate metadata lease,
    // and continue on the still-published parent without running recovery.
    bool replacementUnpublished() const
    {
        return replacement_ && result_ == IndexRootWriteStep::Working &&
               phase_ != Phase::RootFirst && phase_ != Phase::RootSecond;
    }
    // Exclusive maintenance owner only: pin the parent and checkpoint, exclude
    // commits, and drain readers of the target slot before calling. The supplied
    // checkpoint is compared with the complete parent state before publication.
    // A pre-existing target slot is never reused or deleted here.
    bool beginReplacement(char checkpoint_slot, const ::geocaching::storage::CheckpointCandidate& checkpoint,
                          const ::geocaching::storage::IndexRootView& parent, unsigned parent_copy,
                          uint8_t* frame, size_t capacity, ::geocaching::storage::IndexRootBytes& candidate,
                          uint8_t* comparison_frame, size_t comparison_capacity)
    {
        using namespace ::geocaching::storage;
        if (!validIndexRoot(parent) || !comparison_frame || comparison_capacity < 24 || parent_copy > 1 || checkpoint.sequence != parent.sequence ||
            parent.epoch == UINT64_MAX || parent.revision == UINT64_MAX) return false;
        const auto overlaps = [](::geocaching::ByteView left, ::geocaching::ByteView right)
        {
            const auto a = reinterpret_cast<uintptr_t>(left.data), b = reinterpret_cast<uintptr_t>(right.data);
            return a <= b ? b - a < left.size : a - b < right.size;
        };
        if (overlaps(parent.shards, {candidate.data(), candidate.size()}) ||
            overlaps(parent.shards, {frame, capacity}) || overlaps(parent.shards, {comparison_frame, comparison_capacity}) ||
            overlaps({candidate.data(), candidate.size()}, {comparison_frame, comparison_capacity}) ||
            overlaps({frame, capacity}, {comparison_frame, comparison_capacity})) return false;
        if (!begin(checkpoint_slot, checkpoint, frame, capacity, candidate)) return false;
        parent_ = parent;
        index_slot_ = parent.slot == 'a' ? 'b' : 'a';
        epoch_ = parent.epoch + 1;
        revision_ = parent.revision + 1;
        publish_copy_ = static_cast<uint8_t>(1 - parent_copy);
        replacement_ = true;
        comparison_frame_ = comparison_frame;
        comparison_capacity_ = comparison_capacity;
        return true;
    }
    IndexRootWriteStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexRootWriteStep::Working) return result_;
        switch (phase_)
        {
        case Phase::Volume:
        {
            VolumeInstance current;
            if (inspectSdVolume(current) != SdVolumeResult::Ready) return fail(IndexRootWriteStep::IoError);
            if (current != volume_) return fail(IndexRootWriteStep::VolumeChanged);
            phase_ = Phase::Exists;
            return result_;
        }
        case Phase::Exists:
            if (replacement_)
            {
                if (!storage::sd_is_directory("/trailmate/geocaching/.state/index")) return fail(IndexRootWriteStep::Invalid);
                phase_ = Phase::TargetSlot;
                return result_;
            }
            if (storage::sd_exists("/trailmate/geocaching/.state/index")) return fail(IndexRootWriteStep::Invalid);
            phase_ = Phase::Directory;
            return result_;
        case Phase::Directory:
            if (storage::sd_is_directory("/trailmate/geocaching/.state/index")) return fail(IndexRootWriteStep::Invalid);
            phase_ = Phase::Create;
            return result_;
        case Phase::Create:
            if (!storage::sd_mkdir("/trailmate/geocaching/.state/index")) return fail(IndexRootWriteStep::IoError);
            phase_ = Phase::Slot;
            return result_;
        case Phase::TargetSlot:
        case Phase::Slot:
        {
            const char* path = index_slot_ == 'a' ? "/trailmate/geocaching/.state/index/a" : "/trailmate/geocaching/.state/index/b";
            if (phase_ == Phase::TargetSlot)
            {
                if (storage::sd_exists(path) || storage::sd_is_directory(path)) return fail(IndexRootWriteStep::Invalid);
                phase_ = Phase::Slot;
                return result_;
            }
            if (!storage::sd_mkdir(path)) return fail(IndexRootWriteStep::IoError);
            phase_ = Phase::Open;
            return result_;
        }
        case Phase::Open:
            if (!reader_.open(slot_)) return fail(IndexRootWriteStep::IoError);
            phase_ = Phase::Read;
            return result_;
        case Phase::Read:
        {
            CheckpointPageCursor page;
            const auto status = reader_.stepCursor(frame_, capacity_, page);
            if (status == CheckpointReadStep::Reading)
            {
                if (reader_.pendingIndex(cursor_)) phase_ = Phase::Entry;
                return result_;
            }
            if (status != CheckpointReadStep::Verified) return fail(status == CheckpointReadStep::IoError ? IndexRootWriteStep::IoError : IndexRootWriteStep::Invalid);
            if (reader_.sequence() != selected_.sequence || reader_.digest() != selected_.digest) return fail(IndexRootWriteStep::Invalid);
            IndexRootView root{epoch_, selected_.sequence, revision_, index_slot_, {root_->data() + 48, kIndexShardBitmapSize}};
            IndexRootView transition;
            if (replacement_ && !selectIndexRoot(parent_, root, transition)) return fail(IndexRootWriteStep::Invalid);
            if (!encodeIndexRoot(volume_, root, *root_)) return fail(IndexRootWriteStep::Invalid);
            references_.reset(new (std::nothrow) SdIndexReferences(volume_));
            if (!references_ || !references_->begin(root, frame_, capacity_)) return fail(IndexRootWriteStep::Invalid);
            phase_ = Phase::References;
            return result_;
        }
        case Phase::Entry:
            if (!cursor_.next(entry_))
            {
                if (!cursor_.complete()) return fail(IndexRootWriteStep::Invalid);
                phase_ = Phase::Read;
                return result_;
            }
            if (entry_.location.record_sequence != selected_.sequence ||
                !validStoredRowShape({entry_.table, entry_.key, {frame_ + entry_.location.value_offset - entry_.location.frame_offset, entry_.location.value_size}, false}))
                return fail(IndexRootWriteStep::Invalid);
            phase_ = Phase::Table;
            return result_;
        case Phase::Table:
        case Phase::CreateTable:
        {
            char path[64];
            std::snprintf(path, sizeof(path), "/trailmate/geocaching/.state/index/%c/%02x", index_slot_, static_cast<unsigned>(entry_.table));
            if (phase_ == Phase::Table && !storage::sd_is_directory(path))
            {
                phase_ = Phase::CreateTable;
                return result_;
            }
            if (phase_ == Phase::CreateTable && !storage::sd_mkdir(path)) return fail(IndexRootWriteStep::IoError);
            if (!io_.template emplace<SdIndexAppend>(volume_, index_slot_).begin(entry_)) return fail(IndexRootWriteStep::Invalid);
            phase_ = Phase::Append;
            return result_;
        }
        case Phase::Append:
        {
            auto& append = std::get<SdIndexAppend>(io_);
            const auto status = append.step();
            if (status == IndexAppendStep::Working) return result_;
            if (status != IndexAppendStep::Verified) return mapError(status);
            head_ = {epoch_, selected_.sequence, append.writtenLength(), entry_.table, static_cast<uint8_t>(::sys::crc32(entry_.key.data, entry_.key.size))};
            if (!io_.template emplace<SdIndexHeadWriter>(volume_, index_slot_).begin(entry_.key, 0, head_)) return fail(IndexRootWriteStep::Invalid);
            phase_ = Phase::HeadFirst;
            return result_;
        }
        case Phase::HeadFirst:
        case Phase::HeadSecond:
        {
            const auto status = std::get<SdIndexHeadWriter>(io_).step();
            if (status == IndexHeadWriteStep::Working) return result_;
            if (status != IndexHeadWriteStep::Verified) return mapError(status);
            if (phase_ == Phase::HeadFirst)
            {
                if (!io_.template emplace<SdIndexHeadWriter>(volume_, index_slot_).begin(entry_.key, 1, head_)) return fail(IndexRootWriteStep::Invalid);
                phase_ = Phase::HeadSecond;
            }
            else
            {
                (*root_)[48 + (entry_.table - 1) * 32 + head_.bucket / 8] |= static_cast<uint8_t>(1u << (head_.bucket % 8));
                io_.template emplace<std::monostate>();
                phase_ = Phase::Entry;
            }
            return result_;
        }
        case Phase::References:
        {
            const auto status = references_->step();
            if (status == IndexScanStep::Working) return result_;
            if (status != IndexScanStep::End) return mapError(status);
            references_.reset();
            if (replacement_)
            {
                IndexRootView candidate;
                if (!decodeIndexRoot({root_->data(), root_->size()}, volume_, candidate)) return fail(IndexRootWriteStep::Invalid);
                equivalent_.reset(new (std::nothrow) SdIndexEquivalent(volume_));
                if (!equivalent_ || !equivalent_->begin(parent_, candidate, frame_, capacity_, comparison_frame_, comparison_capacity_))
                    return fail(IndexRootWriteStep::Invalid);
                phase_ = Phase::Equivalent;
                return result_;
            }
            phase_ = Phase::Publish;
            return result_;
        }
        case Phase::Equivalent:
        {
            const auto status = equivalent_->step();
            if (status == IndexScanStep::Working) return result_;
            if (status != IndexScanStep::End) return mapError(status);
            equivalent_.reset();
            phase_ = Phase::Publish;
            return result_;
        }
        case Phase::Publish:
        {
            if (!io_.template emplace<SdIndexRootWriter>(volume_).begin(publish_copy_, *root_)) return fail(IndexRootWriteStep::Invalid);
            phase_ = Phase::RootFirst;
            return result_;
        }
        case Phase::RootFirst:
        case Phase::RootSecond:
        {
            const auto status = std::get<SdIndexRootWriter>(io_).step();
            if (status == IndexRootWriteStep::Working) return result_;
            if (status != IndexRootWriteStep::Verified) return fail(status);
            if (replacement_ || phase_ == Phase::RootSecond) return fail(IndexRootWriteStep::Verified);
            if (!io_.template emplace<SdIndexRootWriter>(volume_).begin(1, *root_)) return fail(IndexRootWriteStep::Invalid);
            phase_ = Phase::RootSecond;
            return result_;
        }
        }
        return fail(IndexRootWriteStep::Invalid);
    }

  private:
    enum class Phase : uint8_t
    {
        Volume,
        Exists,
        Directory,
        Create,
        TargetSlot,
        Slot,
        Open,
        Read,
        Entry,
        Table,
        CreateTable,
        Append,
        HeadFirst,
        HeadSecond,
        References,
        Equivalent,
        Publish,
        RootFirst,
        RootSecond
    };
    template <class Status>
    IndexRootWriteStep mapError(Status status)
    {
        return fail(status == Status::VolumeChanged ? IndexRootWriteStep::VolumeChanged : status == Status::IoError ? IndexRootWriteStep::IoError
                                                                                                                    : IndexRootWriteStep::Invalid);
    }
    IndexRootWriteStep fail(IndexRootWriteStep status)
    {
        references_.reset();
        equivalent_.reset();
        io_.template emplace<std::monostate>();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::CheckpointCandidate selected_;
    SdCheckpointReader<Digest> reader_;
    ::geocaching::storage::CheckpointIndexCursor cursor_;
    ::geocaching::storage::IndexedMutation entry_;
    ::geocaching::storage::IndexShardHead head_;
    std::variant<std::monostate, SdIndexAppend, SdIndexHeadWriter, SdIndexRootWriter> io_;
    std::unique_ptr<SdIndexReferences> references_;
    std::unique_ptr<SdIndexEquivalent> equivalent_;
    uint8_t* comparison_frame_ = nullptr;
    size_t comparison_capacity_ = 0;
    ::geocaching::storage::IndexRootBytes* root_ = nullptr;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0;
    ::geocaching::storage::IndexRootView parent_;
    uint64_t epoch_ = 1, revision_ = 1;
    char index_slot_ = 'a';
    uint8_t publish_copy_ = 0;
    bool replacement_ = false;
    char slot_ = 0;
    Phase phase_ = Phase::Volume;
    IndexRootWriteStep result_ = IndexRootWriteStep::Idle;
};
} // namespace platform::esp::arduino_common::geocaching
