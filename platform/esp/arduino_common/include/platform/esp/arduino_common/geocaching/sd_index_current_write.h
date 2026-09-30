#pragma once
#include "platform/esp/arduino_common/geocaching/sd_index_append.h"
#include "platform/esp/arduino_common/geocaching/sd_index_lookup.h"
#include "platform/memory/psram_ptr.h"

namespace platform::esp::arduino_common::geocaching
{
// One transaction replaces one shard. The parent head/frame stay pinned until
// completion. No head is published here; a failed write leaves the parent intact.
// Allocate this bounded operation in PSRAM, never on an ESP task stack.
class SdIndexCurrentWrite
{
  public:
    SdIndexCurrentWrite(const ::geocaching::storage::VolumeInstance& volume, char slot) : volume_(volume), slot_(slot) {}
    bool begin(const ::geocaching::storage::IndexShardHead& parent, ::geocaching::ByteView frame,
               uint64_t previous, uint64_t segment_first, uint32_t frame_offset)
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexAppendStep::Idle || !validIndexShardHead(parent) || parent.sequence > previous ||
            (slot_ != 'a' && slot_ != 'b') || !changes_.open(frame, previous, segment_first, frame_offset)) return false;
        parent_ = parent;
        frame_ = frame;
        previous_ = previous;
        segment_first_ = segment_first;
        frame_offset_ = frame_offset;
        next_ = parent;
        next_.sequence = previous + 1;
        next_.length = 0;
        next_.current_only = true;
        result_ = IndexAppendStep::Working;
        return true;
    }
    bool committedHead(::geocaching::storage::IndexShardHead& out) const
    {
        out = {};
        if (result_ != IndexAppendStep::Verified) return false;
        out = next_;
        return true;
    }
    IndexAppendStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexAppendStep::Working) return result_;
        VolumeInstance current;
        const auto volume_status = session_.inspect(current);
        if (volume_status == SdVolumeResult::Busy) return result_;
        if (volume_status != SdVolumeResult::Ready) return fail(IndexAppendStep::IoError);
        if (current != volume_) return fail(IndexAppendStep::VolumeChanged);
        switch (phase_)
        {
        case Phase::Output:
        {
            char path[96];
            if (!indexShardDataPath(slot_, next_, path, sizeof(path))) return fail(IndexAppendStep::Invalid);
            if (!output_.open(path, "w")) return output_.read_busy() ? result_ : fail(IndexAppendStep::IoError);
            phase_ = parent_.length ? Phase::Input : Phase::Changes;
            break;
        }
        case Phase::Input:
        {
            char path[96];
            if (!indexShardDataPath(slot_, parent_, path, sizeof(path))) return fail(IndexAppendStep::Invalid);
            if (!input_.open(path, "r")) return input_.read_busy() ? result_ : fail(IndexAppendStep::IoError);
            phase_ = Phase::Size;
            break;
        }
        case Phase::Size:
        {
            const auto size = input_.size();
            if (input_.read_busy()) return result_;
            if (size < parent_.length || (parent_.current_only && size != parent_.length)) return fail(IndexAppendStep::Invalid);
            phase_ = Phase::Read;
            break;
        }
        case Phase::Read:
        {
            if (position_ == parent_.length)
            {
                input_.close();
                phase_ = Phase::Changes;
                break;
            }
            const int count = input_.read(bytes_.data() + read_, bytes_.size() - read_);
            if (input_.read_busy()) return result_;
            if (count < 0) return fail(IndexAppendStep::IoError);
            if (!count || static_cast<size_t>(count) > bytes_.size() - read_) return fail(IndexAppendStep::Invalid);
            read_ += static_cast<uint16_t>(count);
            if (read_ != bytes_.size()) break;
            read_ = 0;
            if (!decodeIndexEntry({bytes_.data(), bytes_.size()}, volume_, entry_) ||
                entry_.table != parent_.table || static_cast<uint8_t>(::sys::crc32(entry_.key.data, entry_.key.size)) != parent_.bucket ||
                entry_.location.record_sequence < older_ || entry_.location.record_sequence > parent_.sequence ||
                (parent_.current_only && entry_.erase) ||
                (!parent_.current_only && position_ + bytes_.size() == parent_.length && entry_.location.record_sequence != parent_.sequence)) return fail(IndexAppendStep::Invalid);
            older_ = entry_.location.record_sequence;
            position_ += bytes_.size();
            if (entry_.erase || replaced(entry_.key)) break;
            if (parent_.current_only)
            {
                phase_ = Phase::Copy;
                break;
            }
            // Legacy conversion happens while writing, never while opening a
            // local list. Only the latest reference for each key survives.
            lookup_.reset(::platform::memory::createPsram<SdIndexLookup>(volume_, slot_, parent_.sequence, parent_.length, &session_));
            if (!lookup_ || !lookup_->begin(parent_.table, entry_.key)) return fail(IndexAppendStep::Invalid);
            phase_ = Phase::Latest;
            break;
        }
        case Phase::Latest:
        {
            const auto status = lookup_->step();
            if (status == IndexLookupStep::Working) break;
            if (status != IndexLookupStep::Found) return fail(status == IndexLookupStep::IoError ? IndexAppendStep::IoError : IndexAppendStep::Invalid);
            const bool latest = lookup_->entryOffset() == position_ - bytes_.size();
            lookup_.reset();
            phase_ = latest ? Phase::Copy : Phase::Read;
            break;
        }
        case Phase::Copy:
            if (!writeEntry()) return fail(IndexAppendStep::IoError);
            phase_ = Phase::Read;
            break;
        case Phase::Changes:
        {
            if (!changes_.next(entry_))
            {
                if (!changes_.complete()) return fail(IndexAppendStep::Invalid);
                phase_ = Phase::Flush;
                break;
            }
            if (entry_.table != parent_.table || static_cast<uint8_t>(::sys::crc32(entry_.key.data, entry_.key.size)) != parent_.bucket || entry_.erase) break;
            if (!encodeIndexEntry(volume_, entry_, bytes_)) return fail(IndexAppendStep::Invalid);
            phase_ = Phase::ChangeWrite;
            break;
        }
        case Phase::ChangeWrite:
            if (!writeEntry()) return fail(IndexAppendStep::IoError);
            phase_ = Phase::Changes;
            break;
        case Phase::Flush:
            if (output_.size() != next_.length || !output_.flush()) return fail(IndexAppendStep::IoError);
            output_.close();
            phase_ = Phase::VerifyOpen;
            break;
        case Phase::VerifyOpen:
        {
            char path[96];
            if (!indexShardDataPath(slot_, next_, path, sizeof(path))) return fail(IndexAppendStep::Invalid);
            if (!input_.open(path, "r")) return input_.read_busy() ? result_ : fail(IndexAppendStep::IoError);
            position_ = 0;
            phase_ = Phase::VerifySize;
            break;
        }
        case Phase::VerifySize:
        {
            const auto size = input_.size();
            if (input_.read_busy()) return result_;
            if (size != next_.length) return fail(IndexAppendStep::Invalid);
            phase_ = Phase::VerifyRead;
            break;
        }
        case Phase::VerifyRead:
        {
            if (position_ == next_.length)
            {
                if (verified_crc_ != written_crc_) return fail(IndexAppendStep::Invalid);
                input_.close();
                return result_ = IndexAppendStep::Verified;
            }
            const int count = input_.read(bytes_.data() + read_, bytes_.size() - read_);
            if (input_.read_busy()) return result_;
            if (count < 0) return fail(IndexAppendStep::IoError);
            if (!count || static_cast<size_t>(count) > bytes_.size() - read_) return fail(IndexAppendStep::Invalid);
            read_ += static_cast<uint16_t>(count);
            if (read_ != bytes_.size()) break;
            verified_crc_ = ::sys::crc32(bytes_.data(), bytes_.size(), verified_crc_);
            read_ = 0;
            position_ += bytes_.size();
            break;
        }
        }
        return result_;
    }

  private:
    bool replaced(::geocaching::ByteView key) const
    {
        ::geocaching::storage::TransactionIndexCursor cursor;
        ::geocaching::storage::IndexedMutation change;
        if (!cursor.open(frame_, previous_, segment_first_, frame_offset_)) return true;
        while (cursor.next(change))
            if (change.table == parent_.table && change.key.size == key.size && !std::memcmp(change.key.data, key.data, key.size)) return true;
        return false;
    }
    bool writeEntry()
    {
        if (next_.length > UINT64_MAX - bytes_.size() || output_.write(bytes_.data(), bytes_.size()) != bytes_.size()) return false;
        written_crc_ = ::sys::crc32(bytes_.data(), bytes_.size(), written_crc_);
        next_.length += bytes_.size();
        return true;
    }
    IndexAppendStep fail(IndexAppendStep result)
    {
        input_.close();
        output_.close();
        lookup_.reset();
        return result_ = result;
    }
    enum class Phase : uint8_t
    {
        Output,
        Input,
        Size,
        Read,
        Latest,
        Copy,
        Changes,
        ChangeWrite,
        Flush,
        VerifyOpen,
        VerifySize,
        VerifyRead
    };
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexShardHead parent_, next_;
    ::geocaching::storage::TransactionIndexCursor changes_;
    ::geocaching::storage::IndexedMutation entry_;
    ::geocaching::storage::IndexEntryBytes bytes_{};
    ::geocaching::ByteView frame_;
    SdVolumeReadSession session_;
    ::platform::memory::PsramPtr<SdIndexLookup> lookup_;
    storage::SdRuntimeFile input_, output_;
    uint64_t previous_ = 0, segment_first_ = 0, position_ = 0, older_ = 0;
    uint32_t frame_offset_ = 0, written_crc_ = 0, verified_crc_ = 0;
    uint16_t read_ = 0;
    char slot_;
    Phase phase_ = Phase::Output;
    IndexAppendStep result_ = IndexAppendStep::Idle;
};
static_assert(sizeof(SdIndexCurrentWrite) <= 768, "Current index writes retain bounded PSRAM storage, not a shard or GPX payload");
} // namespace platform::esp::arduino_common::geocaching
