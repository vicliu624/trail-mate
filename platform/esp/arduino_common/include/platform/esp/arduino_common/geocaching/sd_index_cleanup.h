#pragma once
#include "geocaching/storage/index_root.h"
#include "platform/esp/arduino_common/geocaching/sd_index_repair_floor.h"
#include "platform/esp/arduino_common/geocaching/sd_volume.h"
#include <cstdio>
#include <cstring>

namespace platform::esp::arduino_common::geocaching
{
enum class IndexCleanupStep : uint8_t
{
    Working,
    Complete,
    Invalid,
    IoError,
    VolumeChanged
};

// Only derived index trees are accepted. One directory handle and one path,
// never a recursive traversal or an in-memory list of shard files. The owner
// must retain a recoverable authoritative baseline before calling this class.
class SdIndexCleanup
{
  public:
    SdIndexCleanup(const ::geocaching::storage::VolumeInstance& volume, bool archive)
        : volume_(volume), archive_(archive)
    {
        std::snprintf(path_, sizeof(path_), "/trailmate/geocaching/.state/%s", archive ? "index.repair" : "index");
    }
    // The caller holds an exclusive maintenance lease and has durably
    // published both supplied roots. Only their unused opposite slot is
    // removable; root metadata and the current slot are never traversed.
    SdIndexCleanup(const ::geocaching::storage::VolumeInstance& volume,
                   const ::geocaching::storage::IndexRootView& first,
                   const ::geocaching::storage::IndexRootView& second)
        : volume_(volume), depth_(1), base_depth_(1), archive_(false)
    {
        ::geocaching::storage::IndexRootView selected;
        if (!::geocaching::storage::selectIndexRoot(first, second, selected) || first.slot != second.slot)
        {
            result_ = IndexCleanupStep::Invalid;
            return;
        }
        std::snprintf(path_, sizeof(path_), "/trailmate/geocaching/.state/index/%c", selected.slot == 'a' ? 'b' : 'a');
    }
    IndexCleanupStep step()
    {
        if (result_ != IndexCleanupStep::Working) return result_;
        ::geocaching::storage::VolumeInstance current;
        if (inspectSdVolume(current) != SdVolumeResult::Ready) return finish(IndexCleanupStep::IoError);
        if (current != volume_) return finish(IndexCleanupStep::VolumeChanged);
        if (phase_ == Phase::RemoveFile)
        {
            if (!storage::sd_remove(path_)) return finish(IndexCleanupStep::IoError);
            path_[parent_length_] = 0;
            phase_ = Phase::Open;
            return result_;
        }
        if (phase_ == Phase::RemoveDirectory)
        {
            if (!storage::sd_rmdir(path_)) return finish(IndexCleanupStep::IoError);
            if (depth_ == base_depth_) return finish(IndexCleanupStep::Complete);
            *std::strrchr(path_, '/') = 0;
            --depth_;
            phase_ = Phase::Open;
            return result_;
        }
        if (phase_ == Phase::Open)
        {
            if (base_depth_ && depth_ == base_depth_ && !storage::sd_exists(path_)) return finish(IndexCleanupStep::Complete);
            if (!directory_.open(path_)) return finish(IndexCleanupStep::IoError);
            phase_ = Phase::Read;
            return result_;
        }
        char name[32]{};
        bool is_directory = false;
        const auto status = directory_.read_next_status(name, sizeof(name), &is_directory);
        if (status == storage::SdDirReadStatus::End)
        {
            directory_.close();
            phase_ = Phase::RemoveDirectory;
            return result_;
        }
        if (status != storage::SdDirReadStatus::Entry)
            return finish(status == storage::SdDirReadStatus::Invalid ? IndexCleanupStep::Invalid : IndexCleanupStep::IoError);
        if (!allowed(name, is_directory)) return finish(IndexCleanupStep::Invalid);
        directory_.close();
        parent_length_ = std::strlen(path_);
        const auto added = std::snprintf(path_ + parent_length_, sizeof(path_) - parent_length_, "/%s", name);
        if (added <= 0 || static_cast<size_t>(added) >= sizeof(path_) - parent_length_) return finish(IndexCleanupStep::Invalid);
        if (is_directory)
        {
            ++depth_;
            phase_ = Phase::Open;
        }
        else phase_ = Phase::RemoveFile;
        return result_;
    }

  private:
    static bool hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
    bool allowed(const char* name, bool directory) const
    {
        const auto length = std::strlen(name);
        uint64_t floor = 0;
        if (!depth_)
            return directory ? length == 1 && (name[0] == 'a' || name[0] == 'b')
                             : !std::strcmp(name, "root.h0") || !std::strcmp(name, "root.h1") ||
                                   (archive_ && decodeIndexRepairFloorName(volume_, name, floor));
        if (depth_ == 1)
            return directory && length == 2 && name[0] == '0' && name[1] != '0' && hex(name[1]) && name[1] <= 'd';
        if (depth_ != 2 || directory || (length != 6 && length != 9) || !hex(name[0]) || !hex(name[1]) || std::strncmp(name + 2, ".gci", 4)) return false;
        return length == 6 || !std::strcmp(name + 6, ".h0") || !std::strcmp(name + 6, ".h1");
    }
    IndexCleanupStep finish(IndexCleanupStep result)
    {
        directory_.close();
        return result_ = result;
    }
    enum class Phase : uint8_t
    {
        Open,
        Read,
        RemoveFile,
        RemoveDirectory
    };
    ::geocaching::storage::VolumeInstance volume_;
    storage::SdRuntimeDir directory_;
    char path_[96]{};
    size_t parent_length_ = 0;
    uint8_t depth_ = 0;
    uint8_t base_depth_ = 0;
    bool archive_;
    Phase phase_ = Phase::Open;
    IndexCleanupStep result_ = IndexCleanupStep::Working;
};
static_assert(sizeof(SdIndexCleanup) <= 160, "Index cleanup retains only a bounded path and directory cursor");
} // namespace platform::esp::arduino_common::geocaching
