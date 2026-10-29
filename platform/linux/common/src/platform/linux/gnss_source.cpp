#include "platform/linux/gnss_source.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#if defined(__linux__)
#include <fnmatch.h>
#endif

namespace platform::linux_runtime
{
namespace
{
std::mutex options_mutex;
GnssSourceOptions options;
std::shared_ptr<GpioPowerLine> power_backend;
bool configured = false;
std::atomic_bool source_ready{false};
std::string_view trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \r\n\t");
    if (first == value.npos) return {};
    return value.substr(first, value.find_last_not_of(" \r\n\t") - first + 1);
}
bool number(std::string_view text, double& result)
{
    text = trim(text);
    if (text.empty() || text.size() > 32) return false;
    char buffer[33]{};
    std::copy(text.begin(), text.end(), buffer);
    char* end = nullptr;
    result = std::strtod(buffer, &end);
    return end == buffer + text.size() && std::isfinite(result);
}
bool parse_fix(std::string_view text, ::gps::GpsState& output)
{
    if (text.substr(0, 6) != "#XGPS:") return false;
    text.remove_prefix(6);
    std::array<std::string_view, 7> fields;
    for (unsigned index = 0; index < fields.size(); ++index)
    {
        const auto comma = text.find(',');
        if ((index < 6 && comma == text.npos) || (index == 6 && comma != text.npos)) return false;
        fields[index] = trim(text.substr(0, comma));
        if (comma != text.npos) text.remove_prefix(comma + 1);
    }
    ::gps::GpsState fix;
    double accuracy;
    if (!number(fields[0], fix.lat) || std::abs(fix.lat) > 90 ||
        !number(fields[1], fix.lng) || std::abs(fix.lng) > 180 ||
        !number(fields[2], fix.alt_m) || !number(fields[3], accuracy) || accuracy < 0 ||
        !number(fields[4], fix.speed_mps) || fix.speed_mps < 0 ||
        !number(fields[5], fix.course_deg) || fix.course_deg < 0 || fix.course_deg > 360) return false;
    // Nordic PVT altitude is ellipsoidal. No MSL conversion, satellite count,
    // HDOP, or UTC clock synchronization is invented from this record.
    const auto date = fields[6];
    if (date.size() != 21 || date.front() != '"' || date.back() != '"') return false;
    for (unsigned index = 1; index < 20; ++index)
    {
        const char expected = index == 5 || index == 8 ? '-' : index == 11              ? ' '
                                                           : index == 14 || index == 17 ? ':'
                                                                                        : 0;
        if (expected ? date[index] != expected : date[index] < '0' || date[index] > '9') return false;
    }
    const auto pair = [&](unsigned index)
    { return (date[index] - '0') * 10 + date[index + 1] - '0'; };
    const unsigned month = pair(6), day = pair(9);
    if (month < 1 || month > 12 || day < 1 || pair(12) > 23 || pair(15) > 59 || pair(18) > 60) return false;
    const unsigned year = pair(1) * 100 + pair(3);
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const unsigned days[] = {31, leap ? 29U : 28U, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (day > days[month - 1]) return false;
    fix.valid = fix.has_alt = fix.has_speed = fix.has_course = true;
    if (fix.course_deg == 360) fix.course_deg = 0;
    output = fix;
    return true;
}
} // namespace
bool valid_gnss_source_options(const GnssSourceOptions& value)
{
    const unsigned rates[] = {0, 4800, 9600, 19200, 38400, 57600, 115200};
    return (value.protocol == "nmea" || value.protocol == "nordic_slm") &&
           (value.device.empty() || std::filesystem::path(value.device).is_absolute()) &&
           (value.protocol != "nordic_slm" || !value.device.empty()) &&
           std::find(std::begin(rates), std::end(rates), value.baud) != std::end(rates) &&
           (!value.gnss_only_setup || value.protocol == "nordic_slm") && valid_gpio_power_options(value.power);
}
std::string resolve_gnss_serial_device(const std::string& selector, int& error)
{
    error = 0;
    if (selector.find_first_of("*?[") == std::string::npos) return selector;
#if defined(__linux__)
    const std::filesystem::path path(selector);
    const auto parent = path.parent_path();
    if (!path.is_absolute() || parent.string().find_first_of("*?[") != std::string::npos)
    {
        error = EINVAL;
        return {};
    }
    std::error_code ec;
    std::filesystem::directory_iterator entries(parent, ec), end;
    if (ec)
    {
        error = ec.value();
        return {};
    }
    std::string selected;
    size_t visited = 0;
    for (; entries != end; entries.increment(ec))
    {
        if (ec)
        {
            error = ec.value();
            return {};
        }
        if (++visited > 256)
        {
            error = E2BIG;
            return {};
        }
        const auto& entry = *entries;
        if (fnmatch(path.filename().c_str(), entry.path().filename().c_str(), FNM_PERIOD) != 0) continue;
        const auto status = entry.status(ec);
        if (ec)
        {
            ec.clear();
            continue;
        } // Ignore dangling hotplug links.
        if (!std::filesystem::is_character_file(status)) continue;
        if (!selected.empty())
        {
            error = ENOTUNIQ;
            return {};
        }
        selected = entry.path().string();
    }
    if (ec)
    {
        error = ec.value();
        return {};
    }
    if (selected.empty()) error = ENOENT;
    return selected;
#else
    error = ENOTSUP;
    return {};
#endif
}
bool configure_gnss_source(const GnssSourceOptions& value, std::shared_ptr<GpioPowerLine> power_line)
{
    if (!valid_gnss_source_options(value)) return false;
    std::lock_guard<std::mutex> lock(options_mutex);
    if (configured) return false;
    options = value;
    power_backend = std::move(power_line);
    configured = true;
    return true;
}
std::unique_ptr<AsyncGpioPower> make_gnss_power_control()
{
    std::lock_guard<std::mutex> lock(options_mutex);
    configured = true;
    return std::make_unique<AsyncGpioPower>(options.power, power_backend);
}
GnssSourceOptions gnss_source_options()
{
    std::lock_guard<std::mutex> lock(options_mutex);
    configured = true; // The first runtime observation seals even the legacy defaults.
    return options;
}
bool gnss_source_ready() { return source_ready.load(); }
void publish_gnss_source_ready(bool ready) { source_ready = ready; }
void NordicSlmGnss::reset() { *this = {}; }
void NordicSlmGnss::start(bool setup, uint32_t now)
{
    reset();
    setup_ = setup;
    deadline_ = now + 3000;
}
std::string_view NordicSlmGnss::command() const
{
    static constexpr std::string_view commands[] = {
        "AT\r\n", "AT#XGPS=0\r\n", "AT+CFUN=0\r\n", "AT%XSYSTEMMODE=0,0,1,0\r\n", "AT+CFUN=31\r\n", "AT#XGPS=1,0,1\r\n"};
    return commands[step_];
}
std::string_view NordicSlmGnss::pending(uint32_t now)
{
    if (ready_ || failed_) return {};
    if (static_cast<int32_t>(now - deadline_) >= 0)
    {
        failed_ = true;
        return {};
    }
    return waiting_ ? std::string_view{} : command().substr(offset_);
}
void NordicSlmGnss::wrote(std::size_t count, uint32_t now)
{
    if (ready_ || failed_ || waiting_) return;
    offset_ += std::min(count, command().size() - offset_);
    if (offset_ == command().size())
    {
        waiting_ = true;
        deadline_ = now + 3000;
    }
}
bool NordicSlmGnss::line(std::string_view text, uint32_t now, ::gps::GpsState& fix)
{
    text = trim(text);
    if (!ready_ && !failed_ && static_cast<int32_t>(now - deadline_) >= 0) failed_ = true;
    if (failed_) return false;
    auto status = text.substr(0, 6) == "#XGPS:" ? trim(text.substr(6)) : std::string_view{};
    const auto comma = status.find(',');
    if (ready_ && comma != status.npos && status.find(',', comma + 1) == status.npos &&
        (trim(status.substr(0, comma)) == "0" || trim(status.substr(comma + 1)) == "0"))
    {
        ready_ = false;
        failed_ = true;
        return false;
    }
    if (parse_fix(text, fix)) return ready_;
    if (!waiting_ || ready_ || failed_) return false;
    const bool error = text == "ERROR" || text.substr(0, 11) == "+CME ERROR:";
    if (text != "OK" && !(step_ == 1 && error))
    {
        if (error) failed_ = true;
        return false;
    }
    if (step_ == 5)
    {
        ready_ = true;
        waiting_ = false;
        return false;
    }
    step_ = !setup_ && step_ == 1 ? 5 : step_ + 1;
    offset_ = 0;
    waiting_ = false;
    deadline_ = now + 3000;
    return false;
}
} // namespace platform::linux_runtime
