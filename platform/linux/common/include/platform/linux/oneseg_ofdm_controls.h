#pragma once
#include "platform/linux/oneseg_ofdm_lab.h"
#include <array>
#include <cstddef>

namespace platform::linux_runtime
{
enum class OneSegOfdmStage : uint8_t
{
    Input,
    Filter,
    Symbol,
    Tracking,
    Channel,
    Decoder
};
struct OneSegOfdmControl
{
    const char* label;
    const char* unit;
    OneSegOfdmStage stage;
    double low, high, step;
    double OneSegOfdmExperiment::*real = nullptr;
    unsigned OneSegOfdmExperiment::*integer = nullptr;
    bool OneSegOfdmExperiment::*boolean = nullptr;
    bool synthetic_only = false, synthetic_rf = false;
    double value(const OneSegOfdmExperiment& settings) const
    {
        return real ? settings.*real : integer         ? settings.*integer
                                   : settings.*boolean ? 1
                                                       : 0;
    }
    bool set(OneSegOfdmExperiment& settings, double value) const
    {
        if (!std::isfinite(value) || value < low || value > high || ((integer || boolean) && value != std::floor(value))) return false;
        if (real) settings.*real = value;
        else if (integer) settings.*integer = static_cast<unsigned>(value);
        else settings.*boolean = value != 0;
        return true;
    }
};
inline const std::array<OneSegOfdmControl, 34>& oneseg_ofdm_controls()
{
    using P = OneSegOfdmExperiment;
    using S = OneSegOfdmStage;
    static const std::array<OneSegOfdmControl, 34> values{{
        {"Digital gain", "x", S::Input, .01, 16, .1, &P::digital_gain},
        {"Q/I amplitude", "dB", S::Input, -12, 12, .1, &P::iq_gain_db},
        {"Quadrature error", "deg", S::Input, -45, 45, .1, &P::iq_phase_degrees},
        {"DC rejection", "0/1", S::Input, 0, 1, 1, nullptr, nullptr, &P::remove_dc},
        {"Cutoff", "Hz; 0=auto", S::Filter, 0, 250000, 1000, &P::filter_cutoff_hz},
        {"Transition", "Hz; 0=auto", S::Filter, 0, 250000, 1000, &P::filter_transition_hz},
        {"Rejection", "dB; 0=auto", S::Filter, 0, 120, 1, &P::filter_attenuation_db},
        {"Initial clock", "ppm", S::Filter, -500, 500, .1, &P::sample_clock_ppm},
        {"Automatic waveform", "0/1", S::Symbol, 0, 1, 1, nullptr, nullptr, &P::automatic_waveform},
        {"Transmission mode", "1/2/3", S::Symbol, 1, 3, 1, nullptr, &P::mode},
        {"Guard denominator", "4/8/16/32", S::Symbol, 4, 32, 4, nullptr, &P::guard_denominator},
        {"Carrier BW", "rad/symbol", S::Tracking, .00001, .5, .001, &P::carrier_bandwidth},
        {"Clock BW", "rad/symbol", S::Tracking, .000001, .1, .0001, &P::timing_bandwidth},
        {"Loop damping", "zeta", S::Tracking, .1, 3, .01, &P::damping},
        {"Integer CFO search", "bins", S::Tracking, 1, 32, 1, nullptr, &P::integer_search},
        {"Confirmation", "symbols", S::Tracking, 1, 64, 1, nullptr, &P::offset_confirmation},
        {"Channel averaging", "symbols", S::Channel, 1, 32, 1, nullptr, &P::channel_average},
        {"Equalizer", "lambda", S::Channel, 0, 1, .001, &P::equalizer_floor},
        {"EVM window", "symbols", S::Channel, 16, 1024, 16, nullptr, &P::evm_window},
        {"Constellation size", "4/16/64", S::Decoder, 4, 64, 4, nullptr, &P::constellation},
        {"Code rate", "0..4", S::Decoder, 0, 4, 1, nullptr, &P::code_rate_index},
        {"Time interleaving", "mode-dependent", S::Decoder, 0, 16, 1, nullptr, &P::time_interleaving},
        {"SNR", "dB", S::Input, -10, 100, 1, &P::synthetic_snr_db, nullptr, nullptr, true, true},
        {"CFO", "Hz", S::Input, -20000, 20000, 10, &P::synthetic_cfo_hz, nullptr, nullptr, true, true},
        {"Drift", "Hz/s", S::Input, -2000, 2000, 10, &P::synthetic_drift_hz_s, nullptr, nullptr, true, true},
        {"Injected Q/I gain", "dB", S::Input, -12, 12, .1, &P::synthetic_iq_gain_db, nullptr, nullptr, true},
        {"Injected IQ phase", "deg", S::Input, -45, 45, .1, &P::synthetic_iq_phase_degrees, nullptr, nullptr, true},
        {"Echo amplitude", "x", S::Channel, 0, 1.5, .05, &P::synthetic_echo_gain, nullptr, nullptr, true},
        {"Echo delay", "samples", S::Channel, 0, 512, 1, &P::synthetic_echo_delay, nullptr, nullptr, true},
        {"Echo phase", "deg", S::Channel, -180, 180, 5, &P::synthetic_echo_phase, nullptr, nullptr, true},
        {"Injected timing", "samples", S::Symbol, -256, 256, 1, &P::synthetic_timing_offset, nullptr, nullptr, true},
        {"Injected clock", "ppm", S::Filter, -500, 500, 1, &P::synthetic_clock_ppm, nullptr, nullptr, true},
        {"Phase modulation", "rad RMS", S::Tracking, 0, .05, .001, &P::synthetic_phase_noise, nullptr, nullptr, true},
        {"Reference seed", "integer", S::Decoder, 0, 65535, 1, nullptr, &P::synthetic_seed, nullptr, true},
    }};
    return values;
}
inline const char* oneseg_ofdm_stage_name(OneSegOfdmStage stage)
{
    constexpr const char* names[] = {"Input / RF", "Filter / resample", "Symbol / CP", "Carrier / phase", "Pilots / equalizer", "Constellation / FEC"};
    return names[static_cast<unsigned>(stage)];
}
inline const char* oneseg_ofdm_equation(OneSegOfdmStage stage)
{
    constexpr const char* equations[] = {
        "x[n]=I[n]+jQ[n]; Qcorr=(Q/g-I sin(theta))/cos(theta)",
        "y[m]=sum x[n] h[mM-nL]; fNyquist=fs/2",
        "R[d]=sum x[d+n] conj(x[d+n+N]); CP=N/G",
        "forecast=prior+a_prev; e=observed-prior; a+=beta*e; corrected=forecast+(alpha+beta)*e",
        "H=Ypilot/Xpilot; Xeq=Y conj(H)/(|H|^2+lambda)",
        "EVM=sqrt(sum |Sestimate-Sreference|^2 / sum |Sreference|^2)"};
    return equations[static_cast<unsigned>(stage)];
}
inline const char* oneseg_ofdm_explanation(OneSegOfdmStage stage)
{
    constexpr const char* notes[] = {
        "Digital gain scales signal and noise together. Gain-step numbers are not calibrated dB. Inspect clipping and IQ imbalance before assuming more gain improves reception.",
        "A narrower transition or higher rejection requires more FIR taps, delay and CPU. Automatic values preserve the qualified filter. Cutoff plus half the transition must remain below input Nyquist.",
        "CP correlation identifies symbol boundaries and fractional carrier offset. Manual mode, guard and interleaving must agree; a wrong but valid experiment may deliberately lose synchronization.",
        "Native forecast projects the pre-update estimate with the previous integrator. The unchanged loop uses e=observed-prior, not observed-forecast. Compare forecast error with corrected residual; units are phase advance per useful symbol, not absolute RF phase.",
        "Pilot averaging trades estimator noise against channel tracking speed. Zero regularization is ZF; positive lambda limits deep-fade noise amplification. Interpret channel phase together with carrier tracking.",
        "Decision-directed EVM depends on the chosen constellation. Live BER needs known transmitted bits; transport continuity or RS failures are different measurements, not interchangeable BER estimates."};
    return notes[static_cast<unsigned>(stage)];
}
} // namespace platform::linux_runtime
