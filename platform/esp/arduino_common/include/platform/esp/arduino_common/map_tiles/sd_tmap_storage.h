#pragma once
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "ui_map_runtime/map_tiles/tmap_map_tile_source.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>

namespace platform::esp::map_tiles
{
class SdTmapStorage final : public ui::map_tiles::TmapStorage
{
    using File = arduino_common::storage::SdRuntimeFile;
    using Dir = arduino_common::storage::SdRuntimeDir;
    using OpenStatus = arduino_common::storage::SdFileReadStatus;
    using DirStatus = arduino_common::storage::SdDirReadStatus;

  public:
    void* allocate(size_t bytes, size_t alignment) override
    {
        return heap_caps_aligned_alloc(alignment, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    void release(void* memory) override { heap_caps_free(memory); }
    uint32_t session() const override { return arduino_common::storage::sd_media_session(); }
    tmap::Status nextPackage(char* path, size_t capacity) override
    {
        if (ended_) return tmap::Status::Missing;
        if (!directory_.is_open())
        {
            const auto status = directory_.open_read_status("/maps/tmap", session());
            if (status == OpenStatus::Missing)
            {
                ended_ = true;
                return tmap::Status::Missing;
            }
            if (status == OpenStatus::Busy || status == OpenStatus::Unavailable) return tmap::Status::Busy;
            if (status != OpenStatus::Ready) return tmap::Status::IoError;
        }
        // Limit non-package enumeration too. Use caller's PSRAM pending path.
        constexpr size_t prefix = sizeof("/maps/tmap/") - 1;
        if (capacity <= prefix + 6) return tmap::Status::Invalid;
        for (unsigned visited = 0; visited < 32; ++visited)
        {
            bool is_directory = false;
            const auto status = directory_.read_next_status(path + prefix, capacity - prefix, &is_directory);
            if (status == DirStatus::Busy) return tmap::Status::Busy;
            if (status == DirStatus::End)
            {
                ended_ = true;
                directory_.close();
                return tmap::Status::Missing;
            }
            if (status != DirStatus::Entry) return tmap::Status::IoError;
            const auto length = std::strlen(path + prefix);
            if (is_directory || length < 5) continue;
            const auto* extension = path + prefix + length - 5;
            if (extension[0] != '.' || (extension[1] | 32) != 't' || (extension[2] | 32) != 'm' ||
                (extension[3] | 32) != 'a' || (extension[4] | 32) != 'p') continue;
            if (length == capacity - prefix - 1) return tmap::Status::Invalid;
            std::memcpy(path, "/maps/tmap/", prefix);
            return tmap::Status::Ok;
        }
        path[0] = 0;
        return tmap::Status::More;
    }
    tmap::Status openPackage(const char* path) override
    {
        closePackage();
        file_session_ = session();
        if (!file_.open(path, "r", file_session_)) return file_.read_busy() ? tmap::Status::Busy : tmap::Status::IoError;
        size_ = file_.size();
        if (file_.read_busy()) return tmap::Status::Busy;
        return size_ ? tmap::Status::Ok : tmap::Status::Invalid;
    }
    void closePackage() override
    {
        file_.close();
        size_ = 0;
    }
    void resetEnumeration() override
    {
        directory_.close();
        ended_ = false;
    }
    uint64_t size() const override { return size_; }
    tmap::Status readAt(uint64_t offset, uint8_t* output, size_t bytes) override
    {
        if (file_session_ != session() || arduino_common::storage::sd_external_block_owner_active()) return tmap::Status::Busy;
        if (!file_.is_open() || offset > size_ || bytes > size_ - offset) return tmap::Status::Invalid;
        if (!file_.seek(offset)) return file_.read_busy() ? tmap::Status::Busy : tmap::Status::IoError;
        size_t completed = 0;
        while (completed < bytes)
        {
            const auto batch = std::min<size_t>(4096, bytes - completed);
            const auto n = file_.read(output + completed, batch);
            if (file_.read_busy()) return tmap::Status::Busy;
            if (n <= 0 || static_cast<size_t>(n) != batch) return tmap::Status::IoError;
            completed += batch;
        }
        return file_session_ == session() ? tmap::Status::Ok : tmap::Status::Busy;
    }

  private:
    File file_;
    Dir directory_;
    uint64_t size_ = 0;
    uint32_t file_session_ = 0;
    bool ended_ = false;
};
} // namespace platform::esp::map_tiles
