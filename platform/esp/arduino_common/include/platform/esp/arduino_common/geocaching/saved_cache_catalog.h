#pragma once
#include "platform/esp/arduino_common/geocaching/download_store.h"
#include "platform/esp/arduino_common/geocaching/sd_gpx_hash.h"
#include "ui_presentation/geocaching/geocaching_source.h"
#include <cstdio>
#include <memory>
#include <new>

namespace platform::esp::arduino_common::geocaching
{
// Four owned rows shared by the saved-list window and discovery membership
// probes. UI calls never scan storage or close files. The maintenance owner
// advances verification and resets the projection after installs/media handoff.
template <class Digest>
class SavedCacheCatalog
{
  public:
    SavedCacheCatalog(DownloadStore& store, ::geocaching::protocol::RecordCrypto& crypto)
        : store_(store), crypto_(crypto) {}
    ~SavedCacheCatalog()
    {
        if (reading_) store_.releaseRead();
    }
    void reset()
    {
        restart_ = true;
        valid_ = 0;
        invalid_ = false;
        checking_ = true;
        error_ = DownloadRecoveryRead::End;
        ++generation_;
    }
    bool pending() const { return checking_; }
    bool reading() const { return reading_; }
    uint64_t generation() const { return generation_; }
    // Maintenance-owner call before a foreground operation or media handoff.
    void releaseRead()
    {
        if (!reader_ && !reading_ && !metadata_ready_) return;
        if (reading_) store_.releaseRead();
        reading_ = metadata_ready_ = false;
        reader_.reset();
        digest_.reset();
        reset();
    }
    void requestWindow(size_t offset, size_t count)
    {
        if (!count || count > rows_.size() || (!preview_ && offset_ == offset && window_ == count)) return;
        preview_ = false;
        offset_ = offset;
        window_ = count;
        reset();
    }
    // A changed discovery page/window replaces the same four slots. The caller
    // supplies each requested ID synchronously while holding the session lock.
    bool requestPreview(uint64_t page, size_t offset, size_t count)
    {
        if (count > rows_.size() || (preview_ && page_ == page && offset_ == offset && window_ == count)) return false;
        preview_ = true;
        page_ = page;
        offset_ = offset;
        window_ = count;
        requested_ = 0;
        reset();
        return true;
    }
    void previewRow(size_t index, const std::array<uint8_t, 32>& id, const std::array<uint8_t, 32>& hash)
    {
        if (!preview_ || index >= window_) return;
        rows_[index].id = id;
        rows_[index].hash = hash;
        requested_ |= uint8_t(1u << index);
    }
    bool checked(const std::array<uint8_t, 32>& id, const std::array<uint8_t, 32>& hash) const
    {
        return preview_ && !checking_ && error_ == DownloadRecoveryRead::End && requestedRow(id, hash) < rows_.size();
    }
    bool contains(const std::array<uint8_t, 32>& id, const std::array<uint8_t, 32>& hash) const
    {
        for (size_t i = 0; i < rows_.size(); ++i)
            if ((valid_ & (1u << i)) && rows_[i].id == id && rows_[i].hash == hash) return true;
        return false;
    }
    // False yields to another storage owner or retries a temporary failure.
    bool advance()
    {
        if (restart_)
        {
            if (reading_) store_.releaseRead();
            reader_.reset();
            digest_.reset();
            reading_ = metadata_ready_ = has_after_ = false;
            cursor_ = total_ = 0;
            restart_ = false;
        }
        if (!checking_) return false;
        if (reader_)
        {
            if (reader_->step() == GpxHashStep::Reading) return true;
            std::array<uint8_t, 32> hash;
            finish(reader_->result(hash) && hash == current_.file_hash);
            return true;
        }
        if (!metadata_ready_)
        {
            if (preview_)
            {
                while (cursor_ < window_ && !(requested_ & (1u << cursor_))) ++cursor_;
                if (cursor_ == window_) return complete();
            }
            const ::geocaching::ByteView key = preview_     ? ::geocaching::ByteView{rows_[cursor_].id.data(), 32}
                                               : has_after_ ? ::geocaching::ByteView{after_.data(), 32}
                                                            : ::geocaching::ByteView{};
            const auto result = store_.readSavedCache(key, preview_, crypto_, current_);
            if (result == DownloadRecoveryRead::Pending)
            {
                reading_ = true;
                return true;
            }
            if (result == DownloadRecoveryRead::Busy) return false;
            reading_ = false;
            if (result == DownloadRecoveryRead::Unavailable) return readError(result, true);
            if (result == DownloadRecoveryRead::End)
            {
                if (!preview_) return complete();
                ++cursor_;
                return true;
            }
            if (result != DownloadRecoveryRead::Ready) return readError(result, false);
            if (preview_ && current_.hash != rows_[cursor_].hash)
            {
                ++cursor_;
                return true;
            }
            metadata_ready_ = true;
        }
        char path[112]{};
        std::snprintf(path, sizeof(path), "/trailmate/geocaching/caches/");
        size_t offset = std::strlen(path);
        constexpr char hex[] = "0123456789abcdef";
        for (auto byte : current_.id)
        {
            path[offset++] = hex[byte >> 4];
            path[offset++] = hex[byte & 15];
        }
        std::memcpy(path + offset, ".gpx", 5);
        digest_.reset(new (std::nothrow) Digest);
        if (digest_) reader_.reset(new (std::nothrow) SdGpxHash<Digest>(*digest_));
        if (!reader_)
        {
            digest_.reset();
            return readError(DownloadRecoveryRead::Unavailable, true);
        }
        if (error_ != DownloadRecoveryRead::End)
        {
            error_ = DownloadRecoveryRead::End;
            ++generation_;
        }
        if (!reader_->open(path)) finish(false);
        return true;
    }
    const char* error() const
    {
        switch (error_)
        {
        case DownloadRecoveryRead::Unavailable:
            return "Saved cache data temporarily unavailable";
        case DownloadRecoveryRead::WorkspaceTooSmall:
            return "Insufficient memory to read saved cache data";
        case DownloadRecoveryRead::IoError:
            return "Saved cache storage I/O error";
        case DownloadRecoveryRead::VolumeChanged:
            return "SD card changed; reopen Geocaching";
        case DownloadRecoveryRead::Invalid:
            return "Saved cache metadata needs recovery";
        default:
            return nullptr;
        }
    }
    void snapshot(::ui::geocaching::Snapshot& out) const
    {
        out = {};
        out.generation = generation_;
        out.can_refresh = !checking_;
        out.count = count_;
        std::snprintf(out.status.data(), out.status.size(), "%s", error() ? error() : checking_ ? "Checking saved GPX files..."
                                                                                  : invalid_    ? "Some GPX files changed or are unavailable"
                                                                                  : out.count   ? "Saved GPX - available offline"
                                                                                                : "No downloaded caches");
    }
    bool item(size_t visible_index, uint64_t generation, ::ui::geocaching::Item& out) const
    {
        out = {};
        if (preview_ || generation != generation_ || visible_index < offset_ || visible_index - offset_ >= window_) return false;
        const auto slot = visible_index - offset_;
        if (!(valid_ & (1u << slot))) return false;
        const auto& entry = rows_[slot];
        out.id = entry.id;
        out.revision_hash = entry.hash;
        out.latitude_e7 = entry.latitude_e7;
        out.longitude_e7 = entry.longitude_e7;
        out.downloaded = true;
        out.name = entry.name;
        const auto lat = entry.latitude_e7 < 0 ? -int64_t(entry.latitude_e7) : int64_t(entry.latitude_e7);
        const auto lon = entry.longitude_e7 < 0 ? -int64_t(entry.longitude_e7) : int64_t(entry.longitude_e7);
        std::snprintf(out.detail.data(), out.detail.size(), "%s%ld.%07ld, %s%ld.%07ld\nSaved GPX on SD card\nRevision %lu",
                      entry.latitude_e7 < 0 ? "-" : "", long(lat / 10000000), long(lat % 10000000),
                      entry.longitude_e7 < 0 ? "-" : "", long(lon / 10000000), long(lon % 10000000), static_cast<unsigned long>(entry.revision));
        return true;
    }

