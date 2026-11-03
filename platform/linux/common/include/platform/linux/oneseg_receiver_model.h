#pragma once

#include "platform/linux/oneseg_ofdm_controls.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace platform::linux_runtime
{
// These calculations never mutate reception settings or consume a radio lease.
// Resource grids and coherent native tracking remain in their existing views.
enum class OneSegReceiverView : unsigned
{
    Actual,
    Sine,
    ComplexIq,
    Adc,
    Filter,
    Fourier,
    Orthogonality,
    Prefix,
    Synchronization,
    Channel,
    Phase,
    Decisions,
    SoftBits,
    Viterbi,
    ReedSolomon,
    FixedPoint,
    Throughput,
    Count
};
struct OneSegReceiverParameter
{
    const char* name;
    const char* unit;
    double low, high, step, initial;
    const char* choices = nullptr;
};
struct OneSegReceiverViewInfo
{
    OneSegReceiverView view;
    OneSegOfdmStage stage;
    const char* name;
    const char* equations;
    const char* variables;
    const char* relationship;
    const char* conditions;
    std::vector<OneSegReceiverParameter> parameters;
};
inline const std::vector<OneSegReceiverViewInfo>& oneseg_receiver_views()
{
    using V = OneSegReceiverView;
    using S = OneSegOfdmStage;
    static const std::vector<OneSegReceiverViewInfo> views{
        {V::Actual, S::Input, "Actual", "", "", "", "", {}},
        {V::Sine, S::Input, "Sine", "s(t)=A*cos(2*pi*f*t+phi)\nT=1/f; omega=2*pi*f", "A: peak amplitude; f: Hz; phi: radians; t: seconds.", "Amplitude scales the projection; frequency sets its period; initial phase shifts the waveform.", "Continuous single-tone reference, not a measurement of RF carrier frequency.", {{"Amplitude", "x", .1, 1.5, .05, 1}, {"Frequency", "Hz", 1, 8, .25, 3}, {"Initial phase", "deg", -180, 180, 5, 30}, {"Time", "s", 0, 1, .01, .2}}},
        {V::ComplexIq, S::Input, "Complex I/Q", "z=A*exp(j*2*pi*f*t)=I+jQ\nI=A*cos(theta); Q=A*sin(theta)\nzBB=z*exp(-j*2*pi*fLO*t)", "j*j=-1; I: real projection; Q: imaginary projection; fLO: reference frequency in Hz.", "Complex mixing changes rotation to f-fLO. Equal frequencies produce a stationary baseband vector. Negative frequencies rotate in the opposite direction.", "Ideal complex mixing; analog image rejection and I/Q imbalance are separate receiver effects.", {{"Amplitude", "x", .1, 1.5, .05, 1}, {"Frequency", "Hz", -6, 6, .25, 3}, {"Reference LO", "Hz", -6, 6, .25, 2}, {"Time", "s", 0, 1, .01, .16}}},
        {V::Adc, S::Input, "Sampling / ADC", "x[n]=s(n/Fs)\nq=-Afs+Delta*round((clip(x)+Afs)/Delta)\nDelta=2*Afs/(2^B-1)\nfAlias=f-round(f/Fs)*Fs", "Fs: samples/s; B: quantizer bits; Afs=1. Endpoint-inclusive uniform levels. Ideal SQNR~6.02*B+1.76 dB applies only to a full-scale, unclipped sine.", "Lower sampling rate can alias distinct frequencies. More bits reduce quantization error, but cannot undo aliasing or capture clipping.", "Real baseband sampling example with anti-alias requirements; specialized bandpass sampling is a different design.", {{"Frequency", "Hz", 1, 24, .5, 13}, {"Sample rate", "Sa/s", 8, 64, 2, 20}, {"ADC bits", "bit", 2, 10, 1, 4}, {"Input gain", "x", .1, 2, .05, 1.1}}},
        {V::Filter, S::Filter, "FIR / decimation", "y[n]=sum(h[m]*x[n-m])\nz[r]=y[r*D]; FsOut=FsIn/D\nfNyquistOut=FsIn/(2*D)", "Input Fs=64 Sa/s; useful tone=3 Hz; interferer=23 Hz. h: normalized Hann-windowed sinc FIR.", "The pre-decimation filter must suppress frequencies that would fold into the output band. More taps sharpen transition and increase delay and work.", "Exact finite FIR response. The time comparison compensates FIR group delay so it compares the same input interval, not receiver timing.", {{"Cutoff", "Hz", 2, 28, 1, 10}, {"FIR taps", "tap", 9, 65, 2, 33}, {"Decimation", "", 0, 2, 1, 1, "2\n4\n8"}, {"Interference", "x", 0, 1, .05, .6}}},
        {V::Fourier, S::Subcarriers, "DFT / FFT", "X(f)=integral(x(t)*exp(-j*2*pi*f*t),dt)\nX[k]=sum(w[n]*x[n]*exp(-j*2*pi*k*n/N))\nf[k]=k*Fs/N", "N=64; input tone is in bin units. The complex partial sum uses only the selected sample count. The lower spectrum always uses all 64 samples.", "A matched reference adds in phase. A mismatched reference cancels over the observation interval. A non-integer tone leaks across bins; windowing trades main-lobe width for sidelobes.", "FFT is an algorithm for DFT. Zero padding alone does not lengthen observation or improve physical frequency resolution.", {{"Input tone", "bin", 1, 12, .125, 5}, {"Reference k", "bin", 0, 15, 1, 5}, {"Partial sum", "/64", 1, 64, 1, 32}, {"Window", "", 0, 1, 1, 0, "Rectangular\nHann"}}},
        {V::Orthogonality, S::Subcarriers, "Orthogonality", "sum(exp(j*2*pi*(k-l)*n/N))/N=delta[k,l]\ndf=Fs/N=1/Tu\nCM(a)=sum(exp(j*2*pi*a*n/N))/M", "N=64; M: integration samples; a=k-l+epsilon. delta is the Kronecker delta. epsilon is normalized carrier offset.", "Overlapping spectra can have zero cross-projection. A short FFT window or fractional carrier offset breaks the cancellation. Integer offsets permute receiver bins.", "The orthogonality identity requires a full useful symbol and integer bin difference modulo N. The plotted projection uses the actual M, not an assumed complete window.", {{"Carrier difference", "bin", 0, 5, .05, 1}, {"Window samples", "/64", 4, 64, 1, 64}, {"CFO epsilon", "bin", -.5, .5, .025, 0}}},
        {V::Prefix, S::Symbol, "CP / FFT window", "xCP[n]=x[n mod N], -G<=n<N\ny[n]=x[n]+a*x[n-d]\nH[k]=1+a*exp(-j*2*pi*k*d/N)\nY=H*X only for a protected FFT window", "N=64; G: prefix samples; d: echo delay; a: echo amplitude. Safe integer window: d-G <= shift <= 0. A nonzero safe shift adds exp(j*2*pi*k*shift/N).", "A sufficient prefix allows a diagonal frequency-domain channel. Crossing the safe window includes different adjacent symbols and creates ISI; one-tap equalization cannot reverse it.", "Two paths with integer delay, distinct adjacent symbols, no CFO. The residual includes the safe-window phase factor. The prefix does not remove the echo.", {{"CP length", "sample", 0, 24, 1, 8}, {"Echo delay", "sample", 0, 24, 1, 6}, {"Echo amplitude", "x", 0, .95, .05, .5}, {"FFT window shift", "sample", -16, 8, 1, 0}}},
        {V::Synchronization, S::Symbol, "Synchronization", "R[d]=sum(r[d+n+N]*conj(r[d+n]))\nrho=abs(R)/sqrt(E1*E2)\nepsilonHat=arg(R)/(2*pi)\nclockDrift~symbols*(N+G)*ppm*1e-6", "N=64; G=16; known CP start=20. Fractional CFO is ambiguous by an integer bin. A safe timing shift creates phase slope 2*pi*k*shift/N.", "Slide the candidate start to compare samples N apart. CFO affects correlation phase. Clock error accumulates timing drift across symbols and is distinct from a fixed window error.", "The correlation uses actual complex model samples. The clock plot is an independent first-order drift model, not a claim that those samples were resampled. Other standards have different synchronization structures.", {{"Candidate start", "sample", 0, 80, 1, 20}, {"CFO epsilon", "bin", -.45, .45, .025, .15}, {"Clock offset", "ppm", -100, 100, 5, 30}, {"Elapsed symbols", "", 0, 1000, 10, 300}}},
        {V::Channel, S::Channel, "Pilot estimation", "Hhat[kp]=Y[kp]/Xpilot[kp]\nHhat[k]=complexInterpolation(Hhat[kp])\nZF: Xhat=Y/Hhat\nMMSE: Xhat=conj(Hhat)*Y/(abs(Hhat)^2+N0/Es)", "N=64; Es=1; each noise axis has standard deviation sigma; N0=2*sigma^2. Endpoint bins are pilots; intermediate pilots are spaced by the selected interval.", "Longer echoes make H vary faster across frequency. Sparse pilots can miss that variation. ZF amplifies noise in deep fades; regularized MMSE trades bias for lower noise amplification.", "Static synchronized channel, adequate CP. True H and reference symbols exist only in the model. Fitting a pilot to itself is not an independent accuracy measurement. Phase is unreliable near a deep fade.", {{"Echo delay", "sample", 1, 12, 1, 4}, {"Echo amplitude", "x", 0, .98, .02, .7}, {"Pilot spacing", "bin", 0, 3, 1, 1, "2\n4\n8\n16"}, {"Noise sigma", "/axis", 0, .2, .01, .04}, {"Equalizer", "", 0, 1, 1, 1, "ZF\nMMSE"}, {"Channel plot", "", 0, 1, 1, 0, "Amplitude\nPhase"}}},
        {V::Phase, S::Tracking, "Phase tracking", "phiPrior[m]=phiHat[m-1]+omegaHat[m-1]\ne=wrap(phiObs-phiPrior)\nphiHat=phiPrior+alpha*e; omegaHat+=beta*e\nresidual=wrap(phiObs-phiHat)", "m: symbol index; phase in radians. Innovation is observation minus prior; residual is observation minus posterior. They are different errors.", "Faster correction follows a phase step sooner but admits more observation noise. Compare the observation, prior and posterior at the same symbol.", "Second-order alpha/beta phase model. Native tracking has its own phase-advance definition in rad/Tu and remains in Actual. Model state is never substituted for native observations.", {{"Alpha", "", .01, .95, .02, .25}, {"Beta", "", .001, .12, .001, .015}, {"Phase step", "rad", 0, 1.5, .05, .8}, {"Observation noise", "rad", 0, .3, .01, .08}}},
        {V::Decisions, S::Decoder, "Constellation decisions", "Xhard=argmin(s in constellation,abs(Y-s)^2)\nbits=GrayLabel(Xhard)\nEVM=sqrt(sum(abs(Y-Xref)^2)/sum(abs(Xref)^2))", "Axis Gray labels; average reference energy=1. QPSK bit I=1 for negative I; bit Q=1 for negative Q. Payload reference exists only in the model.", "Noise and phase error move received points across decision boundaries. Denser constellations reduce the distance between regions. Hard decisions discard confidence.", "Real modulation is identified by TMCC. Unknown live payload is not a known reference; model reference errors are not measured RF BER.", {{"Constellation", "", 0, 2, 1, 0, "QPSK\n16-QAM\n64-QAM"}, {"Noise sigma", "/axis", 0, .5, .01, .12}, {"Phase error", "deg", -60, 60, 1, 0}, {"Symbols", "", 8, 96, 8, 48}}},
        {V::SoftBits, S::Decoder, "Soft demapping", "L(b)=log(P(b=0|Y)/P(b=1|Y))\nL~(min(abs(Y-s1)^2)-min(abs(Y-s0)^2))/N0\nQPSK: L(I)=2*sqrt(2)*I/N0; N0=2*sigma^2", "s1 and s0 range over constellation points labelled bit 1 and bit 0. Positive L supports zero; negative L supports one; magnitude expresses confidence. QPSK reference energy=1, equal priors and complex AWGN.", "Near a decision boundary the confidence tends to zero. The hard bit can flip while the decoder still knows that the observation is unreliable. Noise calibration affects confidence.", "For QPSK the axis expression is exact. The max-log distance approximation is generally approximate for higher-order QAM. Sign convention is explicit.", {{"Received I", "", -1.5, 1.5, .05, .3}, {"Received Q", "", -1.5, 1.5, .05, -.6}, {"Noise sigma", "/axis", .1, .8, .05, .3}}},
        {V::Viterbi, S::Decoder, "Deinterleaving / FEC", "c[i]=received[permutation(i)]\nM[t,s]=min(M[t-1,sPrev]+branchMetric)\nISDB-T: K=7 convolutional inner code + puncturing", "Model: K=3, octal generators 7/5, rate 1/2, 22 information bits and two zero-tail bits. Six-column reversible block permutation. Burst reliability scales only the corrupted observations; unaffected observations retain unit magnitude. Branch metrics use squared Euclidean distance.", "A burst occupies adjacent channel bits; deinterleaving redistributes those errors before Viterbi traceback. Lower confidence in the damaged observations lets reliable surrounding observations contribute more to the surviving path.", "The model really runs Viterbi but is not the ISDB-T K=7 decoder or its interleaver. Correctability is not a fixed number of bits. RS byte outcomes and TS continuity are separate stages.", {{"Burst start", "bit", 0, 39, 1, 12}, {"Burst length", "bit", 0, 12, 1, 3}, {"Interleaver", "", 0, 1, 1, 1, "Off\nOn"}, {"Burst reliability", "x", .25, 2, .05, 1}}},
        {V::ReedSolomon, S::Decoder, "RS byte correction", "RS(204,188): 188 payload + 16 parity bytes\nGuaranteed bounded-distance recovery: 2*e+s<=16\nS_i=r(alpha^i); Lambda locates errors", "e: unknown-location erroneous bytes; s: known-location erased bytes. Up to eight unknown erroneous bytes, or sixteen erasures. Multiple wrong bits in one byte still count as one erroneous symbol.", "An unknown error consumes twice the budget of a known erasure. Crossing the guarantee does not mean that every decoder necessarily fails in the same way.", "Codeword layout and guarantee preview, not an RS solver or measured decoder outcome. Native corrected/uncorrectable counters are byte-symbol statistics, not BER.", {{"Unknown errors e", "byte", 0, 16, 1, 5}, {"Known erasures s", "byte", 0, 16, 1, 2}}},
        {V::FixedPoint, S::Input, "Fixed point", "q=round(x*2^F); xhat=q/2^F\nrange=[-2^(W-F-1), 2^(W-F-1)-2^-F]\nUnscaled FFT worst-case amplitude growth <= N", "W includes the sign bit; F is fractional bits, constrained below W. Step=2^-F. FFT worst-case headroom is log2(N) bits, not its average power gain.", "More fractional bits improve precision but reduce integer headroom. Saturation and wraparound create different distortion. Receiver FPGA mapping requires per-stage scaling and throughput budgets.", "Single-stage quantization model, not a complete fixed-point FFT implementation. Rounding is floor(x+0.5), including negative half ties.", {{"Word width W", "bit", 4, 16, 1, 8}, {"Fraction bits F", "bit", 0, 12, 1, 5}, {"Input amplitude", "x", .2, 8, .1, 2}, {"Overflow", "", 0, 1, 1, 0, "Saturate\nWrap"}}},
        {V::Throughput, S::Input, "FIFO / TS", "IQbytesPerSecond=samplePairsPerSecond*bytesPerPair\ndQueue/dt=Rin-Rout; queue is bounded\nRnet~Ndata*log2(M)*rConv*(188/204)/(Tu+Tg)", "Queue capacity in KiB (1024 bytes). Rate in kpair/s. Capacity example: Mode3, 384 data carriers, QPSK, rate 2/3, Tu=1008 us.", "A small sustained throughput deficit fills a finite FIFO. Separate capture loss, FEC failures, TS continuity, codec errors and presentation starvation. TS -> PAT/PMT -> PES -> audio/video -> presentation.", "Constant-rate queue model. Capacity preview excludes programme multiplexing overhead and is not measured programme bitrate. DMA is a transfer mechanism and can work with the SPI interface.", {{"Input rate", "kpair/s", 100, 1000, 10, 450}, {"Bytes per pair", "byte", 2, 8, 2, 4}, {"Consumer rate", "kpair/s", 100, 1000, 10, 460}, {"Buffer", "KiB", 32, 1024, 32, 256}, {"Guard denominator", "", 0, 3, 1, 2, "4\n8\n16\n32"}}}};
    return views;
}
inline const OneSegReceiverViewInfo& oneseg_receiver_view(OneSegReceiverView view)
{
    const auto& all = oneseg_receiver_views();
    const auto index = static_cast<unsigned>(view);
    return all[index < all.size() ? index : 0];
}
struct OneSegReceiverModel
{
    using C = std::complex<double>;
    static constexpr double pi = 3.14159265358979323846;
    struct Point
    {
        double x = 0, y = 0;
    };
    enum class Color
    {
        Blue,
        Gold,
        Teal,
        Muted,
        Red,
        Purple
    };
    struct Series
    {
        std::string name;
        std::vector<Point> points;
        Color color = Color::Blue;
        bool scatter = false;
    };
    struct Plot
    {
        std::string title, x_unit, y_unit;
        std::vector<Series> series;
        std::vector<double> vertical_guides, horizontal_guides;
        bool equal_axes = false;
    };
    struct Result
    {
        std::array<Plot, 3> plots;
        std::string status;
        std::array<double, 8> metrics{};
    };
    OneSegReceiverView view = OneSegReceiverView::Sine;
    std::array<double, 6> values{};
    Result result;
    explicit OneSegReceiverModel(OneSegReceiverView selected = OneSegReceiverView::Sine) { reset(selected); }
    void reset(OneSegReceiverView selected)
    {
        view = selected;
        values.fill(0);
        const auto& controls = oneseg_receiver_view(view).parameters;
        for (size_t i = 0; i < controls.size(); ++i) values[i] = controls[i].initial;
    }
    bool set(size_t index, double value)
    {
        const auto& controls = oneseg_receiver_view(view).parameters;
        if (index >= controls.size() || !std::isfinite(value) || value < controls[index].low || value > controls[index].high ||
            (controls[index].choices && value != std::floor(value))) return false;
        values[index] = value;
        return true;
    }
    static std::vector<C> dft(const std::vector<C>& input, bool inverse = false)
    {
        std::vector<C> output(input.size());
        for (size_t k = 0; k < input.size(); ++k)
            for (size_t n = 0; n < input.size(); ++n)
                output[k] += input[n] * std::polar(1., (inverse ? 2 : -2) * pi * k * n / input.size()) / double(inverse ? input.size() : 1);
        return output;
    }
    static double noise(unsigned index)
    {
        const auto uniform = [](uint32_t x)
        {
            x += 0x9e3779b9u;
            x = (x ^ (x >> 16)) * 0x85ebca6bu;
            x = (x ^ (x >> 13)) * 0xc2b2ae35u;
            x ^= x >> 16;
            return (double(x) + .5) / 4294967296.;
        };
        return std::sqrt(-2 * std::log(uniform(index * 2))) * std::cos(2 * pi * uniform(index * 2 + 1));
    }
    static unsigned bit(unsigned i)
    {
        i %= 128;
        return (i * 17 + i * i * 3 + i / 3) % 11 < 5;
    }
    static C qpsk(unsigned i) { return C(1 - 2 * int(bit(2 * i)), 1 - 2 * int(bit(2 * i + 1))) / std::sqrt(2.); }
    static double wrap(double phase) { return std::atan2(std::sin(phase), std::cos(phase)); }
    static std::string number(double value)
    {
        char text[48];
        std::snprintf(text, sizeof(text), "%.5g", value);
        return text;
    }
    Series& series(unsigned plot, const char* name, Color color = Color::Blue, bool scatter = false)
    {
        result.plots[plot].series.push_back({name, {}, color, scatter});
        return result.plots[plot].series.back();
    }
    void title(unsigned plot, const char* name, const char* x, const char* y, bool equal = false)
    {
        auto& p = result.plots[plot];
        p.title = name;
        p.x_unit = x;
        p.y_unit = y;
        p.equal_axes = equal;
    }
    void reference()
    {
        title(2, "QPSK reference", "I", "Q", true);
        auto& points = series(2, "reference", Color::Gold, true);
        for (int i = 0; i < 4; i++) points.points.push_back({(i & 1 ? 1 : -1) / std::sqrt(2.), (i & 2 ? 1 : -1) / std::sqrt(2.)});
    }
    void render();
};

inline void OneSegReceiverModel::render()
{
    using V = OneSegReceiverView;
    result = {};
    for (auto& p : result.plots) p.series.reserve(8);
    const auto samples = [&](unsigned plot, const char* name, Color color, unsigned count, const auto& function, bool scatter = false)
    {
        auto& target = series(plot, name, color, scatter);
        target.points.reserve(count);
        for (unsigned n = 0; n < count; ++n) target.points.push_back(function(n));
    };
    const auto& v = values;
    reference();
    if (view == V::Actual)
    {
        result = {};
        return;
    }
    if (view == V::Sine || view == V::ComplexIq)
    {
        const double amplitude = v[0], frequency = v[1], time = v[3], phase = view == V::Sine ? v[2] * pi / 180 : 0;
        const auto z = std::polar(amplitude, 2 * pi * frequency * time + phase);
        title(0, "Complex vector and projections", "I", "Q", true);
        series(0, "vector", Color::Gold).points = {{0, 0}, {z.real(), z.imag()}};
        samples(0, "circle", Color::Muted, 129, [&](unsigned n)
                {const auto c=std::polar(amplitude,2*pi*n/128);return Point{c.real(),c.imag()}; });
        title(1, "Time-domain projections", "time / s", "amplitude");
        samples(1, "I", Color::Blue, 201, [&](unsigned n)
                { return Point{n / 200., amplitude * std::cos(2 * pi * frequency * n / 200 + phase)}; });
        samples(1, "Q", Color::Gold, 201, [&](unsigned n)
                { return Point{n / 200., amplitude * std::sin(2 * pi * frequency * n / 200 + phase)}; });
        if (view == V::ComplexIq)
        {
            const auto baseband = std::polar(amplitude, 2 * pi * (frequency - v[2]) * time);
            series(0, "mixed", Color::Teal).points = {{0, 0}, {baseband.real(), baseband.imag()}};
            samples(1, "mixed I", Color::Teal, 201, [&](unsigned n)
                    { return Point{n / 200., amplitude * std::cos(2 * pi * (frequency - v[2]) * n / 200)}; });
            result.metrics[0] = frequency - v[2];
        }
        else result.metrics[0] = 1 / frequency;
        result.status = "I=" + number(z.real()) + " Q=" + number(z.imag()) + (view == V::ComplexIq ? "; mixed Hz=" : "; period s=") + number(result.metrics[0]);
    }
    else if (view == V::Adc)
    {
        const double frequency = v[0], fs = v[1], delta = 2 / (std::pow(2, v[2]) - 1), alias = frequency - std::round(frequency / fs) * fs;
        const auto input = [&](double t)
        { return v[3] * std::cos(2 * pi * frequency * t); };
        const auto quantize = [&](double x)
        { return -1 + delta * std::floor((std::clamp(x, -1., 1.) + 1) / delta + .5); };
        title(0, "Input, samples and alias", "time / s", "amplitude");
        samples(0, "input", Color::Muted, 401, [&](unsigned n)
                { return Point{n / 400., input(n / 400.)}; });
        samples(0, "alias", Color::Gold, 401, [&](unsigned n)
                { return Point{n / 400., v[3] * std::cos(2 * pi * alias * n / 400)}; });
        samples(
            0, "sample", Color::Blue, unsigned(fs) + 1, [&](unsigned n)
            { return Point{n / fs, input(n / fs)}; },
            true);
        title(1, "Sample and quantized value", "sample n", "amplitude");
        samples(1, "input", Color::Muted, unsigned(fs) + 1, [&](unsigned n)
                { return Point{double(n), input(n / fs)}; });
        samples(
            1, "quantized", Color::Blue, unsigned(fs) + 1, [&](unsigned n)
            { return Point{double(n), quantize(input(n / fs))}; },
            true);
        result.metrics = {std::abs(alias), delta, quantize(0), quantize(2), quantize(-2)};
        result.status = "Alias=" + number(std::abs(alias)) + " Hz; step=" + number(delta) + (v[3] > 1 ? "; input clipped" : "; within full scale");
    }
    else if (view == V::Filter)
    {
        const unsigned taps = unsigned(v[1]), decimation = 2u << unsigned(v[2]);
        std::vector<double> h(taps);
        double sum = 0;
        for (unsigned n = 0; n < taps; ++n)
        {
            const double u = n - (taps - 1) / 2., fc = v[0] / 64;
            h[n] = (std::abs(u) < 1e-12 ? 2 * fc : std::sin(2 * pi * fc * u) / (pi * u)) * (.5 - .5 * std::cos(2 * pi * n / (taps - 1)));
            sum += h[n];
        }
        for (auto& coefficient : h) coefficient /= sum;
        const auto response = [&](double frequency)
        {C value{};for(unsigned n=0;n<taps;++n)value+=h[n]*std::polar(1.,-2*pi*frequency*n/64);return std::abs(value); };
        title(0, "FIR and output Nyquist", "frequency / Hz", "gain / dB");
        samples(0, "FIR", Color::Blue, 257, [&](unsigned n)
                { return Point{n / 8., 20 * std::log10(std::max(1e-5, response(n / 8.)))}; });
        series(0, "output Nyquist", Color::Gold).points = {{32. / decimation, -100}, {32. / decimation, 0}};
        const auto input = [&](double n)
        { return std::cos(2 * pi * 3 * n / 64) + v[3] * std::cos(2 * pi * 23 * n / 64); };
        title(1, "Decimation at matching times", "time / s", "amplitude");
        samples(
            1, "unfiltered", Color::Red, 33, [&](unsigned n)
            { return Point{n * decimation / 64., input(n * decimation)}; },
            true);
        samples(
            1, "filtered", Color::Blue, 33, [&](unsigned n)
            {double y=0;for(unsigned m=0;m<taps;++m)y+=h[m]*input(n*decimation-double(m)+(taps-1)/2.);return Point{n*decimation/64.,y}; },
            true);
        result.metrics = {64. / decimation, response(23), response(0), double(taps - 1) / 2};
        result.status = "Output=" + number(result.metrics[0]) + " Sa/s; 23Hz response=" + number(20 * std::log10(std::max(1e-12, response(23)))) + " dB; delay=" + number((taps - 1) / 2.) + " samples";
    }
    else if (view == V::Fourier)
    {
        const unsigned count = unsigned(v[2]);
        const auto window = [&](unsigned n)
        { return v[3] > .5 ? .5 - .5 * std::cos(2 * pi * n / 63) : 1.; };
        std::vector<C> input(64);
        for (unsigned n = 0; n < 64; ++n) input[n] = window(n) * std::polar(1., 2 * pi * v[0] * n / 64);
        const auto spectrum = dft(input);
        title(0, "Complex partial-sum path", "Re sum", "Im sum", true);
        auto& walk = series(0, "partial sum");
        walk.points.push_back({0, 0});
        C partial{};
        for (unsigned n = 0; n < count; ++n)
        {
            partial += input[n] * std::polar(1., -2 * pi * v[1] * n / 64);
            walk.points.push_back({partial.real(), partial.imag()});
        }
        series(0, "current sum", Color::Gold).points = {{0, 0}, {partial.real(), partial.imag()}};
        title(1, "Complete 64-point DFT", "DFT bin k", "abs(X)/N");
        samples(
            1, "DFT", Color::Blue, 64, [&](unsigned k)
            { return Point{double(k), std::abs(spectrum[k]) / 64}; },
            true);
        series(1, "selected bin", Color::Gold, true).points = {{v[1], std::abs(spectrum[unsigned(v[1])]) / 64}};
        result.metrics = {std::abs(spectrum[unsigned(v[1])]) / 64, partial.real(), partial.imag()};
        result.status = "Partial sum=" + number(partial.real()) + " + j(" + number(partial.imag()) + "); abs(X[k])/N=" + number(result.metrics[0]);
    }
    else if (view == V::Orthogonality)
    {
        const unsigned count = unsigned(v[1]);
        const auto projection = [&](double a)
        {C value{};for(unsigned n=0;n<count;++n)value+=std::polar(1.,2*pi*a*n/64)/double(count);return value; };
        const double a = v[0] + v[2];
        title(0, "Cross-projection accumulation", "Re correlation", "Im correlation", true);
        auto& walk = series(0, "mean inner product");
        walk.points.push_back({0, 0});
        C value{};
        for (unsigned n = 0; n < count; ++n)
        {
            value += std::polar(1., 2 * pi * a * n / 64) / double(count);
            walk.points.push_back({value.real(), value.imag()});
        }
        series(0, "final", Color::Gold).points = {{0, 0}, {value.real(), value.imag()}};
        title(1, "Projection off a bin center", "offset / bin", "abs(CM)");
        samples(1, "projection", Color::Blue, 241, [&](unsigned n)
                {const double x=-3+n/40.;return Point{x,std::abs(projection(x))}; });
        series(1, "selected", Color::Gold, true).points = {{a, std::abs(value)}};
        result.metrics[0] = std::abs(value);
        result.status = "Offset=" + number(a) + " bin; cross-projection=" + number(std::abs(value)) + (count == 64 ? "; full symbol" : "; partial window");
    }
    else if (view == V::Prefix)
    {
        const int guard = int(v[0]), delay = int(v[1]), shift = int(v[3]);
        const auto waveform = [](unsigned seed)
        {std::vector<C> bins(64);for(unsigned k=0;k<9;++k)bins[k]=qpsk(k+seed);return dft(bins,true); };
        const auto previous = waveform(11), current = waveform(2), next = waveform(37);
        const auto sample = [&](int n)
        {
            const int span = 64 + guard, row = int(std::floor(double(n + guard) / span));
            const int local = ((n + guard) % span + span) % span - guard, k = (local + 64) % 64;
            return (row < 0 ? previous : row > 0 ? next
                                                 : current)[k];
        };
        std::vector<C> received(64);
        for (int n = 0; n < 64; ++n) received[n] = sample(n + shift) + v[2] * sample(n + shift - delay);
        const auto Y = dft(received), X = dft(current);
        title(0, "Input prefix and FFT window", "sample / relative to Tu start", "Re amplitude");
        samples(0, "input", Color::Blue, 64 + guard, [&](unsigned n)
                { return Point{double(int(n) - guard), sample(int(n) - guard).real()}; });
        samples(0, "FFT window", Color::Gold, 64, [&](unsigned n)
                { return Point{double(int(n) + shift), received[n].real()}; });
        title(1, "Diagonal-channel residual", "FFT bin k", "abs(error)");
        double maximum = 0;
        samples(
            1, "residual", Color::Red, 64, [&](unsigned k)
            {const auto H=C(1)+v[2]*std::polar(1.,-2*pi*k*delay/64);const double e=std::abs(Y[k]-X[k]*H*std::polar(1.,2*pi*k*shift/64));maximum=std::max(maximum,e);return Point{double(k),e}; },
            true);
        result.metrics = {maximum, double(shift >= delay - guard && shift <= 0), double(delay - guard)};
        result.status = "Safe window [" + number(delay - guard) + ",0]; shift=" + number(shift) + (result.metrics[1] ? "; protected" : "; adjacent-symbol ISI") + "; max residual=" + number(maximum);
    }
    else if (view == V::Synchronization)
    {
        std::vector<C> bins(64);
        for (unsigned k = 0; k < 20; ++k) bins[k] = qpsk(k);
        const auto base = dft(bins, true);
        std::vector<C> received(180);
        for (unsigned n = 0; n < received.size(); ++n)
        {
            const int u = int(n) - 20;
            received[n] = u >= 0 && u < 80 ? base[(u - 16 + 64) % 64] * std::polar(1., 2 * pi * v[1] * n / 64) : qpsk(n + 31) * .04;
        }
        const auto correlate = [&](unsigned start)
        {
            C r{};
            double e1 = 0, e2 = 0;
            for (unsigned n = 0; n < 16; ++n)
            {
                const auto a = received[start + n + 64], b = received[start + n];
                r += a * std::conj(b);
                e1 += std::norm(a);
                e2 += std::norm(b);
            }
            return Point{std::abs(r) / std::sqrt(e1 * e2), std::arg(r) / (2 * pi)};
        };
        title(0, "Sliding normalized CP correlation", "candidate d / sample", "rho");
        samples(0, "correlation", Color::Blue, 81, [&](unsigned d)
                { return Point{double(d), correlate(d).x}; });
        const auto selected = correlate(unsigned(v[0])), known = correlate(20);
        series(0, "candidate", Color::Gold, true).points = {{v[0], selected.x}};
        title(1, "Independent clock-drift model", "elapsed symbols", "drift / sample");
        samples(1, "clock drift", Color::Gold, 101, [&](unsigned n)
                {const double s=n*v[3]/100;return Point{s,s*80*v[2]*1e-6}; });
        result.metrics = {known.x, known.y, selected.x, selected.y, v[3] * 80 * v[2] * 1e-6};
        result.status = "Candidate rho=" + number(selected.x) + "; CFO=" + number(selected.y) + " bin; known-start CFO=" + number(known.y) + "; drift=" + number(result.metrics[4]) + " sample";
    }
    else if (view == V::Channel)
    {
        const unsigned spacing = 2u << unsigned(v[2]);
        std::vector<unsigned> pilots;
        for (unsigned k = 0; k < 64; k += spacing) pilots.push_back(k);
        if (pilots.back() != 63) pilots.push_back(63);
        std::vector<C> H(64), observed(64), estimate(64), equalized(64), reference_symbols(64);
        for (unsigned k = 0; k < 64; ++k)
        {
            const bool pilot = std::find(pilots.begin(), pilots.end(), k) != pilots.end();
            H[k] = C(1) + v[1] * std::polar(1., -2 * pi * k * v[0] / 64);
            reference_symbols[k] = pilot ? C(1) : qpsk(k + 2);
            observed[k] = H[k] * reference_symbols[k] + C(v[3] * noise(k + 70), v[3] * noise(k + 99));
        }
        for (unsigned k = 0; k < 64; ++k)
        {
            size_t i = 0;
            while (i + 2 < pilots.size() && pilots[i + 1] < k) ++i;
            const double t = double(k - pilots[i]) / (pilots[i + 1] - pilots[i]);
            estimate[k] = observed[pilots[i]] * (1 - t) + observed[pilots[i + 1]] * t;
            equalized[k] = observed[k] * std::conj(estimate[k]) / (std::norm(estimate[k]) + (v[4] > .5 ? 2 * v[3] * v[3] : 1e-15));
        }
        const bool phase = v[5] > .5;
        std::array<std::vector<double>, 2> displayed;
        for (unsigned which = 0; which < 2; ++which)
            for (unsigned k = 0; k < 64; ++k)
            {
                const auto z = which ? estimate[k] : H[k];
                double value = phase ? std::arg(z) : std::abs(z);
                if (phase && k) value = displayed[which].back() + wrap(value - displayed[which].back());
                displayed[which].push_back(value);
            }
        title(0, phase ? "Pilot interpolation: channel phase" : "Pilot interpolation: channel amplitude", "carrier k", phase ? "phase / rad" : "abs(H)");
        samples(0, "model H", Color::Muted, 64, [&](unsigned k)
                { return Point{double(k), displayed[0][k]}; });
        samples(0, "estimated H", Color::Blue, 64, [&](unsigned k)
                { return Point{double(k), displayed[1][k]}; });
        samples(
            0, "pilot", Color::Gold, unsigned(pilots.size()), [&](unsigned i)
            { return Point{double(pilots[i]), displayed[1][pilots[i]]}; },
            true);
        title(1, "Data-carrier equalization", "I", "Q", true);
        auto& before = series(1, "before", Color::Muted, true);
        auto& after = series(1, "after", Color::Blue, true);
        double error = 0, energy = 0, mse = 0;
        for (unsigned k = 0; k < 64; ++k)
        {
            mse += std::norm(estimate[k] - H[k]);
            if (std::find(pilots.begin(), pilots.end(), k) != pilots.end()) continue;
            before.points.push_back({observed[k].real(), observed[k].imag()});
            after.points.push_back({equalized[k].real(), equalized[k].imag()});
            error += std::norm(equalized[k] - reference_symbols[k]);
            energy += std::norm(reference_symbols[k]);
        }
        result.metrics = {mse / 64, std::sqrt(error / energy), double(pilots.size())};
        result.status = "Pilots=" + number(pilots.size()) + "/64; data EVM=" + number(result.metrics[1] * 100) + "%; N0=" + number(2 * v[3] * v[3]) + (v[4] > .5 ? "; MMSE" : "; ZF");
    }
    else if (view == V::Phase)
    {
        title(0, "Phase-correction constellation", "I", "Q", true);
        title(1, "Observation / prior / posterior", "symbol m", "phase / rad");
        title(2, "Innovation and posterior residual", "symbol m", "phase / rad");
        result.plots[2].series.clear();
        auto& before = series(0, "before", Color::Muted, true);
        auto& after = series(0, "after", Color::Blue, true);
        auto& observation = series(1, "observed", Color::Muted);
        auto& prior = series(1, "prior", Color::Gold);
        auto& posterior = series(1, "posterior", Color::Blue);
        auto& innovation = series(2, "innovation", Color::Gold);
        auto& residual = series(2, "residual", Color::Teal);
        double phase = 0, omega = 0, energy = 0;
        for (unsigned m = 0; m < 100; ++m)
        {
            const double y = .012 * m + (m >= 40 ? v[2] : 0) + v[3] * noise(m), p = phase + omega, e = wrap(y - p);
            phase = p + v[0] * e;
            omega += v[1] * e;
            const double r = wrap(y - phase);
            energy += r * r;
            observation.points.push_back({double(m), y});
            prior.points.push_back({double(m), p});
            posterior.points.push_back({double(m), phase});
            innovation.points.push_back({double(m), e});
            residual.points.push_back({double(m), r});
            const auto a = qpsk(m + 2) * std::polar(1., y), b = qpsk(m + 2) * std::polar(1., r);
            before.points.push_back({a.real(), a.imag()});
            after.points.push_back({b.real(), b.imag()});
        }
        result.metrics[0] = std::sqrt(energy / 100);
        result.status = "Residual RMS=" + number(result.metrics[0]) + " rad; phase step at symbol 40";
    }
    else if (view == V::Decisions)
    {
        const unsigned M = 4u << (2 * unsigned(v[0])), levels = unsigned(std::sqrt(M)), axis_bits = unsigned(std::log2(levels)), count = unsigned(v[3]);
        const double scale = std::sqrt(2. * (M - 1) / 3);
        const auto inverse_gray = [](unsigned g)
        {unsigned binary=g;while(g>>=1)binary^=g;return binary; };
        const auto ideal = [&](unsigned n)
        {
            unsigned i = 0, q = 0;
            for (unsigned j = 0; j < axis_bits; ++j)
            {
                i = (i << 1) | bit(n * 2 * axis_bits + j);
                q = (q << 1) | bit(n * 2 * axis_bits + axis_bits + j);
            }
            return C((int(levels) - 1 - 2 * int(inverse_gray(i))) / scale, (int(levels) - 1 - 2 * int(inverse_gray(q))) / scale);
        };
        const auto label_axis = [&](double coordinate)
        {const int position=std::clamp(int(std::floor((coordinate*scale+levels-1)/2+.5)),0,int(levels-1));const unsigned binary=levels-1-position;return binary^(binary>>1); };
        title(0, "Received points and decision regions", "I", "Q", true);
        for (unsigned i = 1; i < levels; ++i)
        {
            const double boundary = (2 * int(i) - int(levels)) / scale;
            result.plots[0].vertical_guides.push_back(boundary);
            result.plots[0].horizontal_guides.push_back(boundary);
        }
        auto& references = series(0, "reference", Color::Gold, true);
        for (unsigned i = 0; i < levels; ++i)
            for (unsigned q = 0; q < levels; ++q) references.points.push_back({(2 * int(i) - int(levels) + 1) / scale, (2 * int(q) - int(levels) + 1) / scale});
        auto& received_points = series(0, "received", Color::Blue, true);
        title(1, "Reference error per received symbol", "symbol index", "abs(error)");
        auto& errors = series(1, "error", Color::Blue, true);
        unsigned wrong = 0;
        double error_energy = 0, reference_energy = 0;
        C selected{};
        unsigned hard_i = 0, hard_q = 0;
        for (unsigned n = 0; n < count; ++n)
        {
            const auto x = ideal(n), y = x * std::polar(1., v[2] * pi / 180) + C(v[1] * noise(n), v[1] * noise(n + 300));
            received_points.points.push_back({y.real(), y.imag()});
            errors.points.push_back({double(n), std::abs(y - x)});
            const auto bi = label_axis(y.real()), bq = label_axis(y.imag());
            wrong += bi != label_axis(x.real()) || bq != label_axis(x.imag());
            error_energy += std::norm(y - x);
            reference_energy += std::norm(x);
            if (n == 2)
            {
                selected = y;
                hard_i = bi;
                hard_q = bq;
            }
        }
        result.metrics = {std::sqrt(error_energy / reference_energy), double(wrong), double(count), double(hard_i), double(hard_q)};
        result.status = "Y[2]=" + number(selected.real()) + " + j(" + number(selected.imag()) + "); Gray axes=" + number(hard_i) + "," + number(hard_q) + "; model EVM=" + number(result.metrics[0] * 100) + "%";
    }
    else if (view == V::SoftBits)
    {
        const double n0 = 2 * v[2] * v[2], li = 2 * std::sqrt(2.) * v[0] / n0, lq = 2 * std::sqrt(2.) * v[1] / n0;
        title(0, "QPSK received point and distances", "I", "Q", true);
        for (unsigned k = 0; k < 4; ++k)
        {
            const double i = (k & 1 ? 1 : -1) / std::sqrt(2.), q = (k & 2 ? 1 : -1) / std::sqrt(2.);
            series(0, "distance", Color::Muted).points = {{i, q}, {v[0], v[1]}};
        }
        series(0, "received", Color::Blue, true).points = {{v[0], v[1]}};
        title(1, "Signed soft-bit confidence", "received axis", "LLR");
        samples(1, "LLR", Color::Blue, 101, [&](unsigned n)
                {const double x=-1.5+3*n/100.;return Point{x,2*std::sqrt(2.)*x/n0}; });
        series(1, "I/Q", Color::Gold, true).points = {{v[0], li}, {v[1], lq}};
        result.metrics = {li, lq, n0};
        result.status = "L(I)=" + number(li) + "; L(Q)=" + number(lq) + "; positive supports 0; N0=" + number(n0);
    }
    else if (view == V::Viterbi)
    {
        const auto parity = [](unsigned n)
        {unsigned p=0;while(n){p^=n&1;n>>=1;}return p; };
        std::array<unsigned, 24> information{};
        std::array<unsigned, 48> coded{}, channel{};
        unsigned state = 0;
        for (unsigned t = 0; t < 24; ++t)
        {
            information[t] = t < 22 ? bit(t) : 0;
            const unsigned reg = (state << 1) | information[t];
            coded[2 * t] = parity(reg & 7);
            coded[2 * t + 1] = parity(reg & 5);
            state = reg & 3;
        }
        const auto perm = [&](unsigned i)
        { return v[2] > .5 ? (i % 6) * 8 + i / 6 : i; };
        for (unsigned i = 0; i < 48; ++i) channel[perm(i)] = coded[i];
        std::array<double, 48> received{}, deinterleaved{};
        for (unsigned i = 0; i < 48; ++i) received[i] = (1 - 2 * int(channel[i])) * (i >= v[0] && i < v[0] + v[1] ? -v[3] : 1);
        for (unsigned i = 0; i < 48; ++i) deinterleaved[i] = received[perm(i)];
        title(0, "Channel and deinterleaved errors", "coded bit position", "error location");
        samples(
            0, "channel burst", Color::Red, 48, [&](unsigned i)
            { return Point{double(i), received[i] * (1 - 2 * int(channel[i])) < 0 ? 1. : 0.}; },
            true);
        samples(
            0, "deinterleaved", Color::Blue, 48, [&](unsigned i)
            { return Point{double(i), deinterleaved[i] * (1 - 2 * int(coded[i])) < 0 ? -.7 : 0.}; },
            true);
        title(1, "Minimum survivor path metric", "input-bit time", "metric");
        auto& metric = series(1, "survivor");
        std::array<double, 4> cost{0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        std::array<std::vector<unsigned>, 4> paths;
        for (unsigned t = 0; t < 24; ++t)
        {
            std::array<double, 4> next;
            next.fill(std::numeric_limits<double>::infinity());
            std::array<std::vector<unsigned>, 4> survivors;
            for (unsigned s = 0; s < 4; ++s)
                for (unsigned b = 0; b < 2; ++b)
                {
                    const unsigned reg = (s << 1) | b, target = reg & 3;
                    const double e1 = deinterleaved[2 * t] - (1 - 2 * int(parity(reg & 7))), e2 = deinterleaved[2 * t + 1] - (1 - 2 * int(parity(reg & 5))), candidate = cost[s] + e1 * e1 + e2 * e2;
                    if (candidate < next[target])
                    {
                        next[target] = candidate;
                        survivors[target] = paths[s];
                        survivors[target].push_back(b);
                    }
                }
            cost = next;
            paths = std::move(survivors);
            metric.points.push_back({double(t), *std::min_element(cost.begin(), cost.end())});
        }
        unsigned wrong = 0;
        for (unsigned t = 0; t < 22; ++t) wrong += paths[0][t] != information[t];
        result.metrics = {double(wrong), double(std::min(48u - unsigned(v[0]), unsigned(v[1]))), cost[0]};
        result.status = "48 received coded bits -> 22 information bits; wrong=" + number(wrong) + "/22; zero-tail state 00; K=3 model";
    }
    else if (view == V::ReedSolomon)
    {
        const unsigned errors = unsigned(v[0]), erasures = unsigned(v[1]);
        title(0, "Received RS codeword layout", "byte position", "layout");
        samples(
            0, "payload", Color::Blue, 188, [](unsigned i)
            { return Point{double(i), 0}; },
            true);
        samples(
            0, "parity", Color::Gold, 16, [](unsigned i)
            { return Point{double(i + 188), 0}; },
            true);
        samples(
            0, "unknown error", Color::Red, errors, [](unsigned i)
            { return Point{double(8 + i * 7), 1}; },
            true);
        samples(
            0, "known erasure", Color::Purple, erasures, [](unsigned i)
            { return Point{double(130 + i * 3), -1}; },
            true);
        title(1, "Guaranteed byte-recovery boundary", "known erasures s", "unknown errors e");
        samples(1, "2e+s=16", Color::Muted, 17, [](unsigned s)
                { return Point{double(s), (16 - s) / 2.}; });
        series(1, "selected", errors * 2 + erasures <= 16 ? Color::Teal : Color::Red, true).points = {{double(erasures), double(errors)}};
        result.metrics = {double(errors * 2 + erasures), double(errors * 2 + erasures <= 16)};
        result.status = "2e+s=" + number(result.metrics[0]) + "/16; " + (result.metrics[1] ? "within guarantee" : "outside guarantee") + "; boundary preview, not decoder output";
    }
    else if (view == V::FixedPoint)
    {
        values[1] = std::min(values[1], values[0] - 1);
        const double scale = std::pow(2, v[1]), minimum = -std::pow(2, v[0] - 1), maximum = std::pow(2, v[0] - 1) - 1, period = std::pow(2, v[0]);
        title(0, "Floating reference and fixed-point input", "sample n", "amplitude");
        auto& original = series(0, "float", Color::Muted);
        auto& quantized = series(0, "fixed", Color::Blue);
        title(1, "Quantization and overflow error", "sample n", "error");
        auto& difference = series(1, "error", Color::Red);
        double energy = 0;
        for (unsigned n = 0; n < 129; ++n)
        {
            const double x = v[2] * std::sin(2 * pi * 3 * n / 128);
            double q = std::floor(x * scale + .5);
            q = v[3] < .5 ? std::clamp(q, minimum, maximum) : std::fmod(std::fmod(q - minimum, period) + period, period) + minimum;
            const double y = q / scale, e = y - x;
            energy += e * e;
            original.points.push_back({double(n), x});
            quantized.points.push_back({double(n), y});
            difference.points.push_back({double(n), e});
        }
        result.metrics = {minimum / scale, maximum / scale, 1 / scale, std::sqrt(energy / 129)};
        result.status = "Range [" + number(result.metrics[0]) + "," + number(result.metrics[1]) + "]; step=" + number(result.metrics[2]) + "; error RMS=" + number(result.metrics[3]);
    }
    else if (view == V::Throughput)
    {
        const double input = v[0] * 1000 * v[1], output = v[2] * 1000 * v[1], capacity = v[3] * 1024, delta = input - output;
        title(0, "Bounded receive FIFO", "elapsed / s", "queue / KiB");
        samples(0, "queue", Color::Blue, 101, [&](unsigned i)
                { return Point{i / 10., std::clamp(delta * i / 10, 0., capacity) / 1024}; });
        series(0, "capacity", Color::Gold).points = {{0, v[3]}, {10, v[3]}};
        title(1, "Loss after FIFO fills", "elapsed / s", "lost / KiB");
        samples(1, "lost", Color::Red, 101, [&](unsigned i)
                { return Point{i / 10., std::max(0., delta * i / 10 - capacity) / 1024}; });
        const double guard = 4u << unsigned(v[4]), net = 384 * 2 * (2. / 3) * (188. / 204) / (.001008 * (1 + 1 / guard));
        result.metrics = {input, delta > 0 ? capacity / delta : 0, net, std::max(0., delta * 10 - capacity)};
        result.status = "IQ=" + number(input / 1e6) + " MB/s; " + (delta > 0 ? "fill time=" + number(capacity / delta) + " s" : "no constant-rate backlog") + "; capacity preview=" + number(net / 1000) + " kb/s";
    }
}
} // namespace platform::linux_runtime
