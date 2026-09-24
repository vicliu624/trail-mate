#pragma once
#include "platform/esp/arduino_common/geocaching/sd_index_cleanup.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_recovery.h"
#include <memory>

namespace platform::esp::arduino_common::geocaching
{
// A failed derived index is archived, never confused with authoritative state.
// Existing checkpoint import and validated journal replay rebuild the normal
// tree. No business write is permitted until the rebuilt root passes recovery
// and the archive is retired. All large buffers remain caller leases.
template <class Digest>
class SdIndexRepair
{
  public:
    SdIndexRepair(const ::geocaching::storage::VolumeInstance& volume,
                  ::geocaching::storage::IndexRootBytes& first, ::geocaching::storage::IndexRootBytes& second,
                  uint8_t* frame, size_t capacity, uint8_t* validation_frame, size_t validation_capacity,
                  ::geocaching::storage::MutationView* mutations, size_t mutation_capacity)
        : volume_(volume), roots_{&first, &second}, frame_(frame), capacity_(capacity), validation_frame_(validation_frame),
          validation_capacity_(validation_capacity), mutations_(mutations), mutation_capacity_(mutation_capacity)
    {
        if (!frame || !validation_frame || !mutations || !mutation_capacity || mutation_capacity > 64 || capacity < 24 || validation_capacity < 24)
        {
            result_ = IndexedRecoveryStep::RecoveryRequired;
            return;
        }
        const ::geocaching::ByteView leases[] = {{first.data(), first.size()}, {second.data(), second.size()}, {frame, capacity}, {validation_frame, validation_capacity}, {reinterpret_cast<const uint8_t*>(mutations), mutation_capacity * sizeof(*mutations)}};
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
        if (phase_ == Phase::Recover)
        {
            const auto status = recovery_->step();
            if (status == IndexedRecoveryStep::Working) return result_;
            if (status == IndexedRecoveryStep::Restored)
            {
                if (!recovery_->selected(root_, copy_) || root_.sequence < floor_) return finish(IndexedRecoveryStep::RecoveryRequired);
                recovery_.reset();
                if (!archive_present_) return finish(IndexedRecoveryStep::Restored);
                return clean(true);
            }
            recovery_.reset();
            if (status != IndexedRecoveryStep::RecoveryRequired || attempted_) return finish(status);
            // Probe again because first initialization or replay may have
            // produced a partial tree before detecting the failure.
            pending_repair_ = true;
            index_present_ = archive_present_ = false;
            phase_ = Phase::OpenState;
            return result_;
        }
        if (phase_ == Phase::CleanPrimary || phase_ == Phase::CleanArchive)
        {
            const auto status = cleanup_->step();
            if (status == IndexCleanupStep::Working) return result_;
            if (status != IndexCleanupStep::Complete)
                return finish(status == IndexCleanupStep::VolumeChanged ? IndexedRecoveryStep::VolumeChanged : status == IndexCleanupStep::IoError ? IndexedRecoveryStep::IoError
                                                                                                                                                   : IndexedRecoveryStep::RecoveryRequired);
            cleanup_.reset();
            if (phase_ == Phase::CleanArchive) return finish(IndexedRecoveryStep::Restored);
            index_present_ = false;
            return recover(true);
        }
        VolumeInstance current;
        if (inspectSdVolume(current) != SdVolumeResult::Ready) return finish(IndexedRecoveryStep::IoError);
        if (current != volume_) return finish(IndexedRecoveryStep::VolumeChanged);
        switch (phase_)
        {
        case Phase::OpenState:
            if (!directory_.open("/trailmate/geocaching/.state")) return finish(IndexedRecoveryStep::IoError);
            phase_ = Phase::ReadState;
            return result_;
        case Phase::ReadState:
        {
            char name[128]{};
            bool is_directory = false;
            const auto status = directory_.read_next_status(name, sizeof(name), &is_directory);
            if (status == storage::SdDirReadStatus::Entry)
            {
                const bool index = !std::strcmp(name, "index"), archive = !std::strcmp(name, "index.repair");
                if ((index || archive) && !is_directory) return finish(IndexedRecoveryStep::RecoveryRequired);
                index_present_ |= index;
                archive_present_ |= archive;
                return result_;
            }
            if (status != storage::SdDirReadStatus::End) return finish(IndexedRecoveryStep::IoError);
            directory_.close();
            position_ = 0;
            phase_ = Phase::ReadRoots;
            return result_;
        }
        case Phase::ReadRoots:
        {
            if (position_ < 4)
            {
                const unsigned position = position_++;
                if (position < 2 ? !index_present_ : !archive_present_) return result_;
                char path[80];
                std::snprintf(path, sizeof(path), "/trailmate/geocaching/.state/%s/root.h%u", position < 2 ? "index" : "index.repair", position % 2);
                auto& bytes = *roots_[0];
                const auto read = storage::sd_read_file(path, bytes.data(), bytes.size());
                if (!readable(read.status)) return finish(IndexedRecoveryStep::IoError);
                IndexRootView root;
                if (read.status == storage::SdFileReadStatus::Ready && read.file_size == bytes.size() && read.bytes_read == bytes.size() && decodeIndexRoot({bytes.data(), bytes.size()}, volume_, root))
                {
                    have_floor_ = true;
                    if (root.sequence > floor_) floor_ = root.sequence;
                }
                return result_;
            }
            position_ = 0;
            phase_ = Phase::ReadFloors;
            return result_;
        }
        case Phase::ReadFloors:
        {
            if (archive_present_ && !position_)
            {
                if (!directory_.open("/trailmate/geocaching/.state/index.repair")) return finish(IndexedRecoveryStep::IoError);
                position_ = 1;
                return result_;
            }
            if (archive_present_ && position_ == 1)
            {
                char name[128]{};
                bool is_directory = false;
                const auto status = directory_.read_next_status(name, sizeof(name), &is_directory);
                if (status != storage::SdDirReadStatus::Entry && status != storage::SdDirReadStatus::End) return finish(IndexedRecoveryStep::IoError);
                if (status == storage::SdDirReadStatus::End)
                {
                    directory_.close();
                    position_ = 2;
                    return result_;
                }
                uint64_t sequence = 0;
                if (!std::strncmp(name, "gcf1-", 5))
                {
                    if (is_directory || !decodeIndexRepairFloorName(volume_, name, sequence)) return finish(IndexedRecoveryStep::RecoveryRequired);
                    stored_floor_valid_ = true;
                    if (sequence > stored_floor_) stored_floor_ = sequence;
                    have_floor_ = true;
                    if (sequence > floor_) floor_ = sequence;
                }
                return result_;
            }
            // An unrecognizable pre-existing index must not become an empty
            // database merely because every authoritative input is missing.
            if (!have_floor_ && (index_present_ || archive_present_) && !floor_) floor_ = 1;
            if (!pending_repair_) return recover(false);
            if (!archive_present_)
            {
                if (!index_present_) return finish(IndexedRecoveryStep::RecoveryRequired);
                phase_ = Phase::Archive;
                return result_;
            }
            return saveFloor();
        }
        case Phase::Archive:
            if (!storage::sd_rename("/trailmate/geocaching/.state/index", "/trailmate/geocaching/.state/index.repair")) return finish(IndexedRecoveryStep::IoError);
            index_present_ = false;
            archive_present_ = true;
            return saveFloor();
        case Phase::FloorOpen:
            if (!floor_file_.open(floor_path_, "w")) return finish(IndexedRecoveryStep::IoError);
            phase_ = Phase::FloorFlush;
            return result_;
        case Phase::FloorFlush:
            if (!floor_file_.flush()) return finish(IndexedRecoveryStep::IoError);
            floor_file_.close();
            phase_ = Phase::FloorVerify;
            return result_;
        case Phase::FloorVerify:
        {
            uint8_t probe;
            const auto read = storage::sd_read_file(floor_path_, &probe, 1);
            if (read.status != storage::SdFileReadStatus::Ready || read.file_size || read.bytes_read) return finish(IndexedRecoveryStep::IoError);
            return index_present_ ? clean(false) : recover(true);
        }
        default:
            return finish(IndexedRecoveryStep::RecoveryRequired);
        }
    }