  private:
    size_t requestedRow(const std::array<uint8_t, 32>& id, const std::array<uint8_t, 32>& hash) const
    {
        for (size_t i = 0; i < window_; ++i)
            if ((requested_ & (1u << i)) && rows_[i].id == id && rows_[i].hash == hash) return i;
        return rows_.size();
    }
    bool complete()
    {
        if (!preview_) count_ = total_;
        error_ = DownloadRecoveryRead::End;
        checking_ = false;
        ++generation_;
        return true;
    }
    bool readError(DownloadRecoveryRead error, bool retry)
    {
        if (error_ != error) ++generation_;
        error_ = error;
        if (!retry)
        {
            checking_ = false;
            valid_ = 0;
        }
        return false;
    }
    void finish(bool valid)
    {
        if (valid)
        {
            const auto slot = preview_ ? requestedRow(current_.id, current_.hash) : total_ >= offset_ ? total_ - offset_
                                                                                                      : rows_.size();
            if (slot < window_)
            {
                rows_[slot] = current_;
                valid_ |= uint8_t(1u << slot);
            }
            ++total_;
        }
        else invalid_ = true;
        after_ = current_.id;
        has_after_ = true;
        metadata_ready_ = false;
        ++cursor_;
        ++generation_;
        reader_.reset();
        digest_.reset();
    }
    DownloadStore& store_;
    ::geocaching::protocol::RecordCrypto& crypto_;
    std::unique_ptr<Digest> digest_;
    std::unique_ptr<SdGpxHash<Digest>> reader_;
    std::array<uint8_t, 32> after_{};
    std::array<::geocaching::storage::SavedCacheEntry, 4> rows_{};
    ::geocaching::storage::SavedCacheRecord current_;
    uint64_t generation_ = 1, page_ = 0;
    size_t cursor_ = 0, total_ = 0, count_ = 0, offset_ = 0, window_ = 4;
    uint8_t valid_ = 0, requested_ = 0;
    DownloadRecoveryRead error_ = DownloadRecoveryRead::End;
    bool checking_ = true, restart_ = false, invalid_ = false, preview_ = false;
    bool reading_ = false, metadata_ready_ = false, has_after_ = false;
};
} // namespace platform::esp::arduino_common::geocaching
