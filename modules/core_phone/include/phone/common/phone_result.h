#pragma once

#include <cstdint>

namespace phone
{

enum class PhoneProtocolKind : std::uint8_t
{
    None = 0,
    Meshtastic,
    MeshCore,
};

enum class PhoneError : std::uint8_t
{
    None = 0,
    InvalidFrame,
    DecodeFailed,
    UnsupportedCommand,
    AppCommandRejected,
    BufferTooSmall,
    NotReady,
};

struct PhoneResult
{
    bool ok = false;
    PhoneError error = PhoneError::None;

    static PhoneResult success()
    {
        return PhoneResult{true, PhoneError::None};
    }

    static PhoneResult fail(PhoneError error)
    {
        return PhoneResult{false, error};
    }
};

} // namespace phone
