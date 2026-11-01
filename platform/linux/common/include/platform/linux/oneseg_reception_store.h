#pragma once

#include "platform/linux/oneseg_reception_settings.h"
#include <filesystem>
#include <fstream>
#include <string>

namespace platform::linux_runtime
{
inline bool load_oneseg_reception(const std::filesystem::path& path, OneSegReceptionSettings& output)
{
    std::ifstream input(path);
    OneSegReceptionSettings value;
    unsigned version = 0;
    std::string trailing;
    if (!(input >> version >> value.sample_rate_hz >> value.spi_clock_hz >> value.gain_step) ||
        version != 1 || !value.valid() || input >> trailing) return false;
    value.enabled = true;
    output = value;
    return true;
}
inline bool save_oneseg_reception(const std::filesystem::path& path, const OneSegReceptionSettings& value)
{
    if (!value.valid()) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        output << "1 " << value.sample_rate_hz << ' ' << value.spi_clock_hz << ' ' << value.gain_step << '\n';
        output.flush();
        if (!output) return false;
    }
    std::filesystem::rename(temporary, path, error);
    if (error) std::filesystem::remove(temporary);
    return !error;
}
} // namespace platform::linux_runtime
