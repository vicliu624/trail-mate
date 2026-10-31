#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace platform::linux_runtime
{
enum class OneSegAribTextError
{
    None,
    TooLong,
    Incomplete,
    Malformed,
    Unsupported,
    BackendUnavailable,
    Decode
};
struct OneSegAribTextResult
{
    OneSegAribTextError error = OneSegAribTextError::None;
    std::string text;
    explicit operator bool() const { return error == OneSegAribTextError::None; }
};
// Bounded SI graphic/control validation before the native character decoder.
// Unsupported control/DRCS formats are explicit errors, never partial names.
OneSegAribTextError validate_oneseg_arib_si(const uint8_t*, size_t);
// Worker-side conversion. Each call has independent character state and errors.
OneSegAribTextResult decode_oneseg_arib_si(const uint8_t*, size_t);
} // namespace platform::linux_runtime
