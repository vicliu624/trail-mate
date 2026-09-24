#pragma once
#include "geocaching/storage/record_frame.h"
#include "platform/esp/arduino_common/geocaching/sd_record_reader.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include <algorithm>
#include <cstdio>

namespace platform::esp::arduino_common::geocaching
{
// One GCR1 record per step, using caller-owned storage. A failed or partial
// record is terminal for this open segment; never search ahead for another magic.
class SdJournalSegment
{
  public:
    uint64_t position() const { return offset_; }
    bool open(uint64_t first_sequence)
    {
        file_.close();
        failure_ = SegmentReadResult::Record;
        offset_ = 0;
        cursor_ = {};
        expected_ = first_sequence;
        if (!first_sequence) return false;
        char path[80]{};
        std::snprintf(path, sizeof(path), "/trailmate/geocaching/.state/journal/%016llx.gcj",
                      static_cast<unsigned long long>(first_sequence));
        if (!file_.open(path, "r")) return false;
        length_ = file_.size();
        if (length_ == 0 || length_ > 1024U * 1024U)
        {
            file_.close();
            return false;
        }
        return true;
    }

    SegmentReadResult next(uint8_t* buffer, size_t capacity, ::geocaching::storage::RecordFrameView& out)
    {
        out = {};
        if (failure_ != SegmentReadResult::Record) return failure_;
        ::geocaching::storage::RecordFrameView frame;
        const auto result = readSdRecord(file_, length_, offset_, buffer, capacity, cursor_, frame);
        if (result != SegmentReadResult::Record)
            return result == SegmentReadResult::End || result == SegmentReadResult::InProgress ? result : fail(result);
        if (frame.kind != ::geocaching::storage::RecordKind::Transaction || frame.sequence != expected_)
            return fail(SegmentReadResult::Corrupt);
        if (expected_ == UINT64_MAX && offset_ != length_) return fail(SegmentReadResult::Corrupt);
        if (expected_ != UINT64_MAX) ++expected_;
        out = frame;
        return SegmentReadResult::Record;
    }

  private:
    SegmentReadResult fail(SegmentReadResult result) { return failure_ = result; }
    storage::SdRuntimeFile file_;
    SdRecordReadCursor cursor_;
    uint64_t offset_ = 0, length_ = 0, expected_ = 0;
    SegmentReadResult failure_ = SegmentReadResult::Record;
};
} // namespace platform::esp::arduino_common::geocaching
