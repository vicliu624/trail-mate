#include "platform/esp/idf_common/gps_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <mutex>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "platform/esp/boards/t_display_p4_board_profile.h"
#include "platform/esp/boards/tab5_board_profile.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/esp/idf_common/tab5_rtc_runtime.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
extern "C" void bsp_set_ext_5v_en(bool en);
#endif

namespace platform::esp::idf_common::gps_runtime
{
namespace
{
constexpr const char* kTag = "idf-gps-runtime";
constexpr size_t kMaxFields = 24;
constexpr size_t kLineBufferSize = 160;
constexpr size_t kRxBufferSize = 512;
constexpr uint32_t kProbeWarmupMs = 1500;
constexpr uint32_t kProbeListenMs = 3500;
constexpr uint32_t kNoDataWarnMs = 5000;

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
    std::array<gps::GnssSatInfo, gps::kMaxGnssSats> sats{};
    std::size_t count = 0;
};

struct RuntimeState
{
    gps::GpsState data{};
    std::array<gps::GnssSatInfo, gps::kMaxGnssSats> sats{};
    std::size_t sat_count = 0;
    gps::GnssStatus status{};
    uint32_t last_motion_ms = 0;
    bool enabled = false;
    bool powered = false;
    uint32_t collection_interval_ms = 1000;
    uint8_t power_strategy = 0;
    uint8_t gnss_mode = 0;
    uint8_t gnss_sat_mask = 0;
    uint8_t nmea_output_hz = 0;
    uint8_t nmea_sentence_mask = 0;
    uint32_t motion_idle_timeout_ms = 0;
    uint8_t motion_sensor_id = 0;
    TaskHandle_t worker_handle = nullptr;
    bool stop_requested = false;
    bool probe_requested = false;
    uint32_t probe_deadline_ms = 0;
    uint32_t last_rx_ms = 0;
    uint32_t last_fix_ms = 0;
    uint32_t last_no_data_log_ms = 0;
    uint32_t last_time_sync_attempt_ms = 0;
    bool first_sentence_logged = false;
    bool time_sync_committed = false;
    std::time_t last_time_sync_epoch = 0;
    std::array<uint16_t, 12> used_sat_ids{};
    std::size_t used_sat_count = 0;
    std::array<GsvCollector, static_cast<size_t>(CollectorSlot::COUNT)> gsv{};
};

RuntimeState s_runtime{};
std::mutex s_mutex;

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

bool gps_available()
{
    return platform::esp::idf_common::bsp_runtime::gps_capable();
}

struct GpsUartPins
{
    int port;
    int tx;
    int rx;
};

GpsUartPins gps_uart_pins()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return {platform::esp::boards::tab5::kBoardProfile.gps_uart.port,
            platform::esp::boards::tab5::kBoardProfile.gps_uart.tx,
            platform::esp::boards::tab5::kBoardProfile.gps_uart.rx};
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return {platform::esp::boards::t_display_p4::kBoardProfile.gps_uart.port,
            platform::esp::boards::t_display_p4::kBoardProfile.gps_uart.tx,
            platform::esp::boards::t_display_p4::kBoardProfile.gps_uart.rx};
#else
    return {-1, -1, -1};
#endif
}

CollectorSlot collector_slot_for_talker(const char* talker)
{
    if (!talker || talker[0] == '\0' || talker[1] == '\0') return CollectorSlot::UNKNOWN;
    if (talker[0] == 'G' && talker[1] == 'P') return CollectorSlot::GPS;
    if (talker[0] == 'G' && talker[1] == 'L') return CollectorSlot::GLN;
    if (talker[0] == 'G' && talker[1] == 'A') return CollectorSlot::GAL;
    if ((talker[0] == 'G' && talker[1] == 'B') || (talker[0] == 'B' && talker[1] == 'D')) return CollectorSlot::BD;
    return CollectorSlot::UNKNOWN;
}

gps::GnssSystem system_for_slot(CollectorSlot slot)
{
    switch (slot)
    {
    case CollectorSlot::GPS:
        return gps::GnssSystem::GPS;
    case CollectorSlot::GLN:
        return gps::GnssSystem::GLN;
    case CollectorSlot::GAL:
        return gps::GnssSystem::GAL;
    case CollectorSlot::BD:
        return gps::GnssSystem::BD;
    default:
        return gps::GnssSystem::UNKNOWN;
    }
}

