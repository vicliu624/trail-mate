#pragma once

#include <cstddef>
#include <cstdint>

namespace gps
{

enum class IoFailure : uint8_t
{
    None = 0,
    NotReady,
    Timeout,
    DeviceError,
    BufferTooSmall,
};

struct IoResult
{
    bool ok = false;
    IoFailure failure = IoFailure::None;
    std::size_t bytes_read = 0;

    static IoResult success(std::size_t n)
    {
        return IoResult{true, IoFailure::None, n};
    }

    static IoResult fail(IoFailure failure)
    {
        return IoResult{false, failure, 0};
    }
};

class IGnssByteStream
{
  public:
    virtual ~IGnssByteStream() = default;

    virtual IoResult read(uint8_t* out, std::size_t capacity) = 0;
};

} // namespace gps
