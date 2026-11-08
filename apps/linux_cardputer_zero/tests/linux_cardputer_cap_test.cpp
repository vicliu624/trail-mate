#include "platform/linux/cardputer_cap.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using platform::linux_runtime::CardputerCap;
using platform::linux_runtime::CardputerCapObservation;

int readPower(const std::filesystem::path& path)
{
    int value = -1;
    std::ifstream(path) >> value;
    return value;
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
                      ("trailmate-cap-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    const auto path = root / "brightness";
    const CardputerCapObservation missing{true, false, true, 0,
                                          "SX1262 probe failed: OCP readback mismatch; status=00"};
    std::ofstream(path) << 0;
    {
        CardputerCap cap(path.string(), 0);
        cap.begin();
        cap.begin();
        assert(readPower(path) == 1);
        assert(cap.takeNotice(7999, missing) == nullptr);
        assert(std::strstr(cap.takeNotice(8000, missing), "not detected"));
        assert(cap.takeNotice(16000, missing) == nullptr);
        assert(!cap.pending());
    }
    assert(readPower(path) == 0);
    std::ofstream(path) << 1;
    {
        CardputerCap cap(path.string(), 0);
        cap.begin();
        auto present = missing;
        present.radio_online = true;
        assert(cap.takeNotice(100, present) == nullptr);
        assert(!cap.pending());
        assert(cap.takeNotice(9000, missing) == nullptr);
    }
    assert(readPower(path) == 1); // Preserve an already enabled OS-owned supply.
    {
        CardputerCap cap(path.string(), 0);
        cap.begin();
        auto present = missing;
        present.gps_chars = 128; // No satellite fix is required to detect a receiver.
        assert(cap.takeNotice(1000, present) == nullptr);
        assert(!cap.pending());
    }
    {
        CardputerCap cap(path.string(), 0);
        cap.begin();
        auto disabled = missing;
        disabled.radio_enabled = false;
        assert(cap.takeNotice(8000, disabled) == nullptr);
        assert(!cap.pending());
    }
    {
        CardputerCap cap(path.string(), 0);
        cap.begin();
        auto denied = missing;
        denied.radio_error = "open spidev: Permission denied";
        assert(std::strstr(cap.takeNotice(8000, denied), "permissions"));
    }
    {
        CardputerCap cap(path.string(), 0);
        cap.begin();
        auto no_endpoint = missing;
        no_endpoint.radio_error.clear();
        no_endpoint.radio_endpoint_present = false;
        assert(std::strstr(cap.takeNotice(8000, no_endpoint), "not detected"));
    }
    {
        CardputerCap cap(path.string(), 0);
        cap.begin();
        auto failed = missing;
        failed.radio_error = "SX1262 configure failed";
        assert(std::strstr(cap.takeNotice(8000, failed), "not responding"));
    }
    {
        CardputerCap cap(root.string(), 0); // Existing but unreadable power interface.
        cap.begin();
        assert(std::strstr(cap.takeNotice(0, missing), "power unavailable"));
        assert(cap.takeNotice(1, missing) == nullptr);
    }
    {
        CardputerCap cap((root / "absent").string(), 0);
        cap.begin();
        assert(!cap.pending()); // Desktop/non-Cardputer hosts remain unaffected.
        assert(cap.takeNotice(8000, missing) == nullptr);
    }
    std::filesystem::remove_all(root);
}
