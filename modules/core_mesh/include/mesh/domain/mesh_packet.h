#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mesh
{

struct ByteView
{
    const uint8_t* data = nullptr;
    size_t size = 0;

    constexpr ByteView() = default;

    constexpr ByteView(const uint8_t* view_data, size_t view_size)
        : data(view_data),
          size(view_size)
    {
    }

    constexpr bool empty() const
    {
        return data == nullptr || size == 0;
    }
};

struct MutableByteBuffer
{
    uint8_t* data = nullptr;
    size_t capacity = 0;
    size_t size = 0;

    MutableByteBuffer() = default;

    MutableByteBuffer(uint8_t* buffer_data, size_t buffer_capacity, size_t buffer_size)
        : data(buffer_data),
          capacity(buffer_capacity),
          size(buffer_size)
    {
    }
};

struct EncodedPacket
{
    uint8_t bytes[256]{};
    size_t size = 0;

    ByteView view() const
    {
        return ByteView(bytes, size);
    }
};

} // namespace mesh
