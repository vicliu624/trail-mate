#include "platform/linux/build_identity.h"
#include "trailmate_linux_build_version.h"
#include <cstdlib>

namespace platform::linux_runtime
{
const char* runtime_firmware_version()
{
    const char* configured = std::getenv("TRAIL_MATE_FIRMWARE_VERSION");
    return configured && configured[0] ? configured : trailmate_linux_build_version;
}
} // namespace platform::linux_runtime
