#include "platform/ui/gps_runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "platform/linux/gnss_source.h"
#include "platform/linux/iio_sensors.h"
#include "platform/linux/runtime_packet_log.h"

namespace platform::ui::gps
{
namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxMercatorLat = 85.05112878;
constexpr double kEquatorResolution = 156543.03392;
constexpr size_t kMaxFields = 24;
constexpr size_t kLineBufferSize = 160;
constexpr uint32_t kExternalSourceStaleMs = 15000U;
constexpr uint32_t kServicePollIntervalMs = 250U;
constexpr uint32_t kRecentTrafficWindowMs = 10000U;

constexpr const char* kGpsValidEnv = "TRAIL_MATE_GPS_VALID";
constexpr const char* kGpsEnabledEnv = "TRAIL_MATE_GPS_ENABLED";
constexpr const char* kGpsPoweredEnv = "TRAIL_MATE_GPS_POWERED";
constexpr const char* kGpsLatEnv = "TRAIL_MATE_GPS_LAT";
constexpr const char* kGpsLngEnv = "TRAIL_MATE_GPS_LNG";
constexpr const char* kGpsAltEnv = "TRAIL_MATE_GPS_ALT_M";
constexpr const char* kGpsSpeedEnv = "TRAIL_MATE_GPS_SPEED_MPS";
constexpr const char* kGpsCourseEnv = "TRAIL_MATE_GPS_COURSE_DEG";
constexpr const char* kGpsSatCountEnv = "TRAIL_MATE_GPS_SATS";
constexpr const char* kGpsHdopEnv = "TRAIL_MATE_GPS_HDOP";
constexpr const char* kGpsFixEnv = "TRAIL_MATE_GPS_FIX";
constexpr const char* kGpsDeviceEnv = "TRAIL_MATE_GPS_DEVICE";
constexpr const char* kGpsDeviceCandidatesEnv = "TRAIL_MATE_GPS_DEVICE_CANDIDATES";
constexpr const char* kGpsBaudEnv = "TRAIL_MATE_GPS_BAUD";
constexpr const char* kGpsNmeaFileEnv = "TRAIL_MATE_GPS_NMEA_FILE";
constexpr const char* kGpsAutoSerialEnv = "TRAIL_MATE_GPS_AUTO_SERIAL";
constexpr const char* kGpsDemoDefaultsEnv = "TRAIL_MATE_GPS_DEMO_DEFAULTS";
constexpr const char* kDefaultGpsDeviceCandidates =
    "/dev/serial0:/dev/ttyAMA1:/dev/ttyAMA0:/dev/ttyS0:/dev/ttyS1";
constexpr uint32_t kAutoSerialNoNmeaFailoverMs = 4000U;

constexpr double kDefaultLat = 25.0389;
constexpr double kDefaultLng = 102.7183;
constexpr double kDefaultAltM = 1891.0;
constexpr double kDefaultSpeedMps = 0.4;
constexpr double kDefaultCourseDeg = 92.0;
constexpr uint8_t kDefaultSatCount = 9;
constexpr float kDefaultHdop = 0.9f;

enum class CollectorSlot : size_t
{
    GPS = 0,
    GLN,
    GAL,
    BD,
    UNKNOWN,
    COUNT
};

struct GsvCollector
{
    std::array<GnssSatInfo, ::gps::kMaxGnssSats> sats{};
    std::size_t count = 0;
};

struct RuntimeState
{
    std::unique_ptr<::platform::linux_runtime::AsyncGpioPower> power;
    int last_power_error = 0;
    ::platform::linux_runtime::NordicSlmGnss slm;
    bool slm_serial = false;
    uint32_t slm_retry_ms = 0;
    GpsState data{};
    std::array<GnssSatInfo, ::gps::kMaxGnssSats> sats{};
    std::size_t sat_count = 0;
    GnssStatus status{};
    uint32_t last_motion_ms = 0;
    uint32_t last_rx_ms = 0;
    uint32_t last_fix_ms = 0;
    std::array<uint16_t, 12> used_sat_ids{};
    std::size_t used_sat_count = 0;
    std::array<GsvCollector, static_cast<size_t>(CollectorSlot::COUNT)> gsv{};

    std::string source_path{};
    bool source_is_serial = false;
    bool source_is_auto_candidate = false;
    std::size_t file_offset = 0;
    std::array<char, kLineBufferSize> line_buffer{};
    std::size_t line_length = 0;
    bool line_overflow = false;
    std::string last_open_failure_path{};
    uint32_t last_open_failure_log_ms = 0;
    uint32_t source_opened_ms = 0;
    uint32_t last_serial_byte_ms = 0;
    uint32_t last_service_poll_ms = 0;
    uint32_t chars_recent_window_ms = 0;
    uint32_t chars_total = 0;
    uint32_t chars_recent = 0;
    bool waiting_log_emitted = false;
    bool no_source_log_emitted = false;
    std::vector<std::string> auto_candidate_paths{};
    std::string auto_candidate_signature{};
    std::size_t auto_candidate_index = 0;

#if defined(__linux__)
    int serial_fd = -1;
#endif
};

bool s_suspended = false;
std::atomic_bool s_power_ready{false};
bool s_enabled = true;
uint32_t s_collection_interval_ms = 1000;
uint8_t s_power_strategy = 0;
uint8_t s_gnss_mode = 0;
uint8_t s_gnss_sat_mask = 0xFF;
uint8_t s_external_nmea_output_hz = 0;
uint8_t s_external_nmea_sentence_mask = 0;
uint32_t s_motion_idle_timeout_ms = 30000;
uint8_t s_motion_sensor_id = 0;
FallbackMode s_fallback_mode = FallbackMode::LiveOnly;
GpsReceiverInitConfig s_receiver_init_config{};
RuntimeState s_runtime{};
std::mutex s_mutex;

void reset_source_locked();
void append_gps_system_log_locked(
    const char* title,
    const std::string& summary,
    const std::vector<::platform::linux_runtime::PacketLogSegment>& segments);

uint32_t now_ms()
{
    using clock = std::chrono::steady_clock;
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
}

bool env_flag_or_default(const char* name, bool fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }

    return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "TRUE") == 0 || std::strcmp(value, "yes") == 0 ||
           std::strcmp(value, "YES") == 0;
}

double env_double_or_default(const char* name, double fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }

    char* end = nullptr;
    const double parsed = std::strtod(value, &end);
    if (end == value || (end && *end != '\0'))
    {
        return fallback;
    }
    return parsed;
}

int env_int_or_default(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }

    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || (end && *end != '\0'))
    {
        return fallback;
    }
    return static_cast<int>(parsed);
}

bool env_configured(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
}

std::string effective_receiver_init_source()
{
    return s_receiver_init_config.baud != 0 ? "settings" : "runtime";
}

bool supported_baud(int baud)
{
    switch (baud)
    {
    case 4800:
    case 9600:
    case 19200:
    case 38400:
    case 57600:
    case 115200:
        return true;
    default:
        return false;
    }
}

bool demo_defaults_enabled()
{
    return s_fallback_mode == FallbackMode::DemoDefaults ||
           env_flag_or_default(kGpsDemoDefaultsEnv, false);
}

void update_recent_traffic_window_locked(uint32_t ts)
{
    if (s_runtime.chars_recent_window_ms == 0 ||
        (ts - s_runtime.chars_recent_window_ms) >= kRecentTrafficWindowMs)
    {
        s_runtime.chars_recent_window_ms = ts;
        s_runtime.chars_recent = 0;
    }
}

bool env_state_configured()
{
    return env_configured(kGpsValidEnv) ||
           env_configured(kGpsLatEnv) ||
           env_configured(kGpsLngEnv) ||
           env_configured(kGpsAltEnv) ||
           env_configured(kGpsSpeedEnv) ||
           env_configured(kGpsCourseEnv) ||
           env_configured(kGpsSatCountEnv) ||
           env_configured(kGpsHdopEnv) ||
           env_configured(kGpsFixEnv);
}