bool parse_uint(const char* text, uint32_t* out)
{
    if (!text || !*text || !out) return false;
    char* end = nullptr;
    unsigned long value = std::strtoul(text, &end, 10);
    if (end == text) return false;
    *out = static_cast<uint32_t>(value);
    return true;
}

bool parse_int(const char* text, int* out)
{
    if (!text || !*text || !out) return false;
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (end == text) return false;
    *out = static_cast<int>(value);
    return true;
}

bool parse_double(const char* text, double* out)
{
    if (!text || !*text || !out) return false;
    char* end = nullptr;
    double value = std::strtod(text, &end);
    if (end == text) return false;
    *out = value;
    return true;
}

bool parse_latlon(const char* value_text, const char* hemi_text, bool latitude, double* out)
{
    if (!value_text || !*value_text || !hemi_text || !*hemi_text || !out) return false;
    double raw = 0.0;
    if (!parse_double(value_text, &raw)) return false;
    int degrees = static_cast<int>(raw / 100.0);
    double minutes = raw - (static_cast<double>(degrees) * 100.0);
    double decimal = static_cast<double>(degrees) + (minutes / 60.0);
    char hemi = hemi_text[0];
    if (hemi == 'S' || hemi == 'W') decimal = -decimal;
    else if ((latitude && hemi != 'N') || (!latitude && hemi != 'E')) return false;
    *out = decimal;
    return true;
}

bool verify_checksum(const char* line)
{
    if (!line || line[0] != '$') return false;
    const char* star = std::strchr(line, '*');
    if (!star || !star[1] || !star[2]) return true;
    uint8_t checksum = 0;
    for (const char* cursor = line + 1; cursor < star; ++cursor) checksum ^= static_cast<uint8_t>(*cursor);
    char text[3] = {star[1], star[2], '\0'};
    char* end = nullptr;
    long expected = std::strtol(text, &end, 16);
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
        if (!comma) break;
        *comma = '\0';
        cursor = comma + 1;
    }
    return count;
}

void clear_payload_locked()
{
    s_runtime.data = gps::GpsState{};
    s_runtime.status = gps::GnssStatus{};
    s_runtime.sat_count = 0;
    s_runtime.used_sat_count = 0;
    s_runtime.last_time_sync_attempt_ms = 0;
    s_runtime.last_time_sync_epoch = 0;
    s_runtime.time_sync_committed = platform::esp::idf_common::tab5_rtc_runtime::is_valid_epoch(std::time(nullptr));
    for (auto& collector : s_runtime.gsv) collector = GsvCollector{};
}

