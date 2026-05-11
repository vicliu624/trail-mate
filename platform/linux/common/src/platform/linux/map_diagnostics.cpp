#include "platform/linux/map_diagnostics.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>

#include "platform/linux/runtime_paths.h"

namespace platform::linux_runtime
{
namespace
{

std::mutex s_log_mutex;

std::string lower_copy(std::string value)
{
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char ch)
                   {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

bool disables_doh(const std::string& value)
{
    const std::string lower = lower_copy(value);
    return lower == "0" || lower == "false" || lower == "off" ||
           lower == "none" || lower == "disabled";
}

std::string timestamp_utc()
{
    using clock = std::chrono::system_clock;
    const std::time_t now = clock::to_time_t(clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

} // namespace

std::filesystem::path map_diagnostic_log_path()
{
    return resolve_paths().settings_root / "logs" / "map.log";
}

void append_map_diagnostic(std::string_view category,
                           std::string_view message)
{
    const auto path = map_diagnostic_log_path();
    if (!ensure_directory(path.parent_path()))
    {
        return;
    }

    std::lock_guard<std::mutex> lock(s_log_mutex);
    std::ofstream out(path, std::ios::app);
    if (!out.is_open())
    {
        return;
    }
    out << timestamp_utc() << " [" << category << "] " << message << '\n';
}

std::string map_curl_doh_url()
{
    if (const char* specific = std::getenv("TRAIL_MATE_MAP_DOH_URL"))
    {
        const std::string value = specific;
        return disables_doh(value) ? std::string() : value;
    }
    if (const char* shared = std::getenv("TRAIL_MATE_CURL_DOH_URL"))
    {
        const std::string value = shared;
        return disables_doh(value) ? std::string() : value;
    }
    return "https://1.1.1.1/dns-query";
}

void apply_map_curl_resolver(CURL* curl)
{
    if (curl == nullptr)
    {
        return;
    }
    const std::string doh = map_curl_doh_url();
    if (doh.empty())
    {
        return;
    }
#if LIBCURL_VERSION_NUM >= 0x073E00
    curl_easy_setopt(curl, CURLOPT_DOH_URL, doh.c_str());
#else
    (void)doh;
#endif
}

std::string curl_error_message(CURLcode code, const char* error_buffer)
{
    if (error_buffer != nullptr && error_buffer[0] != '\0')
    {
        return error_buffer;
    }
    return curl_easy_strerror(code);
}

} // namespace platform::linux_runtime