bool auto_serial_probe_enabled()
{
    if (env_configured(kGpsAutoSerialEnv))
    {
        return env_flag_or_default(kGpsAutoSerialEnv, false);
    }

#if defined(__linux__)
    std::error_code ec;
    return std::filesystem::exists("/dev/spidev1.0", ec) && !ec;
#else
    return false;
#endif
}

std::vector<std::string> split_device_candidates(const char* text)
{
    std::vector<std::string> out;
    if (!text || text[0] == '\0')
    {
        return out;
    }

    const char* cursor = text;
    while (*cursor)
    {
        while (*cursor == ':' || *cursor == ';' || *cursor == ',' ||
               *cursor == ' ' || *cursor == '\t' || *cursor == '\n')
        {
            ++cursor;
        }

        const char* start = cursor;
        while (*cursor && *cursor != ':' && *cursor != ';' && *cursor != ',' &&
               *cursor != ' ' && *cursor != '\t' && *cursor != '\n')
        {
            ++cursor;
        }

        if (cursor > start)
        {
            out.emplace_back(start, static_cast<std::size_t>(cursor - start));
        }
    }
    return out;
}

#if defined(__linux__)
std::string gps_candidate_identity(const std::string& path)
{
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec && !canonical.empty())
    {
        return canonical.string();
    }
    ec.clear();
    const auto absolute = std::filesystem::absolute(path, ec);
    if (!ec && !absolute.empty())
    {
        return absolute.lexically_normal().string();
    }
    return path;
}

std::vector<std::string> discover_clockwork_pi_serial_candidates()
{
    std::vector<std::string> out;
    std::error_code ec;
    const std::filesystem::path by_id("/dev/serial/by-id");
    if (!std::filesystem::exists(by_id, ec) || ec)
    {
        return out;
    }

    for (const auto& entry : std::filesystem::directory_iterator(by_id, ec))
    {
        if (ec)
        {
            break;
        }

        const std::string name = entry.path().filename().string();
        if (name.find("ClockworkPI") == std::string::npos ||
            name.find("uConsole") == std::string::npos)
        {
            continue;
        }

        out.push_back(entry.path().string());
    }

    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> deduplicate_auto_serial_candidates(
    const std::vector<std::string>& candidates)
{
    std::vector<std::string> out;
    std::vector<std::string> identities;
    out.reserve(candidates.size());
    identities.reserve(candidates.size());

    for (const auto& candidate : candidates)
    {
        const std::string identity = gps_candidate_identity(candidate);
        if (std::find(identities.begin(), identities.end(), identity) !=
            identities.end())
        {
            continue;
        }
        identities.push_back(identity);
        out.push_back(candidate);
    }
    return out;
}

const std::vector<std::string>& auto_serial_candidates_locked()
{
    const char* env = std::getenv(kGpsDeviceCandidatesEnv);
    std::vector<std::string> candidates;
    if (env && env[0] != '\0')
    {
        candidates = split_device_candidates(env);
    }
    else
    {
        const auto aio2_candidates = discover_clockwork_pi_serial_candidates();
        candidates = aio2_candidates;
        const auto default_candidates =
            split_device_candidates(kDefaultGpsDeviceCandidates);
        candidates.insert(candidates.end(),
                          default_candidates.begin(),
                          default_candidates.end());
    }

    std::string signature;
    for (const auto& candidate : candidates)
    {
        if (!signature.empty())
        {
            signature.push_back(':');
        }
        signature += candidate;
    }
    if (signature != s_runtime.auto_candidate_signature)
    {
        s_runtime.auto_candidate_signature = signature;
        s_runtime.auto_candidate_paths =
            deduplicate_auto_serial_candidates(candidates);
        s_runtime.auto_candidate_index = 0;
        if (s_runtime.source_is_auto_candidate)
        {
            reset_source_locked();
        }
    }
    return s_runtime.auto_candidate_paths;
}

std::string select_auto_serial_candidate_locked()
{
    const auto& candidates = auto_serial_candidates_locked();
    if (candidates.empty())
    {
        return {};
    }

    std::error_code ec;
    for (std::size_t attempts = 0; attempts < candidates.size(); ++attempts)
    {
        const std::size_t index =
            (s_runtime.auto_candidate_index + attempts) % candidates.size();
        const auto& candidate = candidates[index];
        if (std::filesystem::exists(candidate, ec) && !ec)
        {
            s_runtime.auto_candidate_index = index;
            return candidate;
        }
        ec.clear();
    }

    return {};
}

std::string next_auto_serial_candidate_locked(const std::string& current)
{
    const auto& candidates = auto_serial_candidates_locked();
    if (candidates.empty())
    {
        return {};
    }

    std::size_t current_index = s_runtime.auto_candidate_index;
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        if (candidates[i] == current)
        {
            current_index = i;
            break;
        }
    }

    std::error_code ec;
    for (std::size_t step = 1; step <= candidates.size(); ++step)
    {
        const std::size_t index = (current_index + step) % candidates.size();
        const auto& candidate = candidates[index];
        if (candidate == current)
        {
            continue;
        }
        if (std::filesystem::exists(candidate, ec) && !ec)
        {
            s_runtime.auto_candidate_index = index;
            return candidate;
        }
        ec.clear();
    }

    return {};
}
#endif

::gps::GnssFix env_fix_or_default()
{
    switch (env_int_or_default(kGpsFixEnv, static_cast<int>(::gps::GnssFix::FIX3D)))
    {
    case 2:
        return ::gps::GnssFix::FIX2D;
    case 3:
        return ::gps::GnssFix::FIX3D;
    case 0:
    default:
        return ::gps::GnssFix::NOFIX;
    }
}

std::array<GnssSatInfo, 10> default_satellites()
{
    return {{
        {.id = 3, .sys = ::gps::GnssSystem::GPS, .azimuth = 18, .elevation = 74, .snr = 42, .used = true},
        {.id = 8, .sys = ::gps::GnssSystem::GPS, .azimuth = 84, .elevation = 58, .snr = 39, .used = true},
        {.id = 14, .sys = ::gps::GnssSystem::GPS, .azimuth = 156, .elevation = 41, .snr = 35, .used = true},
        {.id = 22, .sys = ::gps::GnssSystem::GPS, .azimuth = 286, .elevation = 49, .snr = 31, .used = true},
        {.id = 7, .sys = ::gps::GnssSystem::GLN, .azimuth = 312, .elevation = 68, .snr = 37, .used = true},
        {.id = 19, .sys = ::gps::GnssSystem::GAL, .azimuth = 218, .elevation = 55, .snr = 33, .used = true},
        {.id = 31, .sys = ::gps::GnssSystem::BD, .azimuth = 118, .elevation = 36, .snr = 29, .used = true},
        {.id = 35, .sys = ::gps::GnssSystem::BD, .azimuth = 246, .elevation = 28, .snr = 24, .used = false},
        {.id = 12, .sys = ::gps::GnssSystem::GAL, .azimuth = 35, .elevation = 22, .snr = 21, .used = false},
        {.id = 5, .sys = ::gps::GnssSystem::GLN, .azimuth = 172, .elevation = 16, .snr = 18, .used = false},
    }};
}

bool runtime_active()
{
    return !s_suspended && s_enabled && env_flag_or_default(kGpsEnabledEnv, true) &&
           env_flag_or_default(kGpsPoweredEnv, true);
}

GpsState make_default_state()
{
    GpsState state{};
    state.lat = env_double_or_default(kGpsLatEnv, kDefaultLat);
    state.lng = env_double_or_default(kGpsLngEnv, kDefaultLng);
    state.alt_m = env_double_or_default(kGpsAltEnv, kDefaultAltM);
    state.speed_mps = env_double_or_default(kGpsSpeedEnv, kDefaultSpeedMps);
    state.course_deg = env_double_or_default(kGpsCourseEnv, kDefaultCourseDeg);
    state.satellites = static_cast<uint8_t>(
        std::clamp(env_int_or_default(kGpsSatCountEnv, kDefaultSatCount), 0, 32));
    state.valid = env_flag_or_default(kGpsValidEnv, true);
    state.has_alt = true;
    state.has_speed = true;
    state.has_course = true;
    state.age = 0;
    return state;
}

