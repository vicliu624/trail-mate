#pragma once
#include "platform/linux/hostlink_upload_io.h"
#include "tmap/memory.h"
#include "tmap/route_archive.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace platform::linux_runtime
{
// Shared native adapters for map search and routing. Files remain descriptor-
// rooted to one configured media identity; no LVGL or board dependency enters.
class TmapMemory final : public tmap::MemoryProvider
{
  public:
    bool hasExternalMemory() const override { return false; }
    void* allocate(size_t bytes, size_t alignment, tmap::MemoryRegion region) override
    {
        if (!bytes || region != tmap::MemoryRegion::Internal) return nullptr;
        void* pointer = nullptr;
        alignment = std::max(alignment, sizeof(void*));
        return posix_memalign(&pointer, alignment, bytes) == 0 ? pointer : nullptr;
    }
    void release(void* pointer, size_t, size_t, tmap::MemoryRegion) override { std::free(pointer); }
};

class TmapFile final : public tmap::WritableRouteFile
{
  public:
    explicit TmapFile(const std::filesystem::path& root) : io_(root) {}
    bool open(const std::string& logical, bool create = false)
    {
        close();
        if (!io_.open(logical.c_str(), create ? UploadIo::Mode::Exclusive : UploadIo::Mode::Read).handle) return false;
        logical_ = logical;
        writable_ = create;
        if ((create && !io_.lock_open(&io_)) || !io_.size(&io_, length_))
        {
            close();
            if (create) io_.remove(logical.c_str());
            return false;
        }
        return true;
    }
    void close()
    {
        io_.close(&io_);
        logical_.clear();
        length_ = 0;
        writable_ = false;
    }
    bool current() { return !logical_.empty() && io_.current() && io_.still_named(logical_.c_str()); }
    bool flush() { return current() && io_.sync(&io_); }
    uint64_t size() const override { return length_; }
    tmap::Status readAt(uint64_t offset, uint8_t* output, size_t count) override
    {
        if (!current()) return tmap::Status::IoError;
        if (offset > length_ || count > length_ - offset) return tmap::Status::Invalid;
        if (!io_.seek(&io_, offset)) return tmap::Status::IoError;
        size_t done = 0;
        while (done < count)
        {
            const auto read = io_.read(&io_, output + done, count - done);
            if (!read) return tmap::Status::IoError;
            done += read;
        }
        return tmap::Status::Ok;
    }
    tmap::Status writeAt(uint64_t offset, const uint8_t* bytes, size_t count) override
    {
        constexpr uint64_t maximum = 8 * 1024 * 1024;
        if (!writable_ || !current()) return tmap::Status::IoError;
        if (offset > maximum || count > maximum - offset) return tmap::Status::Invalid;
        if (!io_.seek(&io_, offset) || io_.write(&io_, bytes, count) != count) return tmap::Status::IoError;
        length_ = std::max(length_, offset + count);
        return tmap::Status::Ok;
    }
    bool clear()
    {
        if (!writable_ || !current() || !io_.truncate(&io_, 0)) return false;
        length_ = 0;
        return true;
    }
    bool publish(const std::string& destination)
    {
        if (!current() || !flush() || !io_.publish(logical_.c_str(), destination.c_str())) return false;
        logical_ = destination;
        return true;
    }
    void removeOwned()
    {
        if (current()) io_.remove(logical_.c_str());
        close();
    }
    UploadIo& io() { return io_; }

  private:
    UploadIo io_;
    std::string logical_;
    uint64_t length_ = 0;
    bool writable_ = false;
};

class TmapCatalog
{
  public:
    explicit TmapCatalog(std::filesystem::path root, bool routing = false) : root_(std::move(root)), routing_(routing) {}
    tmap::Status next(std::string& logical)
    {
        // Match the production SD layout. Routing-only roots do not make a
        // raster/POI package appear twice. Enumeration work is bounded per call.
        static constexpr const char* roots[] = {"/maps/tmap/osm", "/maps/tmap/terrain", "/maps/tmap/satellite", "/maps/tmap", "/maps/navigation", "/maps/tmap/navigation"};
        logical.clear();
        const size_t count = routing_ ? 6 : 4;
        std::error_code error;
        for (unsigned work = 0; work < 32 && index_ < count; ++work)
        {
            if (!opened_)
            {
                const auto relative = std::filesystem::path(roots[index_] + 1);
                auto directory = root_;
                bool missing = false;
                for (const auto& part : relative)
                {
                    directory /= part;
                    const auto status = std::filesystem::symlink_status(directory, error);
                    if (error == std::errc::no_such_file_or_directory || status.type() == std::filesystem::file_type::not_found)
                    {
                        missing = true;
                        error.clear();
                        break;
                    }
                    if (error || !std::filesystem::is_directory(status)) return tmap::Status::IoError;
                }
                if (missing)
                {
                    ++index_;
                    continue;
                }
                iterator_ = std::filesystem::directory_iterator(directory, error);
                if (error) return tmap::Status::IoError;
                opened_ = true;
            }
            if (iterator_ == std::filesystem::directory_iterator{})
            {
                opened_ = false;
                ++index_;
                continue;
            }
            const auto entry = *iterator_;
            iterator_.increment(error);
            if (error || ++visited_ > 8192) return tmap::Status::IoError;
            if (!entry.is_regular_file(error) || error || entry.is_symlink(error) || error) continue;
            if (entry.hard_link_count(error) != 1 || error) continue;
            const auto name = entry.path().filename().string();
            if (name.empty() || name.front() == '.') continue;
            auto extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char byte)
                           { return char(std::tolower(byte)); });
            if (extension != ".tmap") continue;
            if (++packages_ > 256) return tmap::Status::Invalid;
            logical = std::string(roots[index_]) + '/' + name;
            return tmap::Status::Ok;
        }
        return index_ == count ? tmap::Status::Missing : tmap::Status::More;
    }

  private:
    std::filesystem::path root_;
    std::filesystem::directory_iterator iterator_;
    size_t index_ = 0, visited_ = 0, packages_ = 0;
    bool routing_ = false, opened_ = false;
};
} // namespace platform::linux_runtime
