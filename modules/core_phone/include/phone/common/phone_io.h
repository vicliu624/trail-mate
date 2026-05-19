#pragma once

#include <cstddef>
#include <cstdint>

#include "phone/common/phone_result.h"

namespace phone
{

struct ByteView
{
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

struct MutableBytes
{
    std::uint8_t* data = nullptr;
    std::size_t capacity = 0;
    std::size_t size = 0;
};

struct PhoneNotification
{
    PhoneProtocolKind protocol = PhoneProtocolKind::None;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

} // namespace phone