  private:
    static bool readable(storage::SdFileReadStatus status)
    {
        return status == storage::SdFileReadStatus::Ready || status == storage::SdFileReadStatus::Missing || status == storage::SdFileReadStatus::Invalid;
    }
    IndexedRecoveryStep saveFloor()
    {
        if (stored_floor_valid_ && stored_floor_ >= floor_) return index_present_ ? clean(false) : recover(true);
        char name[32];
        indexRepairFloorName(volume_, floor_, name);
        std::snprintf(floor_path_, sizeof(floor_path_), "/trailmate/geocaching/.state/index.repair/%s", name);
        phase_ = Phase::FloorOpen;
        return result_;
    }
    IndexedRecoveryStep recover(bool rebuilding)
    {
        attempted_ |= rebuilding;
        recovery_.reset(new (std::nothrow) SdIndexedRecovery<Digest>(volume_, *roots_[0], *roots_[1], frame_, capacity_, validation_frame_, validation_capacity_, mutations_, mutation_capacity_));
        if (!recovery_) return finish(IndexedRecoveryStep::OutOfMemory);
        phase_ = Phase::Recover;
        return result_;
    }
    IndexedRecoveryStep clean(bool archive)
    {
        cleanup_.reset(new (std::nothrow) SdIndexCleanup(volume_, archive));
        if (!cleanup_) return finish(IndexedRecoveryStep::OutOfMemory);
        phase_ = archive ? Phase::CleanArchive : Phase::CleanPrimary;
        return result_;
    }
    IndexedRecoveryStep finish(IndexedRecoveryStep status)
    {
        recovery_.reset();
        cleanup_.reset();
        directory_.close();
        floor_file_.close();
        return result_ = status;
    }
    enum class Phase : uint8_t
    {
        OpenState,
        ReadState,
        ReadRoots,
        ReadFloors,
        Recover,
        Archive,
        FloorOpen,
        FloorFlush,
        FloorVerify,
        CleanPrimary,
        CleanArchive
    };
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootBytes* roots_[2];
    ::geocaching::storage::IndexRootView root_;
    uint8_t* frame_;
    size_t capacity_;
    uint8_t* validation_frame_;
    size_t validation_capacity_;
    ::geocaching::storage::MutationView* mutations_;
    size_t mutation_capacity_;
    std::unique_ptr<SdIndexedRecovery<Digest>> recovery_;
    std::unique_ptr<SdIndexCleanup> cleanup_;
    storage::SdRuntimeDir directory_;
    storage::SdRuntimeFile floor_file_;
    char floor_path_[80]{};
    uint64_t floor_ = 0, stored_floor_ = 0;
    unsigned copy_ = 0;
    uint8_t position_ = 0;
    bool stored_floor_valid_ = false, have_floor_ = false, index_present_ = false, archive_present_ = false, pending_repair_ = false, attempted_ = false;
    Phase phase_ = Phase::OpenState;
    IndexedRecoveryStep result_ = IndexedRecoveryStep::Working;
};
} // namespace platform::esp::arduino_common::geocaching