bool parse_two_digits(const char* text, uint8_t* out)
{
    if (text == nullptr || out == nullptr || text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9')
    {
        return false;
    }
    *out = static_cast<uint8_t>((text[0] - '0') * 10 + (text[1] - '0'));
    return true;
}

bool parse_hhmmss(const char* text, uint8_t* hour, uint8_t* minute, uint8_t* second)
{
    if (text == nullptr || hour == nullptr || minute == nullptr || second == nullptr)
    {
        return false;
    }
    return parse_two_digits(text, hour) &&
           parse_two_digits(text + 2, minute) &&
           parse_two_digits(text + 4, second);
}

bool parse_ddmmyy(const char* text, uint8_t* day, uint8_t* month, int* year)
{
    if (text == nullptr || day == nullptr || month == nullptr || year == nullptr)
    {
        return false;
    }

    uint8_t yy = 0;
    if (!parse_two_digits(text, day) || !parse_two_digits(text + 2, month) || !parse_two_digits(text + 4, &yy))
    {
        return false;
    }

    *year = 2000 + static_cast<int>(yy);
    return true;
}

void maybe_sync_time_from_rmc_locked(const std::array<char*, kMaxFields>& fields, std::size_t count, uint32_t ts)
{
    if (count <= 9)
    {
        return;
    }

    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t day = 0;
    uint8_t month = 0;
    int year = 0;
    if (!parse_hhmmss(fields[1], &hour, &minute, &second) || !parse_ddmmyy(fields[9], &day, &month, &year))
    {
        return;
    }

    if (!platform::esp::idf_common::tab5_rtc_runtime::validate_datetime_utc(year, month, day, hour, minute, second))
    {
        return;
    }

    const std::time_t gnss_epoch = platform::esp::idf_common::tab5_rtc_runtime::datetime_to_epoch_utc(
        year, month, day, hour, minute, second);
    if (gnss_epoch < 0)
    {
        return;
    }

    const std::time_t now_epoch = std::time(nullptr);
    const bool system_valid = platform::esp::idf_common::tab5_rtc_runtime::is_valid_epoch(now_epoch);
    const long long delta_seconds = system_valid
                                        ? std::llabs(static_cast<long long>(now_epoch) - static_cast<long long>(gnss_epoch))
                                        : std::numeric_limits<long long>::max();
    const bool needs_system_update = !system_valid || delta_seconds >= 2;
    const bool needs_commit = !s_runtime.time_sync_committed;
    if (!needs_system_update && !needs_commit)
    {
        return;
    }

    if (s_runtime.last_time_sync_attempt_ms != 0 && (ts - s_runtime.last_time_sync_attempt_ms) < 5000)
    {
        return;
    }

    s_runtime.last_time_sync_attempt_ms = ts;
    if (platform::esp::idf_common::tab5_rtc_runtime::apply_system_time_and_sync_rtc(gnss_epoch, "gnss_rmc"))
    {
        s_runtime.last_time_sync_epoch = gnss_epoch;
        s_runtime.time_sync_committed = true;
    }
}

bool used_sat_locked(uint16_t sat_id)
{
    for (std::size_t i = 0; i < s_runtime.used_sat_count; ++i)
    {
        if (s_runtime.used_sat_ids[i] == sat_id) return true;
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
    if (count < 9) return;
    bool active = fields[2] && fields[2][0] == 'A';
    s_runtime.data.valid = active;
    maybe_sync_time_from_rmc_locked(fields, count, ts);
    if (!active) return;
    double lat = 0.0, lng = 0.0, speed_knots = 0.0, course = 0.0;
    if (parse_latlon(fields[3], fields[4], true, &lat)) s_runtime.data.lat = lat;
    if (parse_latlon(fields[5], fields[6], false, &lng)) s_runtime.data.lng = lng;
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
}

void parse_gga_locked(const std::array<char*, kMaxFields>& fields, std::size_t count, uint32_t ts)
{
    if (count < 10) return;
    int quality = 0;
    if (parse_int(fields[6], &quality)) s_runtime.data.valid = quality > 0;
    uint32_t satellites = 0;
    if (parse_uint(fields[7], &satellites))
    {
        s_runtime.data.satellites = static_cast<uint8_t>(std::min<uint32_t>(satellites, 255));
        s_runtime.status.sats_in_use = s_runtime.data.satellites;
    }
    double hdop = 0.0, altitude = 0.0;
    if (parse_double(fields[8], &hdop)) s_runtime.status.hdop = static_cast<float>(hdop);
    if (parse_double(fields[9], &altitude))
    {
        s_runtime.data.alt_m = altitude;
        s_runtime.data.has_alt = true;
    }
    else
    {
        s_runtime.data.has_alt = false;
    }
    if (s_runtime.data.valid) s_runtime.last_fix_ms = ts;
}

void parse_gsa_locked(const std::array<char*, kMaxFields>& fields, std::size_t count)
{
    if (count < 4) return;
    s_runtime.used_sat_count = 0;
    for (std::size_t i = 3; i <= 14 && i < count; ++i)
    {
        uint32_t sat_id = 0;
        if (parse_uint(fields[i], &sat_id) && sat_id > 0 && s_runtime.used_sat_count < s_runtime.used_sat_ids.size())
            s_runtime.used_sat_ids[s_runtime.used_sat_count++] = static_cast<uint16_t>(sat_id);
    }
    int fix_type = 0;
    if (parse_int(fields[2], &fix_type))
    {
        s_runtime.status.fix = fix_type >= 3 ? gps::GnssFix::FIX3D : (fix_type == 2 ? gps::GnssFix::FIX2D : gps::GnssFix::NOFIX);
    }
    if (count > 16)
    {
        double hdop = 0.0;
        if (parse_double(fields[16], &hdop)) s_runtime.status.hdop = static_cast<float>(hdop);
    }
    s_runtime.status.sats_in_use = static_cast<uint8_t>(std::min<std::size_t>(s_runtime.used_sat_count, 255));
    merge_sats_locked();
}

void parse_gsv_locked(const char* talker, const std::array<char*, kMaxFields>& fields, std::size_t count)
{
    if (count < 4) return;
    auto& collector = s_runtime.gsv[static_cast<std::size_t>(collector_slot_for_talker(talker))];
    uint32_t msg_num = 0;
    if (!parse_uint(fields[2], &msg_num)) return;
    if (msg_num == 1) collector.count = 0;
    for (std::size_t base = 4; base + 3 < count && collector.count < collector.sats.size(); base += 4)
    {
        uint32_t sat_id = 0;
        if (!parse_uint(fields[base], &sat_id) || sat_id == 0) continue;
        gps::GnssSatInfo sat{};
        sat.id = static_cast<uint16_t>(sat_id);
        sat.sys = system_for_slot(collector_slot_for_talker(talker));
        uint32_t elev = 0, az = 0;
        int snr = -1;
        if (parse_uint(fields[base + 1], &elev)) sat.elevation = static_cast<uint8_t>(std::min<uint32_t>(elev, 90));
        if (parse_uint(fields[base + 2], &az)) sat.azimuth = static_cast<uint16_t>(std::min<uint32_t>(az, 359));
        if (parse_int(fields[base + 3], &snr)) sat.snr = static_cast<int8_t>(std::clamp(snr, -1, 99));
        collector.sats[collector.count++] = sat;
    }
    merge_sats_locked();
}

void parse_sentence_locked(const char* sentence, uint32_t ts)
{
    if (!sentence || sentence[0] != '$' || !verify_checksum(sentence)) return;
    char working[kLineBufferSize] = {};
    std::strncpy(working, sentence + 1, sizeof(working) - 1);
    char* star = std::strchr(working, '*');
    if (star) *star = '\0';
    std::array<char*, kMaxFields> fields{};
    std::size_t count = split_fields(working, fields);
    if (count == 0 || !fields[0] || std::strlen(fields[0]) < 5) return;
    char talker[3] = {fields[0][0], fields[0][1], '\0'};
    const char* type = fields[0] + std::strlen(fields[0]) - 3;
    s_runtime.last_rx_ms = ts;
    if (!s_runtime.first_sentence_logged)
    {
        ESP_LOGI(kTag, "GNSS first NMEA sentence: %s", sentence);
        s_runtime.first_sentence_logged = true;
    }
    if (std::strcmp(type, "RMC") == 0) parse_rmc_locked(fields, count, ts);
    else if (std::strcmp(type, "GGA") == 0) parse_gga_locked(fields, count, ts);
    else if (std::strcmp(type, "GSA") == 0) parse_gsa_locked(fields, count);
    else if (std::strcmp(type, "GSV") == 0) parse_gsv_locked(talker, fields, count);
}

void append_bytes_and_process(const uint8_t* buffer, int length, char* line_buffer, std::size_t* line_length)
{
    if (!buffer || length <= 0 || !line_buffer || !line_length) return;
    for (int i = 0; i < length; ++i)
    {
        char ch = static_cast<char>(buffer[i]);
        if (ch == '\r' || ch == '\n')
        {
            if (*line_length > 0)
            {
                line_buffer[*line_length] = '\0';
                std::lock_guard<std::mutex> lock(s_mutex);
                parse_sentence_locked(line_buffer, now_ms());
                *line_length = 0;
            }
            continue;
        }
        if (*line_length == 0 && ch != '$') continue;
        if (*line_length + 1 < kLineBufferSize) line_buffer[(*line_length)++] = ch;
        else *line_length = 0;
    }
}

void ensure_ext5v_enabled()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    bsp_set_ext_5v_en(true);
    ESP_LOGI(kTag, "Tab5 M5-Bus Ext5V enabled for GNSS runtime");
#endif
}

void configure_uart_hardware()
{
    const auto gps_uart = gps_uart_pins();
    uart_config_t config{};
    config.baud_rate = 38400;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    uart_driver_delete(static_cast<uart_port_t>(gps_uart.port));
    ESP_ERROR_CHECK(uart_param_config(static_cast<uart_port_t>(gps_uart.port), &config));
    ESP_ERROR_CHECK(uart_set_pin(static_cast<uart_port_t>(gps_uart.port), gps_uart.tx, gps_uart.rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(static_cast<uart_port_t>(gps_uart.port), 4096, 0, 0, nullptr, 0));
    ESP_LOGI(kTag, "GNSS UART configured: port=%d tx=%d rx=%d baud=%d", gps_uart.port, gps_uart.tx, gps_uart.rx, config.baud_rate);
}

void teardown_uart_hardware()
{
    const auto gps_uart = gps_uart_pins();
    if (gps_uart.port >= 0)
    {
        uart_driver_delete(static_cast<uart_port_t>(gps_uart.port));
    }
}

void worker_task(void*)
{
    ensure_ext5v_enabled();
    vTaskDelay(pdMS_TO_TICKS(kProbeWarmupMs));
    configure_uart_hardware();
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_runtime.powered = true;
        s_runtime.last_rx_ms = 0;
        s_runtime.last_fix_ms = 0;
        s_runtime.first_sentence_logged = false;
        s_runtime.last_no_data_log_ms = 0;
    }

    char line_buffer[kLineBufferSize] = {};
    std::size_t line_length = 0;
    uint8_t rx_buffer[kRxBufferSize] = {};

    while (true)
    {
        const auto gps_uart = gps_uart_pins();
        int read_len = uart_read_bytes(static_cast<uart_port_t>(gps_uart.port), rx_buffer, sizeof(rx_buffer), pdMS_TO_TICKS(200));
        if (read_len > 0) append_bytes_and_process(rx_buffer, read_len, line_buffer, &line_length);

        bool should_exit = false;
        bool enabled = false;
        bool probe_requested = false;
        uint32_t probe_deadline_ms = 0;
        uint32_t last_rx_ms = 0;
        uint32_t last_fix_ms = 0;
        uint32_t now = now_ms();
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            enabled = s_runtime.enabled;
            probe_requested = s_runtime.probe_requested;
            probe_deadline_ms = s_runtime.probe_deadline_ms;
            last_rx_ms = s_runtime.last_rx_ms;
            last_fix_ms = s_runtime.last_fix_ms;
            s_runtime.data.age = last_fix_ms ? (now - last_fix_ms) : 0;
            if (s_runtime.stop_requested && !enabled) should_exit = true;
            else if (!enabled && probe_requested && probe_deadline_ms != 0 && now >= probe_deadline_ms)
            {
                ESP_LOGI(kTag, "GNSS startup probe finished: nmea_seen=%d last_fix_age_ms=%lu", last_rx_ms != 0 ? 1 : 0, last_fix_ms != 0 ? static_cast<unsigned long>(now - last_fix_ms) : 0UL);
                s_runtime.probe_requested = false;
                should_exit = true;
            }
            else if ((now - last_rx_ms) >= kNoDataWarnMs && s_runtime.last_no_data_log_ms + kNoDataWarnMs <= now)
            {
                const auto gps_uart_log = gps_uart_pins();
                ESP_LOGW(kTag, "GNSS no NMEA data yet: port=%d tx=%d rx=%d mode=%u", gps_uart_log.port, gps_uart_log.tx, gps_uart_log.rx, s_runtime.gnss_mode);
                s_runtime.last_no_data_log_ms = now;
            }
        }
        if (should_exit) break;
    }

    teardown_uart_hardware();
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_runtime.enabled) clear_payload_locked();
        s_runtime.powered = false;
        s_runtime.stop_requested = false;
        s_runtime.worker_handle = nullptr;
        s_runtime.last_rx_ms = 0;
        s_runtime.last_fix_ms = 0;
        s_runtime.last_time_sync_attempt_ms = 0;
        s_runtime.last_time_sync_epoch = 0;
        s_runtime.time_sync_committed = platform::esp::idf_common::tab5_rtc_runtime::is_valid_epoch(std::time(nullptr));
        s_runtime.first_sentence_logged = false;
        s_runtime.last_no_data_log_ms = 0;
    }
    vTaskDelete(nullptr);
}

