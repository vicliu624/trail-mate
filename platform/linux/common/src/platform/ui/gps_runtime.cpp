#include "platform/ui/gps_runtime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

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
constexpr const char* kGpsBaudEnv = "TRAIL_MATE_GPS_BAUD";
constexpr const char* kGpsNmeaFileEnv = "TRAIL_MATE_GPS_NMEA_FILE";

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
    std::size_t file_offset = 0;
    std::array<char, kLineBufferSize> line_buffer{};
    std::size_t line_length = 0;

#if defined(__linux__)
    int serial_fd = -1;
#endif
};

bool s_suspended = false;
uint32_t s_collection_interval_ms = 1000;
uint8_t s_power_strategy = 0;
uint8_t s_gnss_mode = 0;
uint8_t s_gnss_sat_mask = 0xFF;
uint8_t s_nmea_output_hz = 0;
uint8_t s_nmea_sentence_mask = 0;
uint32_t s_motion_idle_timeout_ms = 30000;
uint8_t s_motion_sensor_id = 0;
RuntimeState s_runtime{};
std::mutex s_mutex;

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
    return !s_suspended && env_flag_or_default(kGpsEnabledEnv, true) &&
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
    if (!sentence || sentence[0] != '$' || !verify_checksum(sentence))
    {
        return;
    }

    char working[kLineBufferSize] = {};
    std::strncpy(working, sentence + 1, sizeof(working) - 1);
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

    for (std::size_t i = 0; i < length; ++i)
    {
        const char ch = buffer[i];
        if (ch == '\r' || ch == '\n')
        {
            if (s_runtime.line_length > 0)
            {
                s_runtime.line_buffer[s_runtime.line_length] = '\0';
                parse_sentence_locked(s_runtime.line_buffer.data(), now_ms());
                s_runtime.line_length = 0;
            }
            continue;
        }

        if (s_runtime.line_length == 0 && ch != '$')
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
        }
    }
}

std::string requested_source_path(bool* out_is_serial)
{
    if (out_is_serial)
    {
        *out_is_serial = false;
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

    return {};
}

#if defined(__linux__)
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
        close(s_runtime.serial_fd);
        s_runtime.serial_fd = -1;
    }
}

bool open_serial_locked(const std::string& path)
{
    close_serial_locked();

    const int fd = open(path.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        return false;
    }

    termios tio{};
    if (tcgetattr(fd, &tio) != 0)
    {
        close(fd);
        return false;
    }

    cfmakeraw(&tio);
    const speed_t baud = baud_to_termios(env_int_or_default(kGpsBaudEnv, 38400));
    cfsetispeed(&tio, baud);
    cfsetospeed(&tio, baud);
    tio.c_cflag |= (CLOCAL | CREAD);
    if (tcsetattr(fd, TCSANOW, &tio) != 0)
    {
        close(fd);
        return false;
    }

    s_runtime.serial_fd = fd;
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
    s_runtime.source_path.clear();
    s_runtime.source_is_serial = false;
    clear_payload_locked();
}

void ensure_source_open_locked()
{
    bool requested_serial = false;
    const std::string requested_path = requested_source_path(&requested_serial);
    if (requested_path.empty())
    {
        if (!s_runtime.source_path.empty())
        {
            reset_source_locked();
        }
        return;
    }

    const bool source_changed =
        requested_path != s_runtime.source_path || requested_serial != s_runtime.source_is_serial;
    if (source_changed)
    {
        reset_source_locked();
        s_runtime.source_path = requested_path;
        s_runtime.source_is_serial = requested_serial;
#if defined(__linux__)
        if (requested_serial)
        {
            if (!open_serial_locked(requested_path))
            {
                s_runtime.source_path.clear();
                s_runtime.source_is_serial = false;
            }
        }
#else
        if (requested_serial)
        {
            s_runtime.source_path.clear();
            s_runtime.source_is_serial = false;
        }
#endif
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
    for (;;)
    {
        const ssize_t got = read(s_runtime.serial_fd, buffer, sizeof(buffer));
        if (got > 0)
        {
            append_bytes_and_process_locked(buffer, static_cast<std::size_t>(got));
            continue;
        }
        if (got == 0)
        {
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            break;
        }
        close_serial_locked();
        break;
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
#endif
        return;
    }

    poll_file_source_locked();
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
    poll_external_source_locked();

    if (external_source_active_locked())
    {
        return make_external_state_locked();
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
    poll_external_source_locked();

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

uint32_t last_motion_ms()
{
    if (!runtime_active())
    {
        return 0;
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    if (external_source_active_locked())
    {
        poll_external_source_locked();
        return s_runtime.last_motion_ms;
    }

    if (s_runtime.last_motion_ms == 0)
    {
        s_runtime.last_motion_ms = now_ms();
    }
    return s_runtime.last_motion_ms;
}

bool is_enabled()
{
    return !s_suspended && env_flag_or_default(kGpsEnabledEnv, true);
}

bool is_powered()
{
    return !s_suspended && env_flag_or_default(kGpsPoweredEnv, true);
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

void set_nmea_config(uint8_t output_hz, uint8_t sentence_mask)
{
    s_nmea_output_hz = output_hz;
    s_nmea_sentence_mask = sentence_mask;
}

void set_motion_idle_timeout(uint32_t timeout_ms)
{
    s_motion_idle_timeout_ms = timeout_ms;
}

void set_motion_sensor_id(uint8_t sensor_id)
{
    s_motion_sensor_id = sensor_id;
}

void suspend_runtime()
{
    s_suspended = true;
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
