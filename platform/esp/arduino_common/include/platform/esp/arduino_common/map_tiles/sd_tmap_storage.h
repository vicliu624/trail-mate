#pragma once
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "ui_map_runtime/map_tiles/tmap_map_tile_source.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>

namespace platform::esp::map_tiles
{
class SdTmapStorage final : public ui::map_tiles::TmapStorage
{
    using File = arduino_common::storage::SdRuntimeFile;
    using Dir = arduino_common::storage::SdRuntimeDir;
    using OpenStatus = arduino_common::storage::SdFileReadStatus;
    using DirStatus = arduino_common::storage::SdDirReadStatus;

  public:
    ~SdTmapStorage() override { closePackage(); }
    void* allocate(size_t bytes, size_t alignment) override
    {
        return heap_caps_aligned_alloc(alignment, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    void release(void* memory) override { heap_caps_free(memory); }
    uint32_t session() const override { return arduino_common::storage::sd_media_session(); }
    uint32_t nowMs() const override { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
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
        // Catalog/header retries must not reopen the same immutable file and
        // discard its partial page transfer on every Busy response.
        if (!path || std::strlen(path) >= sizeof(current_path_)) return tmap::Status::Invalid;
        if (file_.is_open() && size_ && file_session_ == session() && std::strcmp(current_path_, path) == 0)
            return tmap::Status::Ok;
        file_.close();
        size_ = 0;
        position_valid_ = false;
        raw_output_ = nullptr;
        if (blocks_)
            for (auto& block : blocks_->pages) block.bytes = 0;
        file_session_ = session();
        if (!file_.open(path, "r", file_session_)) return file_.read_busy() ? tmap::Status::Busy : tmap::Status::IoError;
        size_ = file_.size();
        if (file_.read_busy()) return tmap::Status::Busy;
        if (size_) std::strcpy(current_path_, path);
        return size_ ? tmap::Status::Ok : tmap::Status::Invalid;
    }
    void closePackage() override
    {
        file_.close();
        size_ = 0;
        current_path_[0] = 0;
        position_valid_ = false;
        raw_output_ = nullptr;
        if (blocks_)
        {
            blocks_->~Blocks();
            release(blocks_);
            blocks_ = nullptr;
        }
    }
    void resetEnumeration() override
    {
        directory_.close();
        ended_ = false;
    }
    uint64_t size() const override { return size_; }
    void cancelTransfers() { raw_output_ = nullptr; }
    tmap::Status readAt(uint64_t offset, uint8_t* output, size_t bytes) override
    {
        if (file_session_ != session() || arduino_common::storage::sd_external_block_owner_active()) return tmap::Status::Busy;
        if (!file_.is_open() || offset > size_ || bytes > size_ - offset) return tmap::Status::Invalid;
        if (bytes > 4096)
        {
            // The worker retains this command and borrowed PSRAM scratch on
            // More. No second raster buffer is allocated, and Busy does not
            // force rereading already completed pixel ranges.
            if (raw_output_ != output || raw_offset_ != offset || raw_bytes_ != bytes)
            {
                raw_output_ = output;
                raw_offset_ = offset;
                raw_bytes_ = bytes;
                raw_completed_ = 0;
            }
            for (unsigned batch = 0; batch < 4 && raw_completed_ < bytes; ++batch)
            {
                const auto at = offset + raw_completed_;
                if (!seekIfNeeded(at)) return file_.read_busy() ? tmap::Status::More : tmap::Status::IoError;
                const auto requested = std::min<size_t>(4096, bytes - raw_completed_);
                const int n = file_.read(output + raw_completed_, requested);
                if (n > 0 && static_cast<size_t>(n) <= requested) raw_completed_ += static_cast<size_t>(n);
                position_valid_ = n > 0 && !file_.read_busy();
                position_ = at + (n > 0 ? static_cast<size_t>(n) : 0);
                if (file_.read_busy()) return tmap::Status::More;
                if (n <= 0 || static_cast<size_t>(n) > requested)
                {
                    raw_output_ = nullptr;
                    return tmap::Status::IoError;
                }
            }
            if (raw_completed_ < bytes) return tmap::Status::More;
            raw_output_ = nullptr;
        }
        else if (bytes)
        {
            // Two fixed PSRAM blocks preserve partial index-page reads across
            // Busy and share completed pages without reissuing physical I/O.
            if (!blocks_)
            {
                void* memory = allocate(sizeof(Blocks), alignof(Blocks));
                if (!memory) return tmap::Status::IoError;
                blocks_ = new (memory) Blocks{};
            }
            Block* selected = nullptr;
            for (auto& block : blocks_->pages)
                if (block.bytes == bytes && block.offset == offset) selected = &block;
            if (!selected)
            {
                selected = &*std::min_element(std::begin(blocks_->pages), std::end(blocks_->pages),
                                              [](const auto& a, const auto& b)
                                              {
                                                  const unsigned pa = !a.bytes ? 0 : a.completed == a.bytes ? 1
                                                                                                            : 2;
                                                  const unsigned pb = !b.bytes ? 0 : b.completed == b.bytes ? 1
                                                                                                            : 2;
                                                  return pa != pb ? pa < pb : a.age < b.age;
                                              });
                selected->offset = offset;
                selected->bytes = bytes;
                selected->completed = 0;
            }
            selected->age = ++blocks_->age;
            while (selected->completed < bytes)
            {
                const auto at = offset + selected->completed;
                if (!seekIfNeeded(at)) return file_.read_busy() ? tmap::Status::Busy : tmap::Status::IoError;
                const auto requested = std::min<size_t>(512, bytes - selected->completed);
                const int n = file_.read(selected->data + selected->completed, requested);
                if (n > 0 && static_cast<size_t>(n) <= requested) selected->completed += static_cast<size_t>(n);
                position_valid_ = n > 0 && !file_.read_busy();
                position_ = at + (n > 0 ? static_cast<size_t>(n) : 0);
                if (file_.read_busy()) return tmap::Status::Busy;
                if (n <= 0 || static_cast<size_t>(n) > requested) return tmap::Status::IoError;
            }
            std::memcpy(output, selected->data, bytes);
        }
        return file_session_ == session() ? tmap::Status::Ok : tmap::Status::Busy;
    }

  private:
    struct Block
    {
        uint8_t data[4096]{};
        uint64_t offset = 0;
        size_t bytes = 0, completed = 0;
        uint32_t age = 0;
    };
    struct Blocks
    {
        Block pages[2]{};
        uint32_t age = 0;
    };
    bool seekIfNeeded(uint64_t offset)
    {
        if (position_valid_ && position_ == offset) return true;
        const bool ok = file_.seek(offset);
        position_valid_ = ok;
        position_ = offset;
        return ok;
    }
    Blocks* blocks_ = nullptr;
    char current_path_[192]{};
    uint8_t* raw_output_ = nullptr;
    uint64_t raw_offset_ = 0, position_ = 0;
    size_t raw_bytes_ = 0, raw_completed_ = 0;
    bool position_valid_ = false;
    File file_;
    Dir directory_;
    uint64_t size_ = 0;
    uint32_t file_session_ = 0;
    bool ended_ = false;
};
} // namespace platform::esp::map_tiles
