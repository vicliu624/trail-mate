#pragma once
#include "platform/esp/arduino_common/geocaching/sd_volume.h"
#include <cstring>

namespace platform::esp::arduino_common::geocaching
{
enum class InventoryStep : uint8_t
{
    Scanning,
    Complete,
    RetryLater,
    Corrupt,
    VolumeChanged
};

struct JournalSegmentRange
{
    bool complete = false;
    uint64_t first_start = 0;
    uint64_t last_start = 0;
};

// Filenames identify segment starts, not individual transactions. Replay must
// check sequence continuity using the records inside each segment.
class SdJournalInventory
{
  public:
    SdJournalInventory(const ::geocaching::storage::VolumeInstance& volume, uint64_t checkpoint)
        : volume_(volume), checkpoint_(checkpoint) {}
    JournalSegmentRange range() const
    {
        if (state_ != InventoryStep::Complete) return {};
        return {true, last_ ? (first_ ? first_ : checkpoint_ + 1) : 0, last_};
    }

    InventoryStep step()
    {
        if (state_ == InventoryStep::Complete || state_ == InventoryStep::Corrupt || state_ == InventoryStep::VolumeChanged) return state_;
        ::geocaching::storage::VolumeInstance current;
        const auto volume = inspectSdVolume(current);
        if (volume == SdVolumeResult::Missing || volume == SdVolumeResult::Unavailable || volume == SdVolumeResult::IoError)
            return restart();
        if (volume != SdVolumeResult::Ready) return finish(InventoryStep::Corrupt);
        if (current != volume_) return finish(InventoryStep::VolumeChanged);
        if (!directory_.is_open() && !directory_.open("/trailmate/geocaching/.state/journal")) return restart();
        char name[128]{};
        bool is_dir = false;
        const auto result = directory_.read_next_status(name, sizeof(name), &is_dir);
        if (result == storage::SdDirReadStatus::Busy) return InventoryStep::RetryLater;
        if (result == storage::SdDirReadStatus::IoError || result == storage::SdDirReadStatus::Unavailable) return restart();
        if (result == storage::SdDirReadStatus::End)
            return finish(InventoryStep::Complete);
        if (result != storage::SdDirReadStatus::Entry || is_dir || std::strlen(name) != 20 || std::strcmp(name + 16, ".gcj"))
            return finish(InventoryStep::Corrupt);
        uint64_t sequence = 0;
        for (size_t i = 0; i < 16; ++i)
        {
            const char c = name[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return finish(InventoryStep::Corrupt);
            sequence = (sequence << 4) | static_cast<uint64_t>(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        if (sequence == 0) return finish(InventoryStep::Corrupt);
        if (sequence > last_) last_ = sequence;
        const auto replay_sequence = checkpoint_ == UINT64_MAX ? UINT64_MAX : checkpoint_ + 1;
        if (sequence <= replay_sequence && sequence > first_) first_ = sequence;
        return state_ = InventoryStep::Scanning;
    }

  private:
    InventoryStep finish(InventoryStep state)
    {
        directory_.close();
        return state_ = state;
    }
    InventoryStep restart()
    {
        directory_.close();
        first_ = 0;
        last_ = 0;
        return state_ = InventoryStep::RetryLater;
    }
    const ::geocaching::storage::VolumeInstance volume_;
    const uint64_t checkpoint_;
    uint64_t first_ = 0, last_ = 0;
    storage::SdRuntimeDir directory_;
    InventoryStep state_ = InventoryStep::Scanning;
};
} // namespace platform::esp::arduino_common::geocaching
