#pragma once
#include "platform/linux/oneseg_reception_settings.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

namespace platform::linux_runtime
{
// Owner/consumer-thread storage. No filesystem access or allocations in append.
// The exported signed interleaved IQ8 file has a versioned little-endian header,
// fixed size limit and payload CRC. It contains raw capture, before DSP controls.
class OneSegIqArchive
{
  public:
    static constexpr size_t maximum_bytes = 32 * 1024 * 1024;
    struct Metadata
    {
        uint32_t rate = 450000, channel = 13, spi = 8700000, gain = 9;
        uint64_t created_ms = 0;
        bool full = false;
        bool valid() const
        {
            return (rate == 450000 || rate == 500000 || rate == 576000) && channel >= 13 && channel <= 62 &&
                   gain <= 13 && (spi == 8350000 || spi == 8700000 || spi == 16000000);
        }
    };
    bool begin(Metadata metadata, size_t capacity)
    {
        if (!metadata.valid() || capacity < 2 || capacity > maximum_bytes || capacity % 2) return false;
        bytes_.assign(capacity, 0);
        metadata_ = metadata;
        metadata_.full = false;
        used_ = 0;
        seen_pairs_ = 0;
        return true;
    }
    bool append(const uint8_t* data, size_t count, uint64_t first_pair)
    {
        if (bytes_.empty() || !data || !count || count % 2 || first_pair != seen_pairs_ || count / 2 > std::numeric_limits<uint64_t>::max() - seen_pairs_) return false;
        seen_pairs_ += count / 2;
        const size_t copy = std::min(count, bytes_.size() - used_);
        if (copy) std::memcpy(bytes_.data() + used_, data, copy);
        used_ += copy;
        metadata_.full = used_ == bytes_.size();
        return true;
    }
    void clear()
    {
        bytes_.clear();
        used_ = 0;
        seen_pairs_ = 0;
        metadata_ = {};
    }
    size_t size() const { return used_; }
    const uint8_t* data() const { return bytes_.data(); }
    const Metadata& metadata() const { return metadata_; }
    bool save(const std::string& path, std::string& error) const
    {
        error.clear();
        if (path.empty() || path.front() != '/' || path.find('\0') != std::string::npos || !used_ || !metadata_.valid())
            return reject(error, "No valid IQ capture or absolute output path");
        std::array<uint8_t, 64> header{};
        std::memcpy(header.data(), "TM1IQ8\r\n", 8);
        put(header.data() + 8, 1, 4);
        put(header.data() + 12, metadata_.rate, 4);
        put(header.data() + 16, metadata_.channel, 4);
        put(header.data() + 20, metadata_.spi, 4);
        put(header.data() + 24, metadata_.gain, 4);
        put(header.data() + 28, metadata_.full ? 1 : 0, 4);
        put(header.data() + 32, used_, 8);
        put(header.data() + 40, crc(bytes_.data(), used_), 4);
        put(header.data() + 48, metadata_.created_ms, 8);
        std::string temporary = path + ".pending-" + std::to_string(getpid());
        const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) return reject(error, "Cannot create exclusive IQ archive staging file");
        bool ok = writeAll(fd, header.data(), header.size()) && writeAll(fd, bytes_.data(), used_) && ::fsync(fd) == 0;
        if (::close(fd) != 0) ok = false;
        // Atomic publication without replacing another recording, also on FAT.
        if (ok) ok = ::syscall(SYS_renameat2, AT_FDCWD, temporary.c_str(), AT_FDCWD, path.c_str(), 1u) == 0;
        if (!ok)
        {
            ::unlink(temporary.c_str());
            return reject(error, "Cannot publish IQ archive; destination must be new and writable");
        }
        return true;
    }
    bool load(const std::string& path, std::string& error)
    {
        error.clear();
        clear();
        if (path.empty() || path.front() != '/' || path.find('\0') != std::string::npos)
            return reject(error, "IQ archive path must be absolute");
        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (fd < 0) return reject(error, "Cannot open IQ archive");
        struct stat info
        {
        };
        std::array<uint8_t, 64> header{};
        bool ok = ::fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 66 &&
                  info.st_size <= static_cast<off_t>(maximum_bytes + 64) && readAll(fd, header.data(), header.size());
        const uint64_t count = get(header.data() + 32, 8);
        if (ok)
        {
            metadata_.rate = get(header.data() + 12, 4);
            metadata_.channel = get(header.data() + 16, 4);
            metadata_.spi = get(header.data() + 20, 4);
            metadata_.gain = get(header.data() + 24, 4);
            metadata_.full = get(header.data() + 28, 4) != 0;
            metadata_.created_ms = get(header.data() + 48, 8);
            ok = !std::memcmp(header.data(), "TM1IQ8\r\n", 8) && get(header.data() + 8, 4) == 1 &&
                 get(header.data() + 28, 4) <= 1 && get(header.data() + 44, 4) == 0 && get(header.data() + 56, 8) == 0 &&
                 metadata_.valid() && count >= 2 && count <= maximum_bytes && count % 2 == 0 &&
                 count + 64 == static_cast<uint64_t>(info.st_size);
        }
        if (ok)
        {
            try
            {
                bytes_.resize(static_cast<size_t>(count));
            }
            catch (...)
            {
                ::close(fd);
                clear();
                return reject(error, "Cannot allocate bounded IQ replay storage");
            }
            ok = readAll(fd, bytes_.data(), bytes_.size()) && crc(bytes_.data(), bytes_.size()) == get(header.data() + 40, 4);
        }
        ::close(fd);
        if (!ok)
        {
            clear();
            return reject(error, "Invalid, truncated or corrupted IQ archive");
        }
        used_ = bytes_.size();
        seen_pairs_ = used_ / 2;
        return true;
    }

  private:
    Metadata metadata_;
    std::vector<uint8_t> bytes_;
    size_t used_ = 0;
    uint64_t seen_pairs_ = 0;
    static bool reject(std::string& error, const char* message)
    {
        error = message;
        return false;
    }
    static void put(uint8_t* output, uint64_t value, unsigned bytes)
    {
        for (unsigned i = 0; i < bytes; ++i) output[i] = static_cast<uint8_t>(value >> (8 * i));
    }
    static uint64_t get(const uint8_t* input, unsigned bytes)
    {
        uint64_t value = 0;
        for (unsigned i = 0; i < bytes; ++i) value |= uint64_t(input[i]) << (8 * i);
        return value;
    }
    static uint32_t crc(const uint8_t* data, size_t count)
    {
        static const auto table = []
        {
            std::array<uint32_t, 256> result{};
            for (unsigned i = 0; i < result.size(); ++i)
            {
                uint32_t value = i;
                for (unsigned bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0);
                result[i] = value;
            }
            return result;
        }();
        uint32_t value = 0xffffffffu;
        for (size_t i = 0; i < count; ++i) value = table[(value ^ data[i]) & 255] ^ (value >> 8);
        return value ^ 0xffffffffu;
    }
    static bool writeAll(int fd, const uint8_t* data, size_t count)
    {
        while (count)
        {
            const auto size = ::write(fd, data, count);
            if (size < 0 && errno == EINTR) continue;
            if (size <= 0) return false;
            data += size;
            count -= static_cast<size_t>(size);
        }
        return true;
    }
    static bool readAll(int fd, uint8_t* data, size_t count)
    {
        while (count)
        {
            const auto size = ::read(fd, data, count);
            if (size < 0 && errno == EINTR) continue;
            if (size <= 0) return false;
            data += size;
            count -= static_cast<size_t>(size);
        }
        return true;
    }
};
} // namespace platform::linux_runtime
