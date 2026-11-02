#pragma once

#include "platform/linux/oneseg_fec_statistics.h"
#include "platform/linux/oneseg_ofdm_carrier_observations.h"
#include "platform/linux/oneseg_ofdm_lab.h"
#include "platform/linux/oneseg_ofdm_measurements.h"
#include "platform/linux/oneseg_ofdm_native_configuration.h"
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace platform::linux_runtime
{
// Record separators keep optional diagnostics independent of native text logs.
class OneSegOfdmWire
{
  public:
    enum class Result
    {
        Ordinary,
        Buffered,
        Sample,
        Signal,
        Configuration,
        Fec,
        Carrier,
        Invalid
    };
    Result feed(uint8_t byte, OneSegPhaseObservation& output, OneSegSignalMeasurement* signal = nullptr, OneSegNativeConfiguration* configuration = nullptr, OneSegFecStatistics* fec = nullptr, OneSegCarrierChunk* carrier = nullptr)
    {
        if (byte == 0x1e)
        {
            const bool interrupted = active_ || discard_;
            active_ = true;
            discard_ = false;
            used_ = 0;
            return interrupted ? Result::Invalid : Result::Buffered;
        }
        if (discard_)
        {
            if (byte == 0x1f) discard_ = false;
            return Result::Buffered;
        }
        if (!active_) return Result::Ordinary;
        if (byte == 0x1f)
        {
            active_ = false;
            bytes_[used_] = 0;
            if (!std::strncmp(bytes_.data(), "TM_CARR1 ", 9) || !std::strncmp(bytes_.data(), "TM_CARR2 ", 9))
            {
                OneSegCarrierChunk value;
                if (!oneseg_parse_carrier_chunk(bytes_.data() + 9, value, bytes_[7] == '2')) return Result::Invalid;
                if (carrier) *carrier = value;
                return Result::Carrier;
            }
            if (!std::strncmp(bytes_.data(), "TM_SIGNAL1 ", 11))
                return parseSignal(signal) ? Result::Signal : Result::Invalid;
            if (!std::strncmp(bytes_.data(), "TM_CONFIG1 ", 11))
                return parseConfiguration(configuration) ? Result::Configuration : Result::Invalid;
            if (!std::strncmp(bytes_.data(), "TM_FEC1 ", 8))
                return parseFec(fec) ? Result::Fec : Result::Invalid;
            return parse(output) ? Result::Sample : Result::Invalid;
        }
        if (byte < 32 || byte > 126 || used_ + 1 >= bytes_.size())
        {
            active_ = false;
            discard_ = true;
            used_ = 0;
            return Result::Invalid;
        }
        bytes_[used_++] = static_cast<char>(byte);
        return Result::Buffered;
    }
    bool incomplete() const { return active_ || discard_; }

  private:
    bool parseFec(OneSegFecStatistics* output)
    {
        OneSegFecStatistics value;
        char* cursor = bytes_.data() + 8;
        uint64_t* fields[]{&value.words, &value.clean, &value.corrected, &value.uncorrectable, &value.symbols, &value.maximum};
        for (auto* field : fields)
        {
            while (*cursor == ' ') ++cursor;
            if (*cursor < '0' || *cursor > '9') return false;
            char* end = nullptr;
            errno = 0;
            *field = std::strtoull(cursor, &end, 10);
            if (errno || end == cursor || (*end && *end != ' ')) return false;
            cursor = end;
        }
        while (*cursor == ' ') ++cursor;
        if (*cursor || !value.valid()) return false;
        if (output) *output = value;
        return true;
    }
    bool parseConfiguration(OneSegNativeConfiguration* output)
    {
        OneSegNativeConfiguration value;
        char* cursor = bytes_.data() + 11;
        double* fields[]{&value.mode, &value.fft_length, &value.cp_length, &value.carriers, &value.sample_rate,
                         &value.alpha, &value.beta, &value.timing_alpha, &value.timing_beta, &value.integer_search,
                         &value.offset_confirmation, &value.channel_average, &value.equalizer_floor, &value.sampling_ppm};
        for (auto* field : fields)
        {
            while (*cursor == ' ') ++cursor;
            char* end = nullptr;
            errno = 0;
            *field = std::strtod(cursor, &end);
            if (errno || end == cursor || (*end && *end != ' ')) return false;
            cursor = end;
        }
        while (*cursor == ' ') ++cursor;
        if (*cursor || !value.valid()) return false;
        if (output) *output = value;
        return true;
    }
    bool parseSignal(OneSegSignalMeasurement* output)
    {
        OneSegSignalMeasurement value;
        char* cursor = bytes_.data() + 11;
        auto integer = [&](uint64_t& result)
        {
            while (*cursor == ' ') ++cursor;
            if (*cursor < '0' || *cursor > '9') return false;
            char* end = nullptr;
            errno = 0;
            result = std::strtoull(cursor, &end, 10);
            if (errno || end == cursor || (*end && *end != ' ')) return false;
            cursor = end;
            return true;
        };
        auto number = [&](double& result)
        {
            while (*cursor == ' ') ++cursor;
            char* end = nullptr;
            errno = 0;
            result = std::strtod(cursor, &end);
            if (errno || end == cursor || (*end && *end != ' ') || !std::isfinite(result)) return false;
            cursor = end;
            return true;
        };
        uint64_t kind = 0, count = 0;
        if (!integer(kind) || kind < 1 || kind > 5 || !integer(value.sequence) ||
            !number(value.sample_rate) || !number(value.metric) || !integer(count) || count < 1 || count > 64) return false;
        value.kind = static_cast<unsigned>(kind);
        value.count = static_cast<size_t>(count);
        for (size_t i = 0; i < value.count; ++i)
            if (!number(value.points[i].x) || !number(value.points[i].y)) return false;
        while (*cursor == ' ') ++cursor;
        if (*cursor || !value.valid()) return false;
        if (output) *output = value;
        return true;
    }
    bool parse(OneSegPhaseObservation& output)
    {
        constexpr char prefix[] = "TM_OFDM1 ";
        const bool reference = !std::strncmp(bytes_.data(), "TM_SYNTH1 ", 10);
        const bool prediction = reference || !std::strncmp(bytes_.data(), "TM_OFDM2 ", sizeof(prefix) - 1);
        if (!prediction && std::strncmp(bytes_.data(), prefix, sizeof(prefix) - 1)) return false;
        char* cursor = bytes_.data() + (reference ? 10 : sizeof(prefix) - 1);
        if (*cursor < '0' || *cursor > '9') return false;
        char* end = nullptr;
        errno = 0;
        OneSegPhaseObservation value;
        value.has_prediction = prediction;
        value.has_reference = reference;
        value.symbol = std::strtoull(cursor, &end, 10);
        if (errno || end == cursor || *end != ' ') return false;
        cursor = end;
        double* fields[] = {&value.prior, &value.observed, &value.corrected, &value.correction,
                            &value.residual, &value.nco_phase, &value.frequency_hz, &value.sampling_ppm,
                            &value.prediction, &value.integrator, &value.alpha, &value.beta,
                            &value.reference_phase, &value.reference_evm, &value.reference_ber, &value.reference_bits,
                            &value.reference_errors, &value.virtual_time, &value.synthetic_snr, &value.synthetic_clock};
        for (size_t i = 0; i < (reference ? 20u : prediction ? 12u
                                                             : 8u);
             ++i)
        {
            auto* field = fields[i];
            while (*cursor == ' ') ++cursor;
            errno = 0;
            *field = std::strtod(cursor, &end);
            if (errno || end == cursor || !std::isfinite(*field) || std::abs(*field) > 1e9) return false;
            cursor = end;
            if (*cursor && *cursor != ' ') return false;
        }
        while (*cursor == ' ') ++cursor;
        if (*cursor || !value.valid()) return false;
        output = value;
        return true;
    }
    std::array<char, 4096> bytes_{};
    size_t used_ = 0;
    bool active_ = false, discard_ = false;
};
} // namespace platform::linux_runtime