void ensure_worker_locked()
{
    if (s_runtime.worker_handle != nullptr) return;
    s_runtime.stop_requested = false;
    if (xTaskCreate(worker_task, "idf_gps", 6144, nullptr, 5, &s_runtime.worker_handle) != pdPASS)
    {
        ESP_LOGE(kTag, "Failed to create GNSS runtime task");
        s_runtime.worker_handle = nullptr;
    }
}

} // namespace

gps::GpsState get_data()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    gps::GpsState data = s_runtime.data;
    data.age = s_runtime.last_fix_ms ? (now_ms() - s_runtime.last_fix_ms) : 0;
    return data;
}

bool get_gnss_snapshot(gps::GnssSatInfo* out, std::size_t max, std::size_t* out_count, gps::GnssStatus* status)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (out_count) *out_count = s_runtime.sat_count;
    if (status) *status = s_runtime.status;
    if (!out || max == 0 || s_runtime.sat_count == 0) return false;
    std::size_t count = std::min(max, s_runtime.sat_count);
    for (std::size_t i = 0; i < count; ++i) out[i] = s_runtime.sats[i];
    if (out_count) *out_count = count;
    return true;
}

uint32_t last_motion_ms()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_runtime.last_motion_ms;
}

bool is_enabled()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return gps_available() && s_runtime.enabled;
}

