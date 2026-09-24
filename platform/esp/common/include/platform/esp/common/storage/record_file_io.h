#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::esp::storage
{
// File transport for bounded record stores. Record layout, CRC and recovery
// stay in the store; bus policy, ownership and device errors stay in adapters.
// Handles are operation-scoped and must be closed before returning to the UI.
class RecordFileIo
{
  public:
    using Handle = void*;
    enum class OpenStatus : uint8_t
    {
        Ready,
        Missing,
        Unavailable,
        IoError
    };
    struct OpenResult
    {
        Handle handle = nullptr;
        OpenStatus status = OpenStatus::IoError;
    };
    enum class Mode : uint8_t
    {
        Read,
        Update,
        CreateTemporary
    };
    virtual ~RecordFileIo() = default;
    // Missing must mean verified absence, never a failed lock/open/read.
    virtual OpenResult open(const char* path, Mode mode) = 0;
    virtual bool close(Handle file) = 0;
    virtual bool size(Handle file, uint64_t& bytes) = 0;
    virtual bool seek(Handle file, uint64_t offset) = 0;
    virtual std::size_t read(Handle file, void* buffer, std::size_t bytes) = 0;
    virtual std::size_t write(Handle file, const void* buffer, std::size_t bytes) = 0;
    // A successful write alone is not a durable commit.
    virtual bool sync(Handle file) = 0;
    // Publish an initialized temporary file only if destination is absent.
    virtual bool publish(const char* temporary, const char* destination) = 0;
};
} // namespace platform::esp::storage
