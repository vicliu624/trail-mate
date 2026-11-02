#pragma once
#include "platform/linux/oneseg_ofdm_controls.h"
#include <algorithm>
#include <cstdio>
#include <string>

namespace platform::linux_runtime
{
struct OneSegOfdmLesson
{
    const char* equation = "";
    std::string explanation;
};
inline const char* oneseg_synthetic_stage_note(OneSegOfdmStage stage)
{
    constexpr const char* notes[]{
        "Teaching source: seeded QAM/pilots, injected noise/carrier/IQ impairments and a fixed DC spur. Receiver calibration acts on generated IQ. This is not hardware or native gr-isdbt instrumentation.",
        "Generated IQ uses processing Fs directly, without a native input resampler. The model extra FIR is a windowed-sinc Kaiser design; DC removal subtracts mean32. Pilot phase slope drives the teaching clock loop, limited to +/-1000 ppm.",
        "Known nominal symbol framing is assumed; no acquisition/reacquisition is performed. Mode/GI/QAM configure BOTH teaching source and receiver. Inject timing error or a late echo to observe CP mismatch and loss of orthogonality.",
        "Teaching forecast uses previous phase advance and integrator. CP correlation and confirmed integer pilot search provide observations. Purple is known injected phase advance. A small residual alone does not prove the correct integer branch.",
        "Known pilots provide channel estimates. Interpolation, EMA and regularized equalization operate on generated IQ. Echo beyond CP creates ISI that equalization cannot undo. Compare channel shape with reference EVM and uncoded BER.",
        "Data is compared with known Gray-labelled QAM, not decision references. No TV, TMCC, convolutional/RS coding or interleaving is generated. FEC controls are intentionally disabled; displayed BER is uncoded hard-decision BER.",
        "Generic known-reference OFDM leaves DC unused and uses fixed reference pilots. It has no native TMCC/AC. Select Actual to inspect computed symbols or a separately labelled layout model."};
    return notes[static_cast<unsigned>(stage)];
}
// Working-parameter calculations, never receiver measurements. The caller must
// explicitly label these previews and keep measured plots/source independent.
inline OneSegOfdmLesson oneseg_ofdm_lesson(const OneSegOfdmControl& control, const OneSegOfdmExperiment& p, double processing_rate = 0, bool synthetic = false)
{
    using P = OneSegOfdmExperiment;
    const auto format = [](const char* pattern, auto... values)
    {
        char output[640];
        std::snprintf(output, sizeof(output), pattern, values...);
        return std::string(output);
    };
    constexpr double pi = 3.14159265358979323846;
    const double tu = .000252 * std::pow(2., std::min(3u, std::max(1u, p.mode)) - 1);
    const double ts = tu * (1 + 1. / std::max(1u, p.guard_denominator));
    if (control.synthetic_only)
    {
        if (control.real == &P::synthetic_snr_db)
            return {"SNR=10log10(Psignal/Pnoise); ideal EVM~10^(-SNR/20)", format("PREVIEW injected AWGN SNR %.1f dB; ideal flat-channel noise EVM %.3g%%. Training metrics use known transmitted data, not decision references. Channel estimation, ICI and synchronization errors add to this noise floor.", p.synthetic_snr_db, 100 * std::pow(10., -p.synthetic_snr_db / 20))};
        if (control.real == &P::synthetic_cfo_hz || control.real == &P::synthetic_drift_hz_s)
            return {"phi(t)=2*pi*(f0*t+0.5*drift*t^2); advance=2*pi*f(t)*Tu", format("PREVIEW CFO %.3g Hz gives %.5g rad/useful symbol; drift %.3g Hz/s. CP estimates fractional CFO; pilot search resolves integer bins after confirmations. A small estimator residual can still be the wrong integer branch: compare the purple truth line.", p.synthetic_cfo_hz, 2 * pi * p.synthetic_cfo_hz * tu, p.synthetic_drift_hz_s)};
        if (control.real == &P::synthetic_iq_gain_db || control.real == &P::synthetic_iq_phase_degrees)
            return {"Qinjected=g*(I*sin(theta)+Q*cos(theta))", format("PREVIEW injects %.2f dB Q/I gain error and %.2f degrees quadrature error before receiver correction. Match the separate calibration controls and compare known-reference EVM, BER and image spectrum. This is generated IQ, not hardware calibration.", p.synthetic_iq_gain_db, p.synthetic_iq_phase_degrees)};
        if (control.real == &P::synthetic_echo_gain || control.real == &P::synthetic_echo_delay || control.real == &P::synthetic_echo_phase)
            return {"H[k]=1+a*exp(j*theta)*exp(-j*2*pi*k*delay/N)", format("PREVIEW echo amplitude %.3g, delay %.1f samples, phase %.1f deg. CP is %.1f samples in this training waveform. Delays beyond CP create inter-symbol interference; interpolation and regularization cannot recreate lost orthogonality.", p.synthetic_echo_gain, p.synthetic_echo_delay, p.synthetic_echo_phase, tu * 64000000 / 63 / p.guard_denominator)};
        if (control.real == &P::synthetic_timing_offset)
            return {"s(t-tau); FFT timing inside CP rotates channel phase; outside CP causes ISI", format("PREVIEW injected timing shift %.2f samples. The trainer intentionally uses known nominal symbol framing, not acquisition/reacquisition. Compare CP peaks, estimated channel, reference EVM and BER as the window crosses symbol boundaries.", p.synthetic_timing_offset)};
        if (control.real == &P::synthetic_clock_ppm)
            return {"FsADC=Fs*(1+ppm*1e-6); timing drift accumulates across symbols", format("PREVIEW source sampling offset %.2f ppm. The teaching receiver resamples with its own clock state, seeded by Initial clock and updated from inter-symbol pilot phase slope. Compare clock-ppm history and the known source offset; model clock state is limited to +/-1000 ppm.", p.synthetic_clock_ppm)};
        if (control.real == &P::synthetic_phase_noise)
            return {"phi_noise(t)=sigma*sqrt(2/8)*sum cos(2*pi*f_i*t+theta_i)", format("PREVIEW phase modulation RMS %.4g rad, eight seeded tones from 50 to 5000 Hz. This explicitly defined stationary model is not an oscillator phase-noise mask. Observe carrier-loop tracking, ICI and reference errors.", p.synthetic_phase_noise)};
        return {"seed -> deterministic QAM, pilots, AWGN and phase modulation", format("PREVIEW reference seed %u. Restarting the same experiment reproduces the generated samples. This source has no TV, TMCC, channel coding or interleaving; displayed BER is uncoded hard-decision BER against known Gray-labelled QAM.", p.synthetic_seed)};
    }
    if (control.real == &P::digital_gain)
        return {"y=G*x; Pout=G^2*Pin; deltaPSD=20log10(G)",
                format("PREVIEW G=%.4g gives %+.2f dB signal AND noise power. SNR is unchanged before clipping. Apply and inspect the measured post-filter spectrum; digital gain cannot recover clipped capture IQ.", p.digital_gain, 20 * std::log10(p.digital_gain))};
    if (control.real == &P::iq_gain_db || control.real == &P::iq_phase_degrees)
    {
        const double g = std::pow(10., p.iq_gain_db / 20), theta = p.iq_phase_degrees * pi / 180;
        const double denominator = 1 + g * g - 2 * g * std::cos(theta);
        const auto image = denominator < 1e-14 ? std::string("ideal/infinite") : format("%.2f dB", 10 * std::log10((1 + g * g + 2 * g * std::cos(theta)) / denominator));
        return {"Qcorr=(Q/g-I*sin(theta))/cos(theta); g=10^(dB/20)",
                format("PREVIEW g=%.5g, theta=%.5g rad. Uncorrected mismatch model IRR=%s. These values describe the assumed imbalance, not measured residual error. Compare mirrored spectral power after Apply.", g, theta, image.c_str())};
    }
    if (control.boolean == &P::remove_dc)
    {
        if (synthetic) return {"Teaching DC rejection: y[n]=x[n]-mean32(x)", "PREVIEW the trainer uses a streaming 32-point moving-mean subtraction and a known fixed DC spur. This differs from the native GNU Radio long-form blocker. Inspect the center-frequency spur and transient distortion; data carriers exclude DC."};
        return {"H(0)=0; y=x-lowpass(x) [DC-removal principle]",
                format("PREVIEW DC rejection %s. Production uses the GNU Radio long-form DC blocker, D=32. Inspect the zero-frequency spur after Apply. Removing DC does not remove arbitrary in-band interference.", p.remove_dc ? "ON" : "OFF")};
    }
    if (control.real == &P::filter_cutoff_hz || control.real == &P::filter_transition_hz || control.real == &P::filter_attenuation_db)
    {
        if (!p.filter_cutoff_hz && !p.filter_transition_hz && !p.filter_attenuation_db)
        {
            if (synthetic) return {"Teaching extra FIR bypass: cutoff=transition=rejection=0", "PREVIEW IQ is generated at nominal processing Fs, without a native input resampler. Explicit settings create a windowed-sinc Kaiser FIR in the model. Inspect spectral rolloff, delay-induced framing error and known-reference EVM after Apply."};
            return {"extra FIR bypass: cutoff=transition=rejection=0", "PREVIEW preserves the qualified resampler filter and bypasses the extra FIR. For an explicit filter set all three values; cutoff+transition/2 must fit raw input Nyquist. Narrower transitions cost taps, delay and CPU."};
        }
        if (!std::isfinite(processing_rate) || processing_rate <= 0 || p.filter_transition_hz <= 0)
            return {"deltaOmega=2*pi*transition/Fproc; N~(A-8)/(2.285*deltaOmega)", "PREVIEW tap estimate needs a known processing sample rate and positive transition. The Kaiser estimate is approximate; the actual GNU Radio design is bounded to 8192 taps. Inspect the measured spectrum after Apply."};
        const double estimate = std::max(1., (p.filter_attenuation_db - 8) / (2.285 * 2 * pi * p.filter_transition_hz / processing_rate) + 1);
        return {"N~(A-8)/(2.285*2*pi*transition/Fproc)+1; delay~(N-1)/(2Fproc)",
                format("PREVIEW Kaiser estimate %.0f taps / %.3f ms delay at Fproc=%.1f kHz. Approximate, not the actual designed tap count. A narrower transition or higher rejection costs CPU; compare post-filter rolloff after Apply.", estimate, (estimate - 1) / processing_rate * 500, processing_rate / 1000)};
    }
    if (control.real == &P::sample_clock_ppm)
        return {"delta0=ppm*1e-6; initial sampling increment=1-delta0",
                format("PREVIEW initial delta=%.7g, increment=%.7g. This seeds the receiver sampling loop; it is not a permanent hardware clock change. Watch native sampling-ppm estimates and CP correlation after Apply.", p.sample_clock_ppm * 1e-6, 1 - p.sample_clock_ppm * 1e-6)};
    if (control.boolean == &P::automatic_waveform)
        return {"Auto: CP hypotheses -> parity-valid TMCC -> configured decode chain",
                format("PREVIEW waveform acquisition %s. Auto ignores stored manual Mode/GI/QAM/FEC/interleaving values. Manual experiments must match the waveform; a wrong but valid choice can remove all programme output without changing RF power.", p.automatic_waveform ? "AUTOMATIC" : "MANUAL")};
    if (control.integer == &P::mode || control.integer == &P::guard_denominator)
    {
        if (synthetic) return {"Tu=N/Fs; Tcp=Tu/G; Ts=Tu+Tcp; deltaF=1/Tu", format("PREVIEW teaching Mode %u / GI 1/%u: Tu %.1f us, CP %.1f us, carrier spacing %.3f Hz. Mode and GI configure BOTH source and receiver with known nominal framing. Vary injected timing or echo delay to test guard robustness; no blind acquisition is claimed.", p.mode, p.guard_denominator, tu * 1e6, tu / p.guard_denominator * 1e6, 1 / tu)};
        return {"Tu=252us*2^(Mode-1); deltaF=1/Tu; Tcp=Tu/G; Ts=Tu+Tcp",
                format("PREVIEW Mode %u / GI 1/%u: Tu=%.1f us, deltaF=%.3f Hz, Tcp=%.1f us, Ts=%.1f us. These are nominal waveform timings. Auto acquisition overrides manual choices; compare CP peak shape and decoding after Apply.", p.mode, p.guard_denominator, tu * 1e6, 1 / tu, tu / std::max(1u, p.guard_denominator) * 1e6, ts * 1e6)};
    }
    if (control.real == &P::carrier_bandwidth || control.real == &P::timing_bandwidth || control.real == &P::damping)
    {
        const bool timing = control.real == &P::timing_bandwidth;
        const auto k = OneSegLoopCoefficients::fromBandwidth(timing ? p.timing_bandwidth : p.carrier_bandwidth, p.damping);
        return {"D=1+2*zeta*B+B^2; alpha=4*zeta*B/D; beta=4*B^2/D",
                format("PREVIEW %s alpha=%.6g, beta=%.6g, zeta=%.4g. Wider bandwidth follows changes faster but passes more estimator noise. Inspect %s traces and residual RMS; these are phase advances, not absolute RF phase.", timing ? "clock" : "carrier", k.alpha, k.beta, p.damping, synthetic ? "teaching-model" : "native")};
    }
    if (control.integer == &P::integer_search)
        return {"nominal integer CFO search span=+/-R*deltaF",
                format("PREVIEW R=%u bins: nominal +/-%.2f kHz at the stored Mode. Larger search costs work and can admit false candidates. Native frequency state has integer ambiguity; this span is not a guaranteed RF acquisition range.", p.integer_search, p.integer_search / tu / 1000)};
    if (control.integer == &P::offset_confirmation)
        return {"minimum confirmation span~Nconfirm*Ts",
                format("PREVIEW %u symbol confirmations span nominally %.3f ms. This is not total acquisition time. More confirmations resist isolated integer-CFO mistakes but delay a genuine change; compare residual spikes and transport continuity.", p.offset_confirmation, p.offset_confirmation * ts * 1000)};
    if (control.integer == &P::channel_average)
    {
        const double a = 1. / std::max(1u, p.channel_average);
        const double n90 = a == 1 ? 1 : std::ceil(std::log(.1) / std::log(1 - a));
        return {"Hhat[k]=(1-a)*Hhat[k-1]+a*H[k]; a=1/A; noise ratio=a/(2-a)",
                format("PREVIEW A=%u, a=%.4g: independent white pilot-noise variance %+.2f dB; 90%% step response about %.0f symbols. This is an EMA, not a rectangular A-symbol mean. More averaging smooths noise but lags a changing channel.", p.channel_average, a, 10 * std::log10(a / (2 - a)), n90)};
    }
    if (control.real == &P::equalizer_floor)
    {
        if (!p.equalizer_floor)
            return {"Xeq=Y/H [ZF]; noise gain=1/|H|", "PREVIEW lambda=0 preserves the qualified ZF division. A deep channel fade can amplify noise without bound in this model. Inspect channel amplitude alongside equalized I/Q and DD EVM; a smooth channel plot alone is not decoding evidence."};
        return {"Xeq=Y*conj(H)/(|H|^2+lambda); max noise gain=1/(2sqrt(lambda))",
                format("PREVIEW lambda=%.5g: bounded model noise amplitude gain %.4g. At |H|^2=lambda the desired response is halved. This is regularized ZF; it is MMSE only with a justified noise/signal ratio. Native H scale matters.", p.equalizer_floor, 1 / (2 * std::sqrt(p.equalizer_floor)))};
    }
    if (control.integer == &P::evm_window)
        return {"EVM=sqrt(sum(symbol error energy)/sum(symbol reference energy))",
                format("PREVIEW N=%u complete symbols spans nominally %.2f ms. Startup uses only available symbols. Pilots/TMCC are included in this DD estimate; it is neither known-reference EVM nor BER. Longer windows stabilize estimates and hide brief changes.", p.evm_window, p.evm_window * ts * 1000)};
    if (control.integer == &P::constellation)
        return {"bits/carrier=log2(M); normalized QAM minimum distance=2/sqrt(2(M-1)/3)",
                format("PREVIEW M=%u: %.0f bits/carrier, minimum distance %.4g at unit average symbol energy. Denser QAM leaves less noise margin. Auto chooses the received constellation; manual selection must match it. DD EVM may be biased by wrong decisions.", p.constellation, std::log2(p.constellation), 2 / std::sqrt(2. * (p.constellation - 1) / 3))};
    if (control.integer == &P::code_rate_index)
    {
        constexpr double rates[]{.5, 2. / 3, .75, 5. / 6, 7. / 8};
        const double r = rates[std::min(4u, p.code_rate_index)];
        return {"coded data payload factor=Rconv*(188/204); excludes pilots/TMCC/GI",
                format("PREVIEW convolutional rate %.5g, with RS factor %.5g. Higher code rate carries more payload with less redundancy. These are code factors, not measured programme bitrate or BER. Manual FEC must match the transmitted code.", r, r * 188 / 204)};
    }
    if (control.integer == &P::time_interleaving)
        return {"manual I in {0,L,2L,4L}; L=2^(3-Mode)",
                format("PREVIEW Mode %u allows I={0,%u,%u,%u}; stored I=%u. Interleaving spreads bursts at the cost of buffering/startup delay, and cannot create coding gain by itself. Auto overrides this manual value; a mismatched depth breaks decoding.", p.mode, 1u << (3 - std::min(3u, std::max(1u, p.mode))), 2u << (3 - std::min(3u, std::max(1u, p.mode))), 4u << (3 - std::min(3u, std::max(1u, p.mode))), p.time_interleaving)};
    return {oneseg_ofdm_equation(control.stage), oneseg_ofdm_explanation(control.stage)};
}
} // namespace platform::linux_runtime
