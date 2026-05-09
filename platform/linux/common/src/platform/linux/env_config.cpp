#include "platform/linux/env_config.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace platform::linux_runtime
{

bool env_flag(const char* name, bool fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }

    return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "TRUE") == 0 || std::strcmp(value, "yes") == 0 ||
           std::strcmp(value, "YES") == 0;
}

int env_int(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }

    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || (end && *end != '\0'))
    {
        return fallback;
    }
    return static_cast<int>(parsed);
}

std::uint32_t env_uint32(const char* name, std::uint32_t fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || (end && *end != '\0'))
    {
        return fallback;
    }
    return static_cast<std::uint32_t>(parsed);
}

double env_double(const char* name, double fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }

    char* end = nullptr;
    const double parsed = std::strtod(value, &end);
    if (end == value || (end && *end != '\0') || !std::isfinite(parsed))
    {
        return fallback;
    }
    return parsed;
}

float env_float(const char* name, float fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }

    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    if (end == value || (end && *end != '\0') || !std::isfinite(parsed))
    {
        return fallback;
    }
    return parsed;
}

const char* env_string(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return (value && value[0] != '\0') ? value : fallback;
}

} // namespace platform::linux_runtime
