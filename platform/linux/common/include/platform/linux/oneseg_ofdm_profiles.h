#pragma once

#include "platform/linux/oneseg_ofdm_lab.h"
#include "platform/linux/oneseg_reception_settings.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <string>

namespace platform::linux_runtime
{
struct OneSegOfdmProfile
{
    std::string id, name;
    OneSegReceptionSettings reception;
    OneSegOfdmExperiment experiment;
    bool synthetic = false;
};

inline OneSegOfdmProfile oneseg_factory_profile()
{
    OneSegOfdmProfile value;
    value.id = "default";
    value.name = "Qualified reception baseline";
    value.reception.enabled = true;
    return value;
}

class OneSegOfdmProfiles
{
  public:
    explicit OneSegOfdmProfiles(std::filesystem::path directory) : directory_(std::move(directory)) {}
    static bool validId(const std::string& id)
    {
        return !id.empty() && id.size() <= 40 && id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") == std::string::npos;
    }
    bool save(const OneSegOfdmProfile& value, bool receiver_idle)
    {
        // SD writes must not contend with a running sub-millisecond FIFO loop.
        if (!receiver_idle || !validId(value.id) || value.id == "default" || value.name.empty() || value.name.size() > 128 ||
            value.name.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
            !value.reception.valid() || !value.experiment.valid(value.synthetic ? 64000000 / 63 : value.reception.sample_rate_hz, value.synthetic)) return false;
        std::error_code error;
        std::filesystem::create_directories(directory_, error);
        if (error) return false;
        const auto destination = directory_ / (value.id + ".profile");
        const auto temporary = directory_ / (value.id + ".tmp");
        {
            std::ofstream out(temporary, std::ios::trunc);
            out.imbue(std::locale::classic());
            out << std::setprecision(17) << "TM_OFDM_PROFILE 2\n"
                << std::quoted(value.name) << '\n';
            const auto& r = value.reception;
            const auto& p = value.experiment;
            out << r.sample_rate_hz << ' ' << r.spi_clock_hz << ' ' << r.gain_step << '\n'
                << p.digital_gain << ' ' << p.iq_gain_db << ' ' << p.iq_phase_degrees << ' ' << p.remove_dc << '\n'
                << p.filter_cutoff_hz << ' ' << p.filter_transition_hz << ' ' << p.filter_attenuation_db << ' ' << p.sample_clock_ppm << '\n'
                << p.carrier_bandwidth << ' ' << p.timing_bandwidth << ' ' << p.damping << ' ' << p.integer_search << ' ' << p.offset_confirmation << '\n'
                << p.channel_average << ' ' << p.equalizer_floor << ' ' << p.evm_window << '\n'
                << p.automatic_waveform << ' ' << p.mode << ' ' << p.guard_denominator << ' ' << p.constellation << ' '
                << p.code_rate_index << ' ' << p.time_interleaving << '\n';
            out << p.synthetic_snr_db << ' ' << p.synthetic_cfo_hz << ' ' << p.synthetic_drift_hz_s << ' '
                << p.synthetic_iq_gain_db << ' ' << p.synthetic_iq_phase_degrees << ' '
                << p.synthetic_echo_gain << ' ' << p.synthetic_echo_delay << ' ' << p.synthetic_echo_phase << ' '
                << p.synthetic_timing_offset << ' ' << p.synthetic_clock_ppm << ' ' << p.synthetic_phase_noise << ' ' << p.synthetic_seed << ' ' << value.synthetic << '\n';
            out.flush();
            if (!out) return false;
        }
        std::filesystem::rename(temporary, destination, error);
        if (error)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        return !error;
    }
    bool load(const std::string& id, OneSegOfdmProfile& output) const
    {
        if (id == "default")
        {
            output = oneseg_factory_profile();
            return true;
        }
        if (!validId(id)) return false;
        const auto path = directory_ / (id + ".profile");
        std::error_code error;
        const auto bytes = std::filesystem::file_size(path, error);
        if (error || bytes > 8192) return false;
        std::ifstream in(path);
        in.imbue(std::locale::classic());
        std::string magic, trailing;
        unsigned version = 0;
        OneSegOfdmProfile value;
        value.id = id;
        value.reception.enabled = true;
        auto& r = value.reception;
        auto& p = value.experiment;
        if (!(in >> magic >> version >> std::quoted(value.name) >> r.sample_rate_hz >> r.spi_clock_hz >> r.gain_step >> p.digital_gain >> p.iq_gain_db >> p.iq_phase_degrees >> p.remove_dc >> p.filter_cutoff_hz >> p.filter_transition_hz >> p.filter_attenuation_db >> p.sample_clock_ppm >> p.carrier_bandwidth >> p.timing_bandwidth >> p.damping >> p.integer_search >> p.offset_confirmation >> p.channel_average >> p.equalizer_floor >> p.evm_window >> p.automatic_waveform >> p.mode >> p.guard_denominator >> p.constellation >> p.code_rate_index >> p.time_interleaving) ||
            magic != "TM_OFDM_PROFILE" || (version != 1 && version != 2) || value.name.empty() || value.name.size() > 128 ||
            value.name.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
            !r.valid()) return false;
        if (version == 2 && !(in >> p.synthetic_snr_db >> p.synthetic_cfo_hz >> p.synthetic_drift_hz_s >> p.synthetic_iq_gain_db >> p.synthetic_iq_phase_degrees >> p.synthetic_echo_gain >> p.synthetic_echo_delay >> p.synthetic_echo_phase >> p.synthetic_timing_offset >> p.synthetic_clock_ppm >> p.synthetic_phase_noise >> p.synthetic_seed >> value.synthetic)) return false;
        if (!p.valid(value.synthetic ? 64000000 / 63 : r.sample_rate_hz, value.synthetic) || in >> trailing) return false;
        output = std::move(value);
        return true;
    }

  private:
    std::filesystem::path directory_;
};
} // namespace platform::linux_runtime
