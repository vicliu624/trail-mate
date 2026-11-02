#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>

namespace platform::linux_runtime
{
enum class OneSegCarrierRole
{
    Unused,
    Data,
    ScatteredPilot,
    Tmcc,
    Ac,
    ReferencePilot
};

// One center segment, with indices matching the pinned native receiver's
// tmcc_decoder_1seg_impl tables (gr-isdbt b576783021fb...). This is a layout
// model; a measured symbol's pilot phase must come from that symbol's metadata.
struct OneSegCarrierGeometry
{
    unsigned mode = 0, fft_length = 0, cp_length = 0, active_carriers = 0;
    double sample_rate = 0;

    static OneSegCarrierGeometry centerSegment(unsigned mode, unsigned guard, bool compact)
    {
        if (mode < 1 || mode > 3 || (guard != 4 && guard != 8 && guard != 16 && guard != 32)) return {};
        const auto n = mode == 3 && compact ? 576u : 1u << (7 + mode);
        return {mode, n, n / guard, 108u << (mode - 1), mode == 3 && compact ? 4000000. / 7 : 64000000. / 63};
    }
    bool valid() const
    {
        return mode >= 1 && mode <= 3 && fft_length >= active_carriers && active_carriers == (108u << (mode - 1)) &&
               cp_length > 0 && std::isfinite(sample_rate) && sample_rate > 0;
    }
    double spacingHz() const { return valid() ? sample_rate / fft_length : 0; }
    double usefulSeconds() const { return valid() ? fft_length / sample_rate : 0; }
    double guardSeconds() const { return valid() ? cp_length / sample_rate : 0; }
    unsigned broadcastFftLength() const { return valid() ? 1u << (10 + mode) : 0; }
    int signedBin(unsigned carrier) const { return static_cast<int>(carrier) - static_cast<int>(active_carriers / 2); }
    unsigned shiftedBin(unsigned carrier) const { return (fft_length - active_carriers) / 2 + carrier; }
    double frequencyHz(unsigned carrier) const { return signedBin(carrier) * spacingHz(); }

    OneSegCarrierRole role(unsigned carrier, unsigned symbol_phase) const
    {
        if (!valid() || carrier >= active_carriers) return OneSegCarrierRole::Unused;
        if (carrier % 12 == 3 * (symbol_phase % 4)) return OneSegCarrierRole::ScatteredPilot;
        constexpr std::array<unsigned, 4> tmcc3{101, 131, 286, 349};
        constexpr std::array<unsigned, 8> ac3{7, 89, 206, 209, 226, 244, 377, 407};
        constexpr std::array<unsigned, 2> tmcc2{23, 178};
        constexpr std::array<unsigned, 4> ac2{98, 101, 118, 136};
        const auto contains = [carrier](const auto& values)
        {
            for (const auto value : values)
                if (value == carrier) return true;
            return false;
        };
        if ((mode == 1 && carrier == 49) || (mode == 2 && contains(tmcc2)) || (mode == 3 && contains(tmcc3))) return OneSegCarrierRole::Tmcc;
        if ((mode == 1 && (carrier == 35 || carrier == 79)) || (mode == 2 && contains(ac2)) || (mode == 3 && contains(ac3))) return OneSegCarrierRole::Ac;
        return OneSegCarrierRole::Data;
    }
    OneSegCarrierRole genericRole(unsigned carrier) const
    {
        if (!valid() || carrier >= active_carriers) return OneSegCarrierRole::Unused;
        return carrier % 12 == 0 ? OneSegCarrierRole::ReferencePilot : OneSegCarrierRole::Data;
    }
    int genericSignedBin(unsigned carrier) const
    {
        // The generic teaching source deliberately leaves DC unused.
        return signedBin(carrier) + (carrier >= active_carriers / 2 ? 1 : 0);
    }
};

// Exact finite-N inner product, rather than a continuous sinc approximation.
// x is received tone frequency minus the receiver bin frequency, in bin units.
inline std::complex<double> oneseg_carrier_projection(double x, unsigned n)
{
    if (!n || !std::isfinite(x)) return {};
    x = std::remainder(x, static_cast<double>(n));
    if (std::abs(x) < 1e-12) return {1, 0};
    constexpr double pi = 3.14159265358979323846;
    const double amplitude = std::sin(pi * x) / (n * std::sin(pi * x / n));
    return std::polar(1., pi * x * (n - 1) / n) * amplitude;
}

inline std::complex<double> oneseg_carrier_wave(int bin, double useful_fraction)
{
    constexpr double tau = 6.28318530717958647692;
    return std::polar(1., tau * bin * useful_fraction);
}

// A delayed path contributes exp(-j*2*pi*k*delay/N). This is a channel model,
// not a measurement of the current receiver's delay or absolute phase.
inline std::complex<double> oneseg_carrier_echo(int bin, unsigned n, double gain, double delay, double phase)
{
    if (!n || !std::isfinite(gain) || !std::isfinite(delay) || !std::isfinite(phase)) return {};
    constexpr double tau = 6.28318530717958647692;
    return std::complex<double>(1, 0) + gain * std::polar(1., phase - tau * bin * delay / n);
}

// Four-tone teaching probe with different adjacent symbols. Linear delayed
// samples cross symbol boundaries when the CP is insufficient; this deliberately
// does not assume Y=HX outside the cyclic-prefix protection interval.
struct OneSegFourToneProbe
{
    OneSegCarrierGeometry geometry;
    int selected_bin = 0;
    int symbol_index = 0;
    double epsilon = 0, delay = 0, echo_gain = 0, window_shift = 0;
    static std::complex<double> weight(int symbol, int tone)
    {
        constexpr double tau = 6.28318530717958647692;
        return std::polar(1., tau * ((symbol % 4) * 3 + tone * (symbol + 1)) / 4);
    }
    std::complex<double> transmitted(double time) const
    {
        const double length = geometry.fft_length + geometry.cp_length;
        const int symbol = static_cast<int>(std::floor((time + geometry.cp_length) / length));
        const double local = time - symbol * length;
        std::complex<double> value{};
        for (int tone = -1; tone <= 2; ++tone)
            value += weight(symbol + symbol_index, tone) * oneseg_carrier_wave(selected_bin + tone, local / geometry.fft_length);
        return value;
    }
    std::complex<double> relativeSample(double time) const
    {
        constexpr double tau = 6.28318530717958647692;
        auto y = transmitted(time + window_shift);
        if (echo_gain != 0) y += echo_gain * transmitted(time + window_shift - delay);
        return y * std::polar(1., tau * (epsilon - selected_bin) * time / geometry.fft_length);
    }
    std::complex<double> receivedBin() const
    {
        if (!geometry.valid()) return {};
        std::complex<double> result{};
        for (unsigned i = 0; i < geometry.fft_length; ++i) result += relativeSample(i);
        return result / double(geometry.fft_length);
    }
    std::complex<double> diagonalChannel() const
    {
        return oneseg_carrier_echo(selected_bin, geometry.fft_length, echo_gain, delay, 0) *
               oneseg_carrier_wave(selected_bin, window_shift / geometry.fft_length);
    }
};
} // namespace platform::linux_runtime
