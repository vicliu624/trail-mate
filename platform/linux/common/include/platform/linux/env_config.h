#pragma once

#include <cstdint>
#include <string>

namespace platform::linux_runtime
{

// ---------------------------------------------------------------------------
// env_config
//
// Centralised environment-variable helpers.  Every runtime that currently
// copies the same "read TRAIL_MATE_* env var with fallback" pattern should
// use these instead of re-implementing getenv + strtol / strtod / strcmp.
// ---------------------------------------------------------------------------

/// Returns true when the env var is set to "1", "true", "TRUE", "yes", or "YES".
/// Returns `fallback` when the variable is absent or empty.
bool env_flag(const char* name, bool fallback = false);

/// Read an integer from an environment variable.  Returns `fallback` when
/// the variable is absent, empty, or not a valid integer.
int env_int(const char* name, int fallback = 0);

/// Read an unsigned 32-bit integer from an environment variable.
/// Returns `fallback` on any parse failure.
std::uint32_t env_uint32(const char* name, std::uint32_t fallback = 0);

/// Read a double from an environment variable.
/// Returns `fallback` on any parse failure or if the value is non-finite.
double env_double(const char* name, double fallback = 0.0);

/// Read a float from an environment variable.
/// Returns `fallback` on any parse failure or if the value is non-finite.
float env_float(const char* name, float fallback = 0.0f);

/// Read a string from an environment variable.  Returns `fallback` when the
/// variable is absent or empty.  The returned pointer is valid for the
/// lifetime of the process (it points into the environment block).
const char* env_string(const char* name, const char* fallback = nullptr);

} // namespace platform::linux_runtime