GpsState make_env_state()
{
    GpsState state{};
    const bool has_lat = env_configured(kGpsLatEnv);
    const bool has_lng = env_configured(kGpsLngEnv);

    state.lat = env_double_or_default(kGpsLatEnv, 0.0);
    state.lng = env_double_or_default(kGpsLngEnv, 0.0);
    state.valid = env_flag_or_default(kGpsValidEnv, has_lat && has_lng) &&
                  has_lat && has_lng;
    state.alt_m = env_double_or_default(kGpsAltEnv, 0.0);
    state.speed_mps = env_double_or_default(kGpsSpeedEnv, 0.0);
    state.course_deg = env_double_or_default(kGpsCourseEnv, 0.0);
    state.satellites = static_cast<uint8_t>(
        std::clamp(env_int_or_default(kGpsSatCountEnv, 0), 0, 32));
    state.has_alt = env_configured(kGpsAltEnv);
    state.has_speed = env_configured(kGpsSpeedEnv);
    state.has_course = env_configured(kGpsCourseEnv);
    state.age = 0;
    return state;
}

GnssStatus make_default_status()
{
    GnssStatus status{};
    status.sats_in_use = static_cast<uint8_t>(
        std::clamp(env_int_or_default(kGpsSatCountEnv, kDefaultSatCount), 0, 32));
    status.sats_in_view = static_cast<uint8_t>(default_satellites().size());
    status.hdop = static_cast<float>(env_double_or_default(kGpsHdopEnv, kDefaultHdop));
    status.fix = env_fix_or_default();
    return status;
}

GnssStatus make_env_status()
{
    GnssStatus status{};
    const GpsState state = make_env_state();
    status.sats_in_use = static_cast<uint8_t>(
        std::clamp(env_int_or_default(kGpsSatCountEnv, 0), 0, 32));
    status.sats_in_view = status.sats_in_use;
    status.hdop = static_cast<float>(env_double_or_default(kGpsHdopEnv, 0.0));
    status.fix = env_configured(kGpsFixEnv)
                     ? env_fix_or_default()
                     : (state.valid ? ::gps::GnssFix::FIX3D
                                    : ::gps::GnssFix::NOFIX);
    if (env_configured(kGpsValidEnv) &&
        !env_flag_or_default(kGpsValidEnv, false))
    {
        status.fix = ::gps::GnssFix::NOFIX;
    }
    return status;
}

void clear_payload_locked()
{
    s_runtime.data = GpsState{};
    s_runtime.status = GnssStatus{};
    s_runtime.sat_count = 0;
    s_runtime.used_sat_count = 0;
    s_runtime.last_rx_ms = 0;
    s_runtime.last_fix_ms = 0;
    for (auto& collector : s_runtime.gsv)
    {
        collector = GsvCollector{};
    }
}

CollectorSlot collector_slot_for_talker(const char* talker)
{
    if (!talker || talker[0] == '\0' || talker[1] == '\0')
    {
        return CollectorSlot::UNKNOWN;
    }
    if (talker[0] == 'G' && talker[1] == 'P')
    {
        return CollectorSlot::GPS;
    }
    if (talker[0] == 'G' && talker[1] == 'L')
    {
        return CollectorSlot::GLN;
    }
    if (talker[0] == 'G' && talker[1] == 'A')
    {
        return CollectorSlot::GAL;
    }
    if ((talker[0] == 'G' && talker[1] == 'B') || (talker[0] == 'B' && talker[1] == 'D'))
    {
        return CollectorSlot::BD;
    }
    return CollectorSlot::UNKNOWN;
}

::gps::GnssSystem system_for_slot(CollectorSlot slot)
{
    switch (slot)
    {
    case CollectorSlot::GPS:
        return ::gps::GnssSystem::GPS;
    case CollectorSlot::GLN:
        return ::gps::GnssSystem::GLN;
    case CollectorSlot::GAL:
        return ::gps::GnssSystem::GAL;
    case CollectorSlot::BD:
        return ::gps::GnssSystem::BD;
    default:
        return ::gps::GnssSystem::UNKNOWN;
    }
}

