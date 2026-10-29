#include "platform/linux/build_identity.h"
#include "platform/ui/device_runtime.h"
#include "platform/linux/resource_pack_policy.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <utility>

using namespace platform::linux_runtime;
bool compatible(const char* current, const char* minimum)
{
    ui::runtime::packs::PackageRecord package;
    package.min_firmware_version = minimum;
    evaluate_resource_pack_compatibility(package, {current, "extended"});
    return package.compatible_firmware;
}
int main(int argc, char** argv)
{
    unsetenv("TRAIL_MATE_FIRMWARE_VERSION");
    if (argc > 1)
    {
        assert(!std::strcmp(runtime_firmware_version(), argv[1]));
        assert(!std::strcmp(::platform::ui::device::firmware_version(), argv[1]));
    }
    assert(std::strcmp(runtime_firmware_version(), "linux-dev"));
    assert(compatible(runtime_firmware_version(), runtime_firmware_version()));
    setenv("TRAIL_MATE_FIRMWARE_VERSION", "2.3.4-test", 1);
    assert(!std::strcmp(runtime_firmware_version(), "2.3.4-test"));
    unsetenv("TRAIL_MATE_FIRMWARE_VERSION");
    const char* ordered[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1", "1.1.0", "2.0.0"};
    for (size_t a = 0; a < std::size(ordered); ++a)
        for (size_t b = 0; b < std::size(ordered); ++b) assert(compatible(ordered[a], ordered[b]) == (a >= b));
    assert(compatible("v1.2.3+build.002", "1.2.3+other"));
    assert(compatible("999999999999999999999.0.0", "999999999999999999998.0.0"));
    for (const char* invalid : {"linux-dev", "1.2", "01.2.3", "1.2.3-01", "1.2.3-", "1.2.3+", "1.2.3-a..b", "1.2.3+invalid+build"})
    {
        assert(!compatible(invalid, "1.0.0"));
        assert(!compatible("2.0.0", invalid));
    }
    ui::runtime::packs::PackageRecord package;
    package.supported_memory_profiles = resource_memory_profiles(" standard, EXTENDED ,\tconstrained ");
    evaluate_resource_pack_compatibility(package, {"1.0.0", "extended"});
    assert(package.compatible_memory_profile);
    evaluate_resource_pack_compatibility(package, {"1.0.0", ""});
    assert(!package.compatible_memory_profile);
    package.supported_memory_profiles = {"extended"};
    evaluate_resource_pack_compatibility(package, {"1.0.0", "constrained"});
    assert(!package.compatible_memory_profile);
    assert(configure_resource_pack_policy({"1.0.0", "extended"}));
    assert(configure_resource_pack_policy({"1.0.0", "extended"}));
    assert(!configure_resource_pack_policy({"1.0.0", "constrained"}));
    assert(resource_pack_policy().memory_profile == "extended");
}
