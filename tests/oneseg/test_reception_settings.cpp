#include "platform/linux/oneseg_reception_store.h"
#include <cassert>
#include <cmath>
#include <unistd.h>

using namespace platform::linux_runtime;
int main()
{
    OneSegReceptionSettings baseline;
    assert(baseline.valid() && !baseline.enabled);
    assert(baseline.sample_rate_hz == 450000 && baseline.spi_clock_hz == 8700000 && baseline.gain_step == 9);
    assert(std::abs(baseline.inputBytesPerSecond() - 900000) < .001);
    assert(std::abs(baseline.spiBytesPerSecond() - 1087500) < .001);
    assert(std::abs(baseline.drainBytesPerSecond() - 187500) < .001);
    assert(std::abs(baseline.fifoFillUs() - 1137.777777) < .001);
    assert(baseline.utilization() > .82 && baseline.utilization() < .83);
    const auto root = std::filesystem::temp_directory_path() / ("oneseg-reception-" + std::to_string(getpid()));
    const auto path = root / "settings.txt";
    for (auto rate : {450000u, 500000u})
        for (auto clock : {8350000u, 8700000u, 16000000u})
            for (auto gain : {0u, 9u, 13u})
            {
                auto value = baseline;
                value.sample_rate_hz = rate;
                value.spi_clock_hz = clock;
                value.gain_step = gain;
                assert(value.valid() && value.drainBytesPerSecond() > 0);
                assert(save_oneseg_reception(path, value));
                OneSegReceptionSettings restored;
                assert(load_oneseg_reception(path, restored) && restored.enabled);
                assert(restored.sample_rate_hz == rate && restored.spi_clock_hz == clock && restored.gain_step == gain);
            }
    auto invalid = baseline;
    invalid.sample_rate_hz = 576000;
    assert(!invalid.valid() && !save_oneseg_reception(path, invalid));
    invalid = baseline;
    invalid.gain_step = 14;
    assert(!invalid.valid());
    invalid = baseline;
    invalid.spi_clock_hz = 20000000;
    assert(!invalid.valid());
    std::ofstream(path) << "1 450000 8700000 9 trailing garbage\n";
    assert(!load_oneseg_reception(path, baseline));
    assert(baseline.sample_rate_hz == 450000 && baseline.gain_step == 9);
    std::filesystem::remove_all(root);
}