bool parse_uint(const char* text, uint32_t* out)
{
    if (!text || !*text || !out)
    {
        return false;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text)
    {
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

bool parse_int(const char* text, int* out)
{
    if (!text || !*text || !out)
    {
        return false;
    }
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text)
    {
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool parse_double(const char* text, double* out)
{
    if (!text || !*text || !out)
    {
        return false;
    }
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text)
    {
        return false;
    }
    *out = value;
    return true;
}

bool parse_latlon(const char* value_text, const char* hemi_text, bool latitude, double* out)
{
    if (!value_text || !*value_text || !hemi_text || !*hemi_text || !out)
    {
        return false;
    }
    double raw = 0.0;
    if (!parse_double(value_text, &raw))
    {
        return false;
    }
    const int degrees = static_cast<int>(raw / 100.0);
    const double minutes = raw - (static_cast<double>(degrees) * 100.0);
    double decimal = static_cast<double>(degrees) + (minutes / 60.0);
    const char hemi = hemi_text[0];
    if (hemi == 'S' || hemi == 'W')
    {
        decimal = -decimal;
    }
    else if ((latitude && hemi != 'N') || (!latitude && hemi != 'E'))
    {
        return false;
    }
    *out = decimal;
    return true;
}

bool verify_checksum(const char* line)
{
    if (!line || line[0] != '$')
    {
        return false;
    }
    const char* star = std::strchr(line, '*');
    if (!star || !star[1] || !star[2])
    {
        return true;
    }
    uint8_t checksum = 0;
    for (const char* cursor = line + 1; cursor < star; ++cursor)
    {
        checksum ^= static_cast<uint8_t>(*cursor);
    }
    char text[3] = {star[1], star[2], '\0'};
    char* end = nullptr;
    const long expected = std::strtol(text, &end, 16);
    return end != text && checksum == static_cast<uint8_t>(expected & 0xFF);
}

std::string sentence_checksum_text(const char* line)
{
    if (!line)
    {
        return {};
    }
    const char* star = std::strchr(line, '*');
    if (!star || !star[1] || !star[2])
    {
        return {};
    }
    return std::string(star + 1, 2);
}

void append_nmea_log_locked(const char* sentence, bool checksum_ok)
{
    if (!sentence || sentence[0] != '$')
    {
        return;
    }

    const char* star = std::strchr(sentence, '*');
    const char* comma = std::strchr(sentence, ',');
    const char* header_end = comma != nullptr ? comma : (star != nullptr ? star : sentence + std::strlen(sentence));
    std::string header(sentence, static_cast<std::size_t>(header_end - sentence));
    std::string body{};
    if (comma != nullptr)
    {
        const char* body_end = star != nullptr ? star : sentence + std::strlen(sentence);
        body.assign(comma + 1, static_cast<std::size_t>(body_end - comma - 1));
    }

    ::platform::linux_runtime::PacketLogEntry entry{};
    entry.source = ::platform::linux_runtime::PacketLogSource::Gps;
    entry.direction = ::platform::linux_runtime::PacketLogDirection::Rx;
    entry.title = header;
    entry.summary = checksum_ok ? "NMEA sentence parsed"
                                : "NMEA checksum failed";
    entry.raw_hex =
        ::platform::linux_runtime::hex_bytes(sentence, std::strlen(sentence));
    entry.segments.push_back({
        .kind = ::platform::linux_runtime::PacketLogSegmentKind::Header,
        .label = "head",
        .text = header,
    });
    if (!body.empty())
    {
        entry.segments.push_back({
            .kind = ::platform::linux_runtime::PacketLogSegmentKind::Body,
            .label = "body",
            .text = body,
        });
    }
    const std::string checksum = sentence_checksum_text(sentence);
    if (!checksum.empty())
    {
        entry.segments.push_back({
            .kind = checksum_ok
                        ? ::platform::linux_runtime::PacketLogSegmentKind::Checksum
                        : ::platform::linux_runtime::PacketLogSegmentKind::Error,
            .label = "sum",
            .text = checksum,
        });
    }
    ::platform::linux_runtime::append_packet_log(std::move(entry));
}

void append_gps_system_log_locked(
    const char* title,
    const std::string& summary,
    std::initializer_list<::platform::linux_runtime::PacketLogSegment> segments = {})
{
    std::vector<::platform::linux_runtime::PacketLogSegment> vector_segments;
    vector_segments.insert(vector_segments.end(), segments.begin(), segments.end());
    append_gps_system_log_locked(title, summary, vector_segments);
}

void append_gps_system_log_locked(
    const char* title,
    const std::string& summary,
    const std::vector<::platform::linux_runtime::PacketLogSegment>& segments)
{
    ::platform::linux_runtime::PacketLogEntry entry{};
    entry.source = ::platform::linux_runtime::PacketLogSource::Gps;
    entry.direction = ::platform::linux_runtime::PacketLogDirection::System;
    entry.title = title ? title : "GPS";
    entry.summary = summary;
    entry.segments.insert(entry.segments.end(), segments.begin(), segments.end());
    ::platform::linux_runtime::append_packet_log(std::move(entry));
}

std::size_t split_fields(char* sentence, std::array<char*, kMaxFields>& fields)
{
    fields.fill(nullptr);
    std::size_t count = 0;
    char* cursor = sentence;
    while (cursor && *cursor && count < fields.size())
    {
        fields[count++] = cursor;
        char* comma = std::strchr(cursor, ',');
        if (!comma)
        {
            break;
        }
        *comma = '\0';
        cursor = comma + 1;
    }
    return count;
}

bool used_sat_locked(uint16_t sat_id)
{
    for (std::size_t i = 0; i < s_runtime.used_sat_count; ++i)
    {
        if (s_runtime.used_sat_ids[i] == sat_id)
        {
            return true;
        }
    }
    return false;
}

void merge_sats_locked()
{
    s_runtime.sat_count = 0;
    for (std::size_t ci = 0; ci < s_runtime.gsv.size(); ++ci)
    {
        auto& collector = s_runtime.gsv[ci];
        for (std::size_t si = 0; si < collector.count && s_runtime.sat_count < s_runtime.sats.size(); ++si)
        {
            auto sat = collector.sats[si];
            sat.used = used_sat_locked(sat.id);
            s_runtime.sats[s_runtime.sat_count++] = sat;
        }
    }
    s_runtime.status.sats_in_view = static_cast<uint8_t>(std::min<std::size_t>(s_runtime.sat_count, 255));
}

void parse_rmc_locked(const std::array<char*, kMaxFields>& fields, std::size_t count, uint32_t ts)
{
    if (count < 9)
    {
        return;
    }

    const bool active = fields[2] && fields[2][0] == 'A';
    s_runtime.data.valid = active;
    if (!active)
    {
        return;
    }

    double lat = 0.0;
    double lng = 0.0;
    double speed_knots = 0.0;
    double course = 0.0;

    if (parse_latlon(fields[3], fields[4], true, &lat))
    {
        s_runtime.data.lat = lat;
    }
    if (parse_latlon(fields[5], fields[6], false, &lng))
    {
        s_runtime.data.lng = lng;
    }
    if (parse_double(fields[7], &speed_knots))
    {
        s_runtime.data.speed_mps = speed_knots * 0.514444;
        s_runtime.data.has_speed = true;
    }
    else
    {
        s_runtime.data.has_speed = false;
    }
    if (parse_double(fields[8], &course))
    {
        s_runtime.data.course_deg = course;
        s_runtime.data.has_course = true;
    }
    else
    {
        s_runtime.data.has_course = false;
    }

    s_runtime.last_fix_ms = ts;
    s_runtime.last_motion_ms = ts;
}

void parse_gga_locked(const std::array<char*, kMaxFields>& fields, std::size_t count, uint32_t ts)
{
    if (count < 10)
    {
        return;
    }

    int quality = 0;
    if (parse_int(fields[6], &quality))
    {
        s_runtime.data.valid = quality > 0;
    }

    uint32_t satellites = 0;
    if (parse_uint(fields[7], &satellites))
    {
        s_runtime.data.satellites = static_cast<uint8_t>(std::min<uint32_t>(satellites, 255));
        s_runtime.status.sats_in_use = s_runtime.data.satellites;
    }

    double hdop = 0.0;
    double altitude = 0.0;
    if (parse_double(fields[8], &hdop))
    {
        s_runtime.status.hdop = static_cast<float>(hdop);
    }
    if (parse_double(fields[9], &altitude))
    {
        s_runtime.data.alt_m = altitude;
        s_runtime.data.has_alt = true;
    }
    else
    {
        s_runtime.data.has_alt = false;
    }

    if (s_runtime.data.valid)
    {
        s_runtime.last_fix_ms = ts;
        s_runtime.last_motion_ms = ts;
    }
}

void parse_gsa_locked(const std::array<char*, kMaxFields>& fields, std::size_t count)
{
    if (count < 4)
    {
        return;
    }

    s_runtime.used_sat_count = 0;
    for (std::size_t i = 3; i <= 14 && i < count; ++i)
    {
        uint32_t sat_id = 0;
        if (parse_uint(fields[i], &sat_id) && sat_id > 0 &&
            s_runtime.used_sat_count < s_runtime.used_sat_ids.size())
        {
            s_runtime.used_sat_ids[s_runtime.used_sat_count++] = static_cast<uint16_t>(sat_id);
        }
    }

    int fix_type = 0;
    if (parse_int(fields[2], &fix_type))
    {
        s_runtime.status.fix = fix_type >= 3 ? ::gps::GnssFix::FIX3D
                                             : (fix_type == 2 ? ::gps::GnssFix::FIX2D
                                                              : ::gps::GnssFix::NOFIX);
    }
    if (count > 16)
    {
        double hdop = 0.0;
        if (parse_double(fields[16], &hdop))
        {
            s_runtime.status.hdop = static_cast<float>(hdop);
        }
    }

    s_runtime.status.sats_in_use =
        static_cast<uint8_t>(std::min<std::size_t>(s_runtime.used_sat_count, 255));
    merge_sats_locked();
}

void parse_gsv_locked(const char* talker, const std::array<char*, kMaxFields>& fields, std::size_t count)
{
    if (count < 4)
    {
        return;
    }

    auto& collector =
        s_runtime.gsv[static_cast<std::size_t>(collector_slot_for_talker(talker))];
    uint32_t msg_num = 0;
    if (!parse_uint(fields[2], &msg_num))
    {
        return;
    }
    if (msg_num == 1)
    {
        collector.count = 0;
    }

    for (std::size_t base = 4; base + 3 < count && collector.count < collector.sats.size(); base += 4)
    {
        uint32_t sat_id = 0;
        if (!parse_uint(fields[base], &sat_id) || sat_id == 0)
        {
            continue;
        }
        GnssSatInfo sat{};
        sat.id = static_cast<uint16_t>(sat_id);
        sat.sys = system_for_slot(collector_slot_for_talker(talker));
        uint32_t elev = 0;
        uint32_t az = 0;
        int snr = -1;
        if (parse_uint(fields[base + 1], &elev))
        {
            sat.elevation = static_cast<uint8_t>(std::min<uint32_t>(elev, 90));
        }
        if (parse_uint(fields[base + 2], &az))
        {
            sat.azimuth = static_cast<uint16_t>(std::min<uint32_t>(az, 359));
        }
        if (parse_int(fields[base + 3], &snr))
        {
            sat.snr = static_cast<int8_t>(std::clamp(snr, -1, 99));
        }
        collector.sats[collector.count++] = sat;
    }

    merge_sats_locked();
}

void parse_sentence_locked(const char* sentence, uint32_t ts)
{
    if (s_runtime.slm_serial)
    {
        GpsState fix;
        if (sentence && s_runtime.slm.line(sentence, ts, fix))
        {
            s_runtime.data = fix;
            s_runtime.status.fix = ::gps::GnssFix::FIX3D;
            s_runtime.last_rx_ms = s_runtime.last_fix_ms = ts;
            if (fix.speed_mps > 0.5) s_runtime.last_motion_ms = ts;
        }
        ::platform::linux_runtime::publish_gnss_source_ready(s_runtime.slm.ready());
        return;
    }
    const bool checksum_ok = verify_checksum(sentence);
    if (sentence && sentence[0] == '$')
    {
        append_nmea_log_locked(sentence, checksum_ok);
    }
    if (!sentence || sentence[0] != '$' || !checksum_ok)
    {
        return;
    }

    char working[kLineBufferSize] = {};
    const std::size_t copy_len =
        std::min<std::size_t>(std::strlen(sentence + 1), sizeof(working) - 1);
    std::memcpy(working, sentence + 1, copy_len);
    char* star = std::strchr(working, '*');
    if (star)
    {
        *star = '\0';
    }

    std::array<char*, kMaxFields> fields{};
    const std::size_t count = split_fields(working, fields);
    if (count == 0 || !fields[0] || std::strlen(fields[0]) < 5)
    {
        return;
    }

    char talker[3] = {fields[0][0], fields[0][1], '\0'};
    const char* type = fields[0] + std::strlen(fields[0]) - 3;
    s_runtime.last_rx_ms = ts;

    if (std::strcmp(type, "RMC") == 0)
    {
        parse_rmc_locked(fields, count, ts);
    }
    else if (std::strcmp(type, "GGA") == 0)
    {
        parse_gga_locked(fields, count, ts);
    }
    else if (std::strcmp(type, "GSA") == 0)
    {
        parse_gsa_locked(fields, count);
    }
    else if (std::strcmp(type, "GSV") == 0)
    {
        parse_gsv_locked(talker, fields, count);
    }
}

void append_bytes_and_process_locked(const char* buffer, std::size_t length)
{
    if (!buffer || length == 0)
    {
        return;
    }

    const uint32_t ts = now_ms();
    s_runtime.chars_total += static_cast<uint32_t>(
        std::min<std::size_t>(length, std::numeric_limits<uint32_t>::max()));
    update_recent_traffic_window_locked(ts);
    s_runtime.chars_recent += static_cast<uint32_t>(
        std::min<std::size_t>(length, std::numeric_limits<uint32_t>::max()));

    for (std::size_t i = 0; i < length; ++i)
    {
        const char ch = buffer[i];
        if (ch == '\r' || ch == '\n')
        {
            if (!s_runtime.line_overflow && s_runtime.line_length > 0)
            {
                s_runtime.line_buffer[s_runtime.line_length] = '\0';
                parse_sentence_locked(s_runtime.line_buffer.data(), now_ms());
                s_runtime.line_length = 0;
            }
            s_runtime.line_length = 0;
            s_runtime.line_overflow = false;
            continue;
        }
        if (s_runtime.line_overflow) continue;

        if (s_runtime.line_length == 0 && ch != '$' && !s_runtime.slm_serial)
        {
            continue;
        }

        if (s_runtime.line_length + 1 < s_runtime.line_buffer.size())
        {
            s_runtime.line_buffer[s_runtime.line_length++] = ch;
        }
        else
        {
            s_runtime.line_length = 0;
            s_runtime.line_overflow = true;
        }
    }
}

std::string requested_source_path(bool* out_is_serial,
                                  bool* out_is_auto_candidate = nullptr)
{
    if (out_is_serial)
    {
        *out_is_serial = false;
    }
    if (out_is_auto_candidate)
    {
        *out_is_auto_candidate = false;
    }

    const char* device = std::getenv(kGpsDeviceEnv);
    if (device && device[0] != '\0')
    {
        if (out_is_serial)
        {
            *out_is_serial = true;
        }
        return std::string(device);
    }

    const char* file = std::getenv(kGpsNmeaFileEnv);
    if (file && file[0] != '\0')
    {
        return std::string(file);
    }

    const auto configured = ::platform::linux_runtime::gnss_source_options();
    if (!configured.device.empty())
    {
        if (out_is_serial) *out_is_serial = true;
        return configured.device;
    }

#if defined(__linux__)
    if (!auto_serial_probe_enabled())
    {
        return {};
    }

    const std::string candidate = select_auto_serial_candidate_locked();
    if (!candidate.empty())
    {
        if (out_is_serial)
        {
            *out_is_serial = true;
        }
        if (out_is_auto_candidate)
        {
            *out_is_auto_candidate = true;
        }
        return candidate;
    }
#endif

    return {};
}

#if defined(__linux__)
int serial_baud_for_path(const std::string& path)
{
    const auto configured = ::platform::linux_runtime::gnss_source_options();
    if (s_receiver_init_config.baud != 0 &&
        supported_baud(static_cast<int>(s_receiver_init_config.baud)))
    {
        return static_cast<int>(s_receiver_init_config.baud);
    }
    if (configured.baud && !env_configured(kGpsBaudEnv)) return configured.baud;

    const int default_baud =
        (!env_configured(kGpsBaudEnv) &&
         (path == "/dev/ttyS0" || path == "/dev/serial0" ||
          path.find("ClockworkPI") != std::string::npos ||
          path.find("uConsole") != std::string::npos))
            ? 9600
            : 38400;
    const int env_baud = env_int_or_default(kGpsBaudEnv, default_baud);
    return supported_baud(env_baud) ? env_baud : default_baud;
}

speed_t baud_to_termios(int baud)
{
    switch (baud)
    {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    case 38400:
    default:
        return B38400;
    }
}

void close_serial_locked()
{
    if (s_runtime.serial_fd >= 0)
    {
        if (s_runtime.slm_serial)
        {
            // Best-effort stop without blocking the UI/service thread on an
            // unresponsive USB modem. This is not a modem power-off claim.
            static constexpr char stop[] = "AT#XGPS=0\r\n";
            (void)write(s_runtime.serial_fd, stop, sizeof(stop) - 1);
        }
        close(s_runtime.serial_fd);
        s_runtime.serial_fd = -1;
    }
    s_runtime.slm_serial = false;
    s_runtime.slm_retry_ms = 0;
    s_runtime.slm.reset();
    ::platform::linux_runtime::publish_gnss_source_ready(false);
}

bool open_serial_locked(const std::string& path, int* out_error)
{
    close_serial_locked();

    const auto configured = ::platform::linux_runtime::gnss_source_options();
    const bool slm = configured.protocol == "nordic_slm";
    int selection_error = 0;
    const auto resolved = ::platform::linux_runtime::resolve_gnss_serial_device(path, selection_error);
    if (resolved.empty())
    {
        if (out_error) *out_error = selection_error ? selection_error : ENOENT;
        return false;
    }
    const int fd = open(resolved.c_str(), (slm ? O_RDWR : O_RDONLY) | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
    {
        if (out_error)
        {
            *out_error = errno;
        }
        return false;
    }

    if (slm && flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        if (out_error) *out_error = errno;
        close(fd);
        return false;
    }

    termios tio{};
    if (tcgetattr(fd, &tio) != 0)
    {
        if (out_error)
        {
            *out_error = errno;
        }
        close(fd);
        return false;
    }

    cfmakeraw(&tio);
    const int configured_baud = serial_baud_for_path(path);
    const speed_t baud = baud_to_termios(configured_baud);
    cfsetispeed(&tio, baud);
    cfsetospeed(&tio, baud);
    tio.c_cflag |= (CLOCAL | CREAD);
#ifdef CRTSCTS
    if (slm) tio.c_cflag &= ~CRTSCTS;
#endif
    if (tcsetattr(fd, TCSANOW, &tio) != 0)
    {
        if (out_error)
        {
            *out_error = errno;
        }
        close(fd);
        return false;
    }

    s_runtime.serial_fd = fd;
    s_runtime.slm_serial = slm;
    if (configured.modem_lines)
    {
        int lines = TIOCM_DTR | TIOCM_RTS;
        (void)ioctl(fd, TIOCMBIS, &lines);
    }
    if (slm) s_runtime.slm.start(configured.gnss_only_setup, now_ms());
    if (out_error)
    {
        *out_error = 0;
    }
    return true;
}
#endif

void reset_source_locked()
{
#if defined(__linux__)
    close_serial_locked();
#endif
    s_runtime.file_offset = 0;
    s_runtime.line_length = 0;
    s_runtime.line_overflow = false;
    s_runtime.source_path.clear();
    s_runtime.source_is_serial = false;
    s_runtime.source_is_auto_candidate = false;
    s_runtime.source_opened_ms = 0;
    s_runtime.last_serial_byte_ms = 0;
    s_runtime.waiting_log_emitted = false;
    clear_payload_locked();
}

void rotate_auto_candidate_after_no_traffic_locked(uint32_t now)
{
#if defined(__linux__)
    if (!s_runtime.source_is_serial || !s_runtime.source_is_auto_candidate ||
        s_runtime.source_opened_ms == 0 || s_runtime.last_serial_byte_ms != 0 ||
        s_runtime.last_rx_ms != 0 ||
        (now - s_runtime.source_opened_ms) < kAutoSerialNoNmeaFailoverMs)
    {
        return;
    }

    const std::string failed_path = s_runtime.source_path;
    const std::string next_path = next_auto_serial_candidate_locked(failed_path);
    const auto& candidates = auto_serial_candidates_locked();
    append_gps_system_log_locked(
        "GPS serial no traffic",
        next_path.empty()
            ? "Serial source had no NMEA bytes; no alternate candidate is available"
            : "Serial source had no NMEA bytes; trying next candidate",
        {
            {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
             .label = "path",
             .text = failed_path},
            {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
             .label = "next_path",
             .text = next_path.empty() ? "none" : next_path},
            {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
             .label = "candidate_index",
             .text = std::to_string(s_runtime.auto_candidate_index)},
            {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
             .label = "candidate_count",
             .text = std::to_string(candidates.size())},
        });

    reset_source_locked();
#else
    (void)now;
#endif
}

void ensure_source_open_locked()
{
    bool requested_serial = false;
    bool requested_auto_candidate = false;
    const std::string requested_path =
        requested_source_path(&requested_serial, &requested_auto_candidate);
    if (requested_path.empty())
    {
        if (!s_runtime.source_path.empty())
        {
            reset_source_locked();
        }
        if (s_runtime.power) s_runtime.power->request(false);
        s_power_ready = false;
        return;
    }

    const auto power_options = ::platform::linux_runtime::gnss_source_options().power;
    if (requested_serial && power_options.line >= 0)
    {
        if (!s_runtime.power) s_runtime.power = ::platform::linux_runtime::make_gnss_power_control();
        s_runtime.power->request(true);
        const auto power = s_runtime.power->snapshot();
        s_power_ready = power.state == ::platform::linux_runtime::AsyncGpioPower::State::Ready;
        if (power.state != ::platform::linux_runtime::AsyncGpioPower::State::Ready)
        {
            ::platform::linux_runtime::publish_gnss_source_ready(false);
            if (power.error && power.error != s_runtime.last_power_error)
                append_gps_system_log_locked("GPS power unavailable", "GNSS enable line could not be acquired",
                                             {{.kind = ::platform::linux_runtime::PacketLogSegmentKind::Error, .label = "errno", .text = std::to_string(power.error)}});
            s_runtime.last_power_error = power.error;
            return;
        }
        s_runtime.last_power_error = 0;
    }
    else if (s_runtime.power)
    {
#if defined(__linux__)
        close_serial_locked();
#endif
        s_runtime.power->request(false);
        s_power_ready = false;
    }

    const bool source_changed =
        requested_path != s_runtime.source_path ||
        requested_serial != s_runtime.source_is_serial ||
        requested_auto_candidate != s_runtime.source_is_auto_candidate;
    if (source_changed)
    {
        reset_source_locked();
        s_runtime.source_path = requested_path;
        s_runtime.source_is_serial = requested_serial;
        s_runtime.source_is_auto_candidate = requested_auto_candidate;
#if defined(__linux__)
        if (requested_serial)
        {
            int open_error = 0;
            if (!open_serial_locked(requested_path, &open_error))
            {
                const uint32_t now = now_ms();
                const bool should_log =
                    s_runtime.last_open_failure_path != requested_path ||
                    s_runtime.last_open_failure_log_ms == 0 ||
                    (now - s_runtime.last_open_failure_log_ms) > 5000U;
                if (should_log)
                {
                    append_gps_system_log_locked(
                        "GPS source open failed",
                        "Serial source could not be opened",
                        {
                            {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                             .label = "path",
                             .text = requested_path},
                            {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                             .label = "baud",
                             .text = std::to_string(serial_baud_for_path(requested_path))},
                            {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                             .label = "errno",
                             .text = std::to_string(open_error)},
                        });
                    s_runtime.last_open_failure_path = requested_path;
                    s_runtime.last_open_failure_log_ms = now;
                }
                s_runtime.source_path.clear();
                s_runtime.source_is_serial = false;
                s_runtime.source_is_auto_candidate = false;
                if (requested_auto_candidate)
                {
                    (void)next_auto_serial_candidate_locked(requested_path);
                }
            }
            else
            {
                s_runtime.source_opened_ms = now_ms();
                s_runtime.last_serial_byte_ms = 0;
                s_runtime.waiting_log_emitted = false;
                auto segments = std::vector<::platform::linux_runtime::PacketLogSegment>{
                    {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                     .label = "path",
                     .text = requested_path},
                    {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                     .label = "baud",
                     .text = std::to_string(serial_baud_for_path(requested_path))},
                    {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                     .label = "baud_source",
                     .text = effective_receiver_init_source()},
                };
                if (requested_auto_candidate)
                {
                    const auto& candidates = auto_serial_candidates_locked();
                    segments.push_back(
                        {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                         .label = "candidate_index",
                         .text = std::to_string(s_runtime.auto_candidate_index)});
                    segments.push_back(
                        {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                         .label = "candidate_count",
                         .text = std::to_string(candidates.size())});
                }
                append_gps_system_log_locked(
                    "GPS source opened",
                    "Serial source opened for NMEA input",
                    segments);
            }
        }
#else
        if (requested_serial)
        {
            append_gps_system_log_locked(
                "GPS source unsupported",
                "Serial GPS source requested on a non-Linux runtime",
                {
                    {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                     .label = "path",
                     .text = requested_path},
                });
            s_runtime.source_path.clear();
            s_runtime.source_is_serial = false;
            s_runtime.source_is_auto_candidate = false;
        }
#endif
        if (!requested_serial && !s_runtime.source_path.empty())
        {
            append_gps_system_log_locked(
                "GPS source opened",
                "NMEA file source opened",
                {
                    {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                     .label = "path",
                     .text = requested_path},
                });
        }
    }
}

void poll_file_source_locked()
{
    if (s_runtime.source_path.empty())
    {
        return;
    }

    std::ifstream stream(s_runtime.source_path, std::ios::binary);
    if (!stream.is_open())
    {
        return;
    }

    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size < 0)
    {
        return;
    }
    if (static_cast<std::streamoff>(s_runtime.file_offset) > size)
    {
        s_runtime.file_offset = 0;
    }
    stream.seekg(static_cast<std::streamoff>(s_runtime.file_offset), std::ios::beg);

    char buffer[256];
    while (stream.good())
    {
        stream.read(buffer, static_cast<std::streamsize>(sizeof(buffer)));
        const std::streamsize got = stream.gcount();
        if (got <= 0)
        {
            break;
        }
        append_bytes_and_process_locked(buffer, static_cast<std::size_t>(got));
        s_runtime.file_offset += static_cast<std::size_t>(got);
    }
}

#if defined(__linux__)
void poll_serial_source_locked()
{
    if (s_runtime.serial_fd < 0)
    {
        return;
    }

    char buffer[256];
    for (unsigned batch = 0; batch < 16; ++batch)
    {
        const ssize_t got = read(s_runtime.serial_fd, buffer, sizeof(buffer));
        if (got > 0)
        {
            s_runtime.last_serial_byte_ms = now_ms();
            append_bytes_and_process_locked(buffer, static_cast<std::size_t>(got));
            continue;
        }
        if (got == 0)
        {
            reset_source_locked();
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            break;
        }
        const int read_error = errno;
        const std::string failed_path = s_runtime.source_path;
        append_gps_system_log_locked(
            "GPS serial read failed",
            "Serial source read returned an error; closing source",
            {
                {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                 .label = "path",
                 .text = failed_path},
                {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Error,
                 .label = "errno",
                 .text = std::to_string(read_error)},
            });
        reset_source_locked();
        break;
    }

    const uint32_t now = now_ms();
    if (s_runtime.slm_serial)
    {
        if (s_runtime.slm.failed())
        {
            if (!s_runtime.slm_retry_ms) s_runtime.slm_retry_ms = now + 30000;
            if (static_cast<int32_t>(now - s_runtime.slm_retry_ms) >= 0)
            {
                s_runtime.slm.start(::platform::linux_runtime::gnss_source_options().gnss_only_setup, now);
                s_runtime.slm_retry_ms = 0;
            }
        }
        const auto command = s_runtime.slm.pending(now);
        ::platform::linux_runtime::publish_gnss_source_ready(s_runtime.slm.ready());
        if (s_runtime.slm.failed())
        {
            s_runtime.data.valid = false;
            s_runtime.status.fix = ::gps::GnssFix::NOFIX;
        }
        if (!command.empty())
        {
            const auto sent = write(s_runtime.serial_fd, command.data(), command.size());
            if (sent > 0) s_runtime.slm.wrote(static_cast<std::size_t>(sent), now);
        }
    }
    if (!s_runtime.waiting_log_emitted &&
        s_runtime.source_opened_ms != 0 &&
        s_runtime.last_rx_ms == 0 &&
        (now - s_runtime.source_opened_ms) >= 1000U)
    {
        append_gps_system_log_locked(
            s_runtime.slm_serial ? "GPS modem waiting for position" : "GPS serial waiting for NMEA",
            s_runtime.slm_serial ? "Waiting for SLM initialization and a valid #XGPS position record" : s_runtime.last_serial_byte_ms == 0 ? "Serial source is open; no NMEA bytes have arrived yet"
                                                                                                                                           : "Serial bytes have arrived; waiting for a complete NMEA sentence",
            {
                {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                 .label = "path",
                 .text = s_runtime.source_path},
                {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                 .label = "last_byte_age_ms",
                 .text = s_runtime.last_serial_byte_ms == 0
                             ? "none"
                             : std::to_string(now - s_runtime.last_serial_byte_ms)},
            });
        s_runtime.waiting_log_emitted = true;
    }
}
#endif

void poll_external_source_locked()
{
    ensure_source_open_locked();
    if (s_runtime.source_path.empty())
    {
        return;
    }

    if (s_runtime.source_is_serial)
    {
#if defined(__linux__)
        poll_serial_source_locked();
        rotate_auto_candidate_after_no_traffic_locked(now_ms());
#endif
        return;
    }

    poll_file_source_locked();
}

void log_missing_source_once_locked()
{
    if (s_runtime.no_source_log_emitted)
    {
        return;
    }
    if (!requested_source_path(nullptr).empty() || env_state_configured() ||
        demo_defaults_enabled())
    {
        return;
    }

    append_gps_system_log_locked(
        "GPS source missing",
        "GPS service is running, but no live serial or NMEA file source is configured",
        {
            {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
             .label = "auto_serial",
             .text = auto_serial_probe_enabled() ? "1" : "0"},
        });
    s_runtime.no_source_log_emitted = true;
}

bool external_source_active_locked()
{
    return !s_runtime.source_path.empty();
}

GpsState make_external_state_locked()
{
    GpsState state = s_runtime.data;
    if (s_runtime.last_fix_ms != 0)
    {
        state.age = now_ms() - s_runtime.last_fix_ms;
        if (state.age > kExternalSourceStaleMs)
        {
            state.valid = false;
        }
    }
    return state;
}

} // namespace

GpsState get_data()
{
    if (!runtime_active())
    {
        return {};
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    bool requested_serial = false;
    const bool source_requested =
        !requested_source_path(&requested_serial).empty();

    if (external_source_active_locked())
    {
        return make_external_state_locked();
    }
    if (source_requested)
    {
        return {};
    }

    if (env_state_configured())
    {
        return make_env_state();
    }

    if (!demo_defaults_enabled())
    {
        return {};
    }

    if (s_runtime.last_motion_ms == 0)
    {
        s_runtime.last_motion_ms = now_ms();
    }
    return make_default_state();
}

bool get_gnss_snapshot(GnssSatInfo* out, std::size_t max, std::size_t* out_count, GnssStatus* status)
{
    if (!runtime_active())
    {
        if (out_count)
        {
            *out_count = 0;
        }
        if (status)
        {
            *status = GnssStatus{};
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    bool requested_serial = false;
    const bool source_requested =
        !requested_source_path(&requested_serial).empty();

    if (external_source_active_locked())
    {
        const bool stale =
            s_runtime.last_rx_ms == 0 || (now_ms() - s_runtime.last_rx_ms) > kExternalSourceStaleMs;
        if (stale)
        {
            if (out_count)
            {
                *out_count = 0;
            }
            if (status)
            {
                *status = GnssStatus{};
            }
            return false;
        }

        const std::size_t count = std::min(max, s_runtime.sat_count);
        if (out && count > 0)
        {
            std::copy_n(s_runtime.sats.begin(), count, out);
        }
        if (out_count)
        {
            *out_count = count;
        }
        if (status)
        {
            *status = s_runtime.status;
        }
        if (s_runtime.last_motion_ms == 0)
        {
            s_runtime.last_motion_ms = now_ms();
        }
        return true;
    }
    if (source_requested)
    {
        if (out_count)
        {
            *out_count = 0;
        }
        if (status)
        {
            *status = GnssStatus{};
        }
        return false;
    }

    if (env_state_configured())
    {
        if (out_count)
        {
            *out_count = 0;
        }
        if (status)
        {
            *status = make_env_status();
        }
        return true;
    }

    if (!demo_defaults_enabled())
    {
        if (out_count)
        {
            *out_count = 0;
        }
        if (status)
        {
            *status = GnssStatus{};
        }
        return false;
    }

    const auto satellites = default_satellites();
    const std::size_t count = std::min(max, satellites.size());
    if (out && count > 0)
    {
        std::copy_n(satellites.begin(), count, out);
    }
    if (out_count)
    {
        *out_count = count;
    }
    if (status)
    {
        *status = make_default_status();
    }
    if (s_runtime.last_motion_ms == 0)
    {
        s_runtime.last_motion_ms = now_ms();
    }
    return true;
}

GpsDiagnosticsSnapshot diagnostics()
{
    GpsDiagnosticsSnapshot snapshot{};
    snapshot.supported = runtime_active();
    snapshot.enabled = is_enabled();
    snapshot.powered = is_powered();
    snapshot.collection_interval_ms = s_collection_interval_ms;
    snapshot.poll_interval_ms = 1000;

    if (!snapshot.supported)
    {
        snapshot.code = ::gps::GpsDiagnosticCode::Disabled;
        return snapshot;
    }
    if (!snapshot.enabled)
    {
        snapshot.code = ::gps::GpsDiagnosticCode::NotEnabled;
        return snapshot;
    }
    if (!snapshot.powered)
    {
        snapshot.code = ::gps::GpsDiagnosticCode::PowerOff;
        return snapshot;
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    bool requested_serial = false;
    const bool source_requested =
        !requested_source_path(&requested_serial).empty();

    if (external_source_active_locked())
    {
        snapshot.ready = !s_runtime.slm_serial || s_runtime.slm.ready();
        snapshot.has_fix = make_external_state_locked().valid;
        snapshot.satellites = s_runtime.data.satellites;
        snapshot.sats_in_view = s_runtime.status.sats_in_view;
        snapshot.sats_in_use = s_runtime.status.sats_in_use;
        snapshot.last_rx_age_ms = s_runtime.last_rx_ms ? (now_ms() - s_runtime.last_rx_ms) : 0xFFFFFFFFUL;
    }
    else if (env_state_configured())
    {
        const auto state = make_env_state();
        const auto status = make_env_status();
        snapshot.ready = true;
        snapshot.has_fix = state.valid;
        snapshot.satellites = state.satellites;
        snapshot.sats_in_view = status.sats_in_view;
        snapshot.sats_in_use = status.sats_in_use;
        snapshot.last_rx_age_ms = 0;
    }
    else if (!source_requested && demo_defaults_enabled())
    {
        const auto state = make_default_state();
        const auto status = make_default_status();
        snapshot.ready = true;
        snapshot.has_fix = state.valid;
        snapshot.satellites = state.satellites;
        snapshot.sats_in_view = status.sats_in_view;
        snapshot.sats_in_use = status.sats_in_use;
        snapshot.last_rx_age_ms = 0;
    }
    else
    {
        snapshot.ready = false;
    }

    if (!snapshot.ready)
    {
        snapshot.code = !source_requested && !env_state_configured() && !demo_defaults_enabled() && !auto_serial_probe_enabled()
                            ? ::gps::GpsDiagnosticCode::NotConfigured
                            : ::gps::GpsDiagnosticCode::TransportNotReady;
    }
    else if (snapshot.last_rx_age_ms != 0xFFFFFFFFUL && snapshot.last_rx_age_ms > kExternalSourceStaleMs)
    {
        snapshot.code = ::gps::GpsDiagnosticCode::TrafficStalled;
    }
    else if (!snapshot.has_fix)
    {
        snapshot.code = ::gps::GpsDiagnosticCode::NoFix;
    }
    else
    {
        snapshot.code = ::gps::GpsDiagnosticCode::OK;
    }
    return snapshot;
}

uint32_t last_motion_ms()
{
    const auto imu = ::platform::linux_runtime::linux_imu_snapshot();
    if (!runtime_active())
    {
        return imu.ready ? imu.last_motion_ms : 0;
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    if (!external_source_active_locked() && s_runtime.last_motion_ms == 0)
    {
        s_runtime.last_motion_ms = now_ms();
    }
    const auto gps_motion = s_runtime.last_motion_ms;
    if (!imu.ready || !imu.last_motion_ms) return gps_motion;
    if (!gps_motion) return imu.last_motion_ms;
    const auto now = now_ms();
    return uint32_t(now - imu.last_motion_ms) < uint32_t(now - gps_motion) ? imu.last_motion_ms : gps_motion;
}

void tick_service()
{
    const uint32_t now = now_ms();
    std::lock_guard<std::mutex> lock(s_mutex);
    update_recent_traffic_window_locked(now);

    if (!runtime_active())
    {
        reset_source_locked();
        if (s_runtime.power) s_runtime.power->request(false);
        s_power_ready = false;
        return;
    }

    if (s_runtime.last_service_poll_ms != 0 &&
        (now - s_runtime.last_service_poll_ms) < kServicePollIntervalMs)
    {
        return;
    }
    s_runtime.last_service_poll_ms = now;

    poll_external_source_locked();
    log_missing_source_once_locked();
}

bool supports_receiver_baud_setting()
{
    return true;
}

bool supports_receiver_init_policy_settings()
{
    return false;
}

bool supports_gnss_runtime_settings()
{
    return false;
}

bool supports_collection_interval_setting()
{
    return false;
}

bool supports_external_nmea_output_setting()
{
    return false;
}

bool supports_altitude_reference_setting()
{
    return false;
}

bool supports_coordinate_format_setting()
{
    return false;
}

bool is_enabled()
{
    return !s_suspended && s_enabled && env_flag_or_default(kGpsEnabledEnv, true);
}

bool is_powered()
{
    if (s_suspended || !s_enabled || !env_flag_or_default(kGpsPoweredEnv, true)) return false;
    return ::platform::linux_runtime::gnss_source_options().power.line < 0 || s_power_ready.load();
}

void set_enabled(bool enabled)
{
    s_enabled = enabled;
    if (!enabled)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        reset_source_locked();
        if (s_runtime.power) s_runtime.power->request(false);
        s_power_ready = false;
    }
}

void set_collection_interval(uint32_t interval_ms)
{
    s_collection_interval_ms = interval_ms;
}

void set_power_strategy(uint8_t strategy)
{
    s_power_strategy = strategy;
}

void set_gnss_config(uint8_t mode, uint8_t sat_mask)
{
    s_gnss_mode = mode;
    s_gnss_sat_mask = sat_mask;
}

void set_external_nmea_config(uint8_t output_hz, uint8_t sentence_mask)
{
    s_external_nmea_output_hz = output_hz;
    s_external_nmea_sentence_mask = sentence_mask;
}

void set_receiver_init_config(const GpsReceiverInitConfig& config)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    const bool baud_changed = s_receiver_init_config.baud != config.baud;
    s_receiver_init_config = config;
    if (baud_changed)
    {
        append_gps_system_log_locked(
            "GPS receiver config updated",
            "Receiver serial configuration changed; reopening source on next service tick",
            {
                {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                 .label = "baud",
                 .text = std::to_string(config.baud)},
                {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                 .label = "probe_ms",
                 .text = std::to_string(config.probe_ms)},
                {.kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
                 .label = "profile",
                 .text = std::to_string(config.profile)},
            });
        reset_source_locked();
    }
}

void set_fallback_mode(FallbackMode mode)
{
    s_fallback_mode = mode;
}

void set_motion_idle_timeout(uint32_t timeout_ms)
{
    s_motion_idle_timeout_ms = timeout_ms;
}

void set_motion_sensor_id(uint8_t sensor_id)
{
    s_motion_sensor_id = sensor_id;
}

void acquire_power_lease(const char* reason)
{
    (void)reason;
}

void release_power_lease(const char* reason)
{
    (void)reason;
}

void suspend_runtime()
{
    s_suspended = true;
    std::lock_guard<std::mutex> lock(s_mutex);
    reset_source_locked();
    if (s_runtime.power) s_runtime.power->request(false);
    s_power_ready = false;
}

void resume_runtime()
{
    s_suspended = false;
    std::lock_guard<std::mutex> lock(s_mutex);
    s_runtime.last_motion_ms = now_ms();
}

double calculate_map_resolution(int zoom, double lat)
{
    double clamped_lat = lat;
    if (clamped_lat > kMaxMercatorLat)
    {
        clamped_lat = kMaxMercatorLat;
    }
    else if (clamped_lat < -kMaxMercatorLat)
    {
        clamped_lat = -kMaxMercatorLat;
    }

    const double zoom_scale = std::pow(2.0, static_cast<double>(zoom));
    const double lat_rad = clamped_lat * kPi / 180.0;
    return (kEquatorResolution / zoom_scale) * std::cos(lat_rad);
}

} // namespace platform::ui::gps
