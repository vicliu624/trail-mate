#include "platform/linux/oneseg_ofdm_environment.h"
#include "platform/linux/oneseg_ofdm_lab.h"
#include <cassert>
#include <limits>
using namespace platform::linux_runtime;
int main()
{
    OneSegOfdmExperiment baseline;
    assert(baseline.valid(450000) && baseline.automatic_waveform);
    assert(baseline.digital_gain == 1 && !baseline.remove_dc && baseline.filter_cutoff_hz == 0);
    const auto environment = oneseg_ofdm_environment(baseline);
    bool damping_found = false;
    const std::string prefix = "TRAIL_MATE_OFDM_DAMPING=";
    for (const auto& value : environment)
        if (value.compare(0, prefix.size(), prefix) == 0)
        {
            assert(std::stod(value.substr(prefix.size())) == baseline.damping);
            damping_found = true;
        }
    assert(damping_found);
    auto manual = baseline;
    manual.time_interleaving = 16;
    assert(manual.valid(450000)); // Auto ignores an unused mode/depth mismatch.
    manual.automatic_waveform = false;
    assert(!manual.valid(450000));
    manual.mode = 1;
    assert(manual.valid(450000));
    const auto coefficients = OneSegLoopCoefficients::fromBandwidth(baseline.carrier_bandwidth, baseline.damping);
    assert(coefficients.alpha > .08 && coefficients.alpha < .09 && coefficients.beta > .003 && coefficients.beta < .004);
    auto invalid = baseline;
    invalid.carrier_bandwidth = std::numeric_limits<double>::quiet_NaN();
    assert(!invalid.valid(450000));
    invalid = baseline;
    invalid.filter_cutoff_hz = 214000;
    invalid.filter_transition_hz = 30000;
    invalid.filter_attenuation_db = 55;
    assert(!invalid.valid(450000));
    invalid.filter_transition_hz = 10000;
    assert(invalid.valid(450000));
    OneSegPhaseHistory history;
    assert(!history.fresh(0) && history.source == OneSegLabSource::Unavailable);
    for (unsigned i = 1; i <= 200; ++i)
    {
        OneSegPhaseObservation sample;
        sample.symbol = i;
        sample.prior = 1;
        sample.observed = 1.1;
        sample.corrected = 1.05;
        sample.correction = .05;
        sample.residual = .05;
        assert(history.append(sample, OneSegLabSource::RecordedIq, i * 10));
    }
    assert(history.size == 160 && history.chronological(0).symbol == 41 && history.newest().symbol == 200);
    assert(history.fresh(3500) && !history.fresh(3501));
    assert(std::abs(history.residualRms() - .05) < 1e-12);
    auto sample = history.newest();
    assert(!history.append(sample, OneSegLabSource::RecordedIq, 2010));
    ++sample.symbol;
    assert(!history.append(sample, OneSegLabSource::LiveReceiver, 2010));
    sample.residual = 100;
    assert(!history.append(sample, OneSegLabSource::RecordedIq, 2010));
}
