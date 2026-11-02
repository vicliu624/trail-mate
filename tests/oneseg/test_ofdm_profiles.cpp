#include "platform/linux/oneseg_ofdm_profiles.h"
#include <cassert>
#include <unistd.h>
using namespace platform::linux_runtime;
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("ofdm-profiles-" + std::to_string(getpid()));
    OneSegOfdmProfiles store(root);
    auto baseline = oneseg_factory_profile();
    assert(baseline.reception.sample_rate_hz == 450000 && baseline.reception.spi_clock_hz == 8700000 && baseline.reception.gain_step == 9);
    assert(!store.save(baseline, true));
    auto experiment = baseline;
    experiment.id = "tracking-fast";
    experiment.name = "Tracking bandwidth experiment";
    experiment.reception.gain_step = 5;
    experiment.experiment.carrier_bandwidth = .1;
    experiment.experiment.damping = 1;
    assert(!store.save(experiment, false));
    assert(store.save(experiment, true));
    OneSegOfdmProfile loaded;
    assert(store.load(experiment.id, loaded));
    assert(loaded.experiment.carrier_bandwidth == .1 && loaded.reception.gain_step == 5);
    assert(loaded.experiment.timing_bandwidth == baseline.experiment.timing_bandwidth);
    experiment.id = "synthetic";
    experiment.synthetic = true;
    experiment.experiment.synthetic_cfo_hz = 1400.125;
    experiment.experiment.synthetic_snr_db = 17.5;
    assert(store.save(experiment, true) && store.load("synthetic", loaded));
    assert(loaded.synthetic && loaded.experiment.synthetic_cfo_hz == 1400.125 && loaded.experiment.synthetic_snr_db == 17.5);
    {
        std::ifstream current(root / "tracking-fast.profile");
        std::ofstream old(root / "legacy.profile");
        std::string line;
        for (unsigned i = 0; i < 8; ++i)
        {
            assert(std::getline(current, line));
            old << (i == 0 ? "TM_OFDM_PROFILE 1" : line) << '\n';
        }
    }
    assert(store.load("legacy", loaded) && !loaded.synthetic && loaded.experiment.synthetic_snr_db == 30);
    assert(store.load("default", loaded));
    assert(loaded.reception.gain_step == 9 && loaded.experiment.carrier_bandwidth == baseline.experiment.carrier_bandwidth);
    experiment.id = "../escape";
    assert(!store.save(experiment, true) && !store.load(experiment.id, loaded));
    experiment.id = "invalid";
    experiment.experiment.damping = -1;
    assert(!store.save(experiment, true));
    // A file named default cannot override the compiled factory preset.
    std::ofstream(root / "default.profile") << "corrupted factory override";
    assert(store.load("default", loaded) && loaded.reception.sample_rate_hz == 450000);
    std::ofstream(root / "tracking-fast.profile", std::ios::app) << "trailing data\n";
    assert(!store.load("tracking-fast", loaded));
    std::filesystem::remove_all(root);
}
