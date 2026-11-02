#pragma once
#include "platform/linux/oneseg_ofdm_lab.h"
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace platform::linux_runtime
{
inline std::vector<std::string> oneseg_ofdm_environment(const OneSegOfdmExperiment& value)
{
    std::vector<std::string> result{"TRAIL_MATE_OFDM_DIAGNOSTICS=1"};
    const auto add = [&](const char* name, double number)
    {
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << "TRAIL_MATE_OFDM_" << name << '=' << std::setprecision(17) << number;
        result.push_back(text.str());
    };
    add("CARRIER_BW", value.carrier_bandwidth);
    add("TIMING_BW", value.timing_bandwidth);
    add("DAMPING", value.damping);
    add("INTEGER_SEARCH", value.integer_search);
    add("OFFSET_CONFIRMATION", value.offset_confirmation);
    add("DIGITAL_GAIN", value.digital_gain);
    add("IQ_GAIN_DB", value.iq_gain_db);
    add("IQ_PHASE_DEGREES", value.iq_phase_degrees);
    add("REMOVE_DC", value.remove_dc);
    add("FILTER_CUTOFF_HZ", value.filter_cutoff_hz);
    add("FILTER_TRANSITION_HZ", value.filter_transition_hz);
    add("FILTER_ATTENUATION_DB", value.filter_attenuation_db);
    add("SAMPLE_CLOCK_PPM", value.sample_clock_ppm);
    add("CHANNEL_AVERAGE", value.channel_average);
    add("EQUALIZER_FLOOR", value.equalizer_floor);
    add("EVM_WINDOW", value.evm_window);
    add("MANUAL", !value.automatic_waveform);
    add("MODE", value.mode);
    add("GUARD", value.guard_denominator);
    add("CONSTELLATION", value.constellation);
    add("CODE_RATE", value.code_rate_index);
    add("INTERLEAVING", value.time_interleaving);
    add("SYNTH_SNR", value.synthetic_snr_db);
    add("SYNTH_CFO", value.synthetic_cfo_hz);
    add("SYNTH_DRIFT", value.synthetic_drift_hz_s);
    add("SYNTH_IQ_GAIN", value.synthetic_iq_gain_db);
    add("SYNTH_IQ_PHASE", value.synthetic_iq_phase_degrees);
    add("SYNTH_ECHO_GAIN", value.synthetic_echo_gain);
    add("SYNTH_ECHO_DELAY", value.synthetic_echo_delay);
    add("SYNTH_ECHO_PHASE", value.synthetic_echo_phase);
    add("SYNTH_TIMING", value.synthetic_timing_offset);
    add("SYNTH_CLOCK", value.synthetic_clock_ppm);
    add("SYNTH_PHASE_NOISE", value.synthetic_phase_noise);
    add("SYNTH_SEED", value.synthetic_seed);
    return result;
}
} // namespace platform::linux_runtime
