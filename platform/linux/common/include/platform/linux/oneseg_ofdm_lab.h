#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace platform::linux_runtime
{
enum class OneSegLabSource : uint8_t
{
    Unavailable,
    LiveReceiver,
    RecordedIq,
    Synthetic
};

// A complete experiment, distinct from the immutable qualified factory preset.
// Zero filter values mean the qualified backend's automatic filter design.
struct OneSegOfdmExperiment
{
    double digital_gain = 1.0;
    double iq_gain_db = 0.0, iq_phase_degrees = 0.0;
    bool remove_dc = false;
    double filter_cutoff_hz = 0.0, filter_transition_hz = 0.0;
    double filter_attenuation_db = 0.0;
    double sample_clock_ppm = 0.0;
    double carrier_bandwidth = 3.14159 / 100.0;
    double timing_bandwidth = 3.14159 / 10000.0;
    double damping = 0.70710678118654752440;
    unsigned integer_search = 10;
    unsigned offset_confirmation = 4;
    unsigned channel_average = 1;
    double equalizer_floor = 0.0;
    unsigned evm_window = 128;
    bool automatic_waveform = true;
    unsigned mode = 3, guard_denominator = 16;
    unsigned constellation = 4, code_rate_index = 1, time_interleaving = 4;
    // Only the explicit Synthetic source consumes these; live defaults unchanged.
    double synthetic_snr_db = 30, synthetic_cfo_hz = 100, synthetic_drift_hz_s = 0;
    double synthetic_iq_gain_db = 0, synthetic_iq_phase_degrees = 0;
    double synthetic_echo_gain = 0, synthetic_echo_delay = 16, synthetic_echo_phase = 0;
    double synthetic_timing_offset = 0, synthetic_clock_ppm = 0, synthetic_phase_noise = 0;
    unsigned synthetic_seed = 1;

    bool valid(uint32_t sample_rate, bool synthetic = false) const
    {
        const auto range = [](double value, double low, double high)
        { return std::isfinite(value) && value >= low && value <= high; };
        if (!range(synthetic_snr_db, -10, 100) || !range(synthetic_cfo_hz, -20000, 20000) ||
            !range(synthetic_drift_hz_s, -2000, 2000) || !range(synthetic_iq_gain_db, -12, 12) ||
            !range(synthetic_iq_phase_degrees, -45, 45) || !range(synthetic_echo_gain, 0, 1.5) ||
            !range(synthetic_echo_delay, 0, 512) || !range(synthetic_echo_phase, -180, 180) ||
            !range(synthetic_timing_offset, -256, 256) || !range(synthetic_clock_ppm, -500, 500) ||
            !range(synthetic_phase_noise, 0, .05) || synthetic_seed > 65535) return false;
        if (!sample_rate || !range(digital_gain, 0.01, 16) ||
            !range(iq_gain_db, -12, 12) || !range(iq_phase_degrees, -45, 45) ||
            !range(sample_clock_ppm, -500, 500) ||
            !range(carrier_bandwidth, 0.00001, 0.5) ||
            !range(timing_bandwidth, 0.000001, 0.1) || !range(damping, 0.1, 3) ||
            integer_search < 1 || integer_search > 32 ||
            offset_confirmation < 1 || offset_confirmation > 64 ||
            channel_average < 1 || channel_average > 32 ||
            !range(equalizer_floor, 0, 1) || evm_window < 16 || evm_window > 1024)
            return false;
        if (!range(filter_cutoff_hz, 0, sample_rate / 2.0) ||
            !range(filter_transition_hz, 0, sample_rate / 2.0) ||
            !range(filter_attenuation_db, 0, 120)) return false;
        const bool automatic_filter = filter_cutoff_hz == 0 && filter_transition_hz == 0 && filter_attenuation_db == 0;
        if (!automatic_filter && (filter_cutoff_hz <= 0 || filter_transition_hz <= 0 ||
                                  filter_attenuation_db < 20 ||
                                  filter_cutoff_hz + filter_transition_hz / 2 > sample_rate / 2.0)) return false;
        if (mode < 1 || mode > 3 || (guard_denominator != 4 && guard_denominator != 8 && guard_denominator != 16 && guard_denominator != 32) ||
            (constellation != 4 && constellation != 16 && constellation != 64) || code_rate_index > 4) return false;
        const unsigned factor = 1u << (3 - mode);
        return time_interleaving <= 16 && (synthetic || automatic_waveform || time_interleaving == 0 || time_interleaving == factor || time_interleaving == 2 * factor || time_interleaving == 4 * factor);
    }
};

struct OneSegLoopCoefficients
{
    double alpha = 0, beta = 0;
    static OneSegLoopCoefficients fromBandwidth(double bandwidth, double damping)
    {
        if (!std::isfinite(bandwidth) || !std::isfinite(damping) || bandwidth <= 0 || damping <= 0) return {};
        const double denominator = 1 + 2 * damping * bandwidth + bandwidth * bandwidth;
        return {4 * damping * bandwidth / denominator, 4 * bandwidth * bandwidth / denominator};
    }
};

// These are native receiver observations, not values produced by the teaching
// simulator. Phase advance is radians per useful symbol, not absolute RF phase.
struct OneSegPhaseObservation
{
    uint64_t symbol = 0;
    double prior = 0, observed = 0, corrected = 0;
    double correction = 0, residual = 0;
    double nco_phase = 0, frequency_hz = 0, sampling_ppm = 0;
    bool has_prediction = false;
    double prediction = 0, integrator = 0, alpha = 0, beta = 0;
    bool has_reference = false;
    double reference_phase = 0, reference_evm = 0, reference_ber = 0, reference_bits = 0, reference_errors = 0;
    double virtual_time = 0, synthetic_snr = 0, synthetic_clock = 0;
    bool valid() const
    {
        if (has_reference)
        {
            if (!has_prediction) return false;
            for (double value : {reference_phase, reference_evm, reference_ber, reference_bits, reference_errors, virtual_time, synthetic_snr, synthetic_clock})
                if (!std::isfinite(value) || std::abs(value) > 1e9) return false;
            if (reference_evm < 0 || reference_ber < 0 || reference_ber > 1 || reference_bits < 1 || reference_bits != std::floor(reference_bits) ||
                reference_errors < 0 || reference_errors > reference_bits || reference_errors != std::floor(reference_errors) || virtual_time < 0 ||
                std::abs(reference_ber - reference_errors / reference_bits) > 1e-9) return false;
        }
        for (double value : {prior, observed, corrected, correction, residual, nco_phase, frequency_hz, sampling_ppm})
            if (!std::isfinite(value)) return false;
        if (has_prediction)
        {
            for (double value : {prediction, integrator, alpha, beta})
                if (!std::isfinite(value) || std::abs(value) > 1e9) return false;
            if (alpha < 0 || alpha > 4 || beta < 0 || beta > 4) return false;
            const double tolerance = 5e-6 * (1 + std::abs(prior) + std::abs(prediction) + std::abs(corrected));
            if (std::abs(prediction - prior - integrator) > tolerance ||
                std::abs(corrected - prediction - (alpha + beta) * (observed - prior)) > tolerance) return false;
        }
        return std::abs((observed - corrected) - residual) <= 1e-4 * (1 + std::abs(observed)) &&
               std::abs((corrected - prior) - correction) <= 1e-4 * (1 + std::abs(prior));
    }
};

struct OneSegPhaseHistory
{
    static constexpr size_t capacity = 160;
    OneSegLabSource source = OneSegLabSource::Unavailable;
    std::array<OneSegPhaseObservation, capacity> samples{};
    size_t size = 0, next = 0;
    uint64_t last_update_ms = 0;
    bool append(const OneSegPhaseObservation& value, OneSegLabSource origin, uint64_t now_ms)
    {
        if (!value.valid() || origin == OneSegLabSource::Unavailable ||
            (value.has_reference && origin != OneSegLabSource::Synthetic) ||
            (size && value.symbol <= newest().symbol)) return false;
        if (size && source != origin) return false;
        source = origin;
        samples[next] = value;
        next = (next + 1) % capacity;
        if (size < capacity) ++size;
        last_update_ms = now_ms;
        return true;
    }
    const OneSegPhaseObservation& newest() const { return samples[(next + capacity - 1) % capacity]; }
    const OneSegPhaseObservation& chronological(size_t index) const
    {
        return samples[(next + capacity - size + index) % capacity];
    }
    bool fresh(uint64_t now_ms) const { return size && now_ms >= last_update_ms && now_ms - last_update_ms <= 1500; }
    double residualRms() const
    {
        if (!size) return 0;
        double sum = 0;
        for (size_t index = 0; index < size; ++index) sum += samples[index].residual * samples[index].residual;
        return std::sqrt(sum / size);
    }
};
} // namespace platform::linux_runtime