bool is_powered()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return gps_available() && s_runtime.powered;
}

void set_collection_interval(uint32_t interval_ms)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_runtime.collection_interval_ms = interval_ms;
}

void set_power_strategy(uint8_t strategy)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_runtime.power_strategy = strategy;
}

void set_gnss_config(uint8_t mode, uint8_t sat_mask)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_runtime.gnss_mode = mode;
    s_runtime.gnss_sat_mask = sat_mask;
    s_runtime.enabled = gps_available() && mode != 0;
    if (!s_runtime.enabled)
    {
        s_runtime.stop_requested = true;
        clear_payload_locked();
        ESP_LOGI(kTag, "GNSS runtime disabled: mode=%u sat_mask=0x%02X", mode, sat_mask);
        return;
    }
    s_runtime.probe_requested = false;
    s_runtime.probe_deadline_ms = 0;
    s_runtime.time_sync_committed = platform::esp::idf_common::tab5_rtc_runtime::is_valid_epoch(std::time(nullptr));
    ensure_worker_locked();
    ESP_LOGI(kTag, "GNSS runtime enabled: interval_ms=%lu mode=%u sat_mask=0x%02X strategy=%u", static_cast<unsigned long>(s_runtime.collection_interval_ms), s_runtime.gnss_mode, s_runtime.gnss_sat_mask, s_runtime.power_strategy);
}

void set_nmea_config(uint8_t output_hz, uint8_t sentence_mask)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_runtime.nmea_output_hz = output_hz;
    s_runtime.nmea_sentence_mask = sentence_mask;
}

void set_motion_idle_timeout(uint32_t timeout_ms)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_runtime.motion_idle_timeout_ms = timeout_ms;
}

void set_motion_sensor_id(uint8_t sensor_id)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_runtime.motion_sensor_id = sensor_id;
}

TaskHandle_t get_task_handle()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_runtime.worker_handle;
}

void request_startup_probe()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!gps_available() || s_runtime.enabled || s_runtime.worker_handle != nullptr) return;
    s_runtime.probe_requested = true;
    s_runtime.probe_deadline_ms = now_ms() + kProbeWarmupMs + kProbeListenMs;
    ensure_worker_locked();
    const auto gps_uart = gps_uart_pins();
    ESP_LOGI(kTag, "GNSS startup probe scheduled: port=%d tx=%d rx=%d mode=%u sat_mask=0x%02X", gps_uart.port, gps_uart.tx, gps_uart.rx, s_runtime.gnss_mode, s_runtime.gnss_sat_mask);
}

} // namespace platform::esp::idf_common::gps_runtime
