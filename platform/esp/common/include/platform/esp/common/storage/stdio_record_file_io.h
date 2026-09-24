#pragma once
#include "platform/esp/common/storage/record_file_io.h"
#include <cerrno>
#include <cstdio>
#include <limits>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace platform::esp::storage
{
// Explicit native filesystem backend for host recovery tests. Not a fallback
// selected in response to an SD error.
class StdioRecordFileIo final : public RecordFileIo
{
  public:
    OpenResult open(const char* path, Mode mode) override
    {
        auto* file = std::fopen(path, mode == Mode::Read ? "rb" : mode == Mode::Update ? "r+b"
                                                                                       : "wb");
        return {file, file ? OpenStatus::Ready : errno == ENOENT ? OpenStatus::Missing
                                                                 : OpenStatus::IoError};
    }
    bool close(Handle file) override { return std::fclose(static_cast<std::FILE*>(file)) == 0; }
    bool size(Handle file, uint64_t& bytes) override
    {
        auto* stream = static_cast<std::FILE*>(file);
        bytes = 0;
        if (std::fseek(stream, 0, SEEK_END)) return false;
        const auto end = std::ftell(stream);
        if (end < 0) return false;
        bytes = static_cast<uint64_t>(end);
        return true;
    }
    bool seek(Handle file, uint64_t offset) override
    {
        return offset <= static_cast<uint64_t>(std::numeric_limits<long>::max()) &&
               std::fseek(static_cast<std::FILE*>(file), static_cast<long>(offset), SEEK_SET) == 0;
    }
    std::size_t read(Handle file, void* buffer, std::size_t bytes) override
    {
        return std::fread(buffer, 1, bytes, static_cast<std::FILE*>(file));
    }
    std::size_t write(Handle file, const void* buffer, std::size_t bytes) override
    {
        return std::fwrite(buffer, 1, bytes, static_cast<std::FILE*>(file));
    }
    bool sync(Handle file) override
    {
        auto* stream = static_cast<std::FILE*>(file);
        if (std::fflush(stream)) return false;
#if defined(_WIN32)
        return ::_commit(_fileno(stream)) == 0;
#else
        return ::fsync(::fileno(stream)) == 0;
#endif
    }
    bool publish(const char* temporary, const char* destination) override
    {
        const auto existing = open(destination, Mode::Read);
        if (existing.handle)
        {
            close(existing.handle);
            return false;
        }
        return existing.status == OpenStatus::Missing && std::rename(temporary, destination) == 0;
    }
};
inline RecordFileIo& stdioRecordFileIo()
{
    static StdioRecordFileIo io;
    return io;
}
} // namespace platform::esp::storage
