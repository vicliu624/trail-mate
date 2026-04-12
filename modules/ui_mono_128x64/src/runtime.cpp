#include "ui/mono_128x64/runtime.h"

#include "app/app_config.h"
#include "ble/ble_manager.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "chat/runtime/self_identity_policy.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "generated/meshtastic/mesh.pb.h"
#include "generated/meshtastic/portnums.pb.h"
#include "platform/ui/screen_runtime.h"
#include "pb_encode.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ui::mono_128x64
{
namespace
{
const char* inputActionName(InputAction action)
{
    switch (action)
    {
    case InputAction::None:
        return "None";
    case InputAction::Up:
        return "Up";
    case InputAction::Down:
        return "Down";
    case InputAction::Left:
        return "Left";
    case InputAction::Right:
        return "Right";
    case InputAction::Select:
        return "Select";
    case InputAction::Back:
        return "Back";
    case InputAction::Primary:
        return "Primary";
    case InputAction::Secondary:
        return "Secondary";
    default:
        return "?";
    }
}

constexpr const char* kMainMenuItems[] = {
    "CHATS",
    "NODES",
    "GPS",
    "SETTINGS",
    "INFO",
};

constexpr const char* kSettingsMenuItems[] = {
    "LORA",
    "DEVICE",
    "ACTIONS",
};

constexpr const char* kMeshtasticRadioItems[] = {
    "PROTOCOL",
    "REGION",
    "TX POWER",
    "MODEM",
    "CH SLOT",
    "ENCRYPT",
};

constexpr const char* kMeshCoreRadioItems[] = {
    "PROTOCOL",
    "REGION",
    "TX POWER",
    "PRESET",
    "SLOT",
    "ENCRYPT",
    "NAME",
};

constexpr uint16_t kMeshtasticChannelNumMax = 16;
constexpr uint32_t kScreenTimeoutAlwaysMs = 300000UL;
constexpr uint32_t kScreenTimeoutOptionsMs[] = {15000UL, 30000UL, 60000UL, kScreenTimeoutAlwaysMs};

uint16_t normalizedMeshtasticChannelNum(uint16_t channel_num)
{
    return std::min<uint16_t>(channel_num, kMeshtasticChannelNumMax);
}

void sanitizeMeshtasticChannelNum(app::AppConfig& cfg)
{
    cfg.meshtastic_config.channel_num = normalizedMeshtasticChannelNum(cfg.meshtastic_config.channel_num);
}

void formatMeshtasticChannelSlot(char* out, size_t out_len, uint16_t channel_num)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const unsigned slot = static_cast<unsigned>(normalizedMeshtasticChannelNum(channel_num));
    if (slot == 0U)
    {
        std::snprintf(out, out_len, "Auto");
        return;
    }

    std::snprintf(out, out_len, "%u", slot);
}

constexpr const char* kDeviceItems[] = {
    "BLE",
    "GPS",
    "SATS",
    "GPS INT",
    "SCREEN OFF",
    "TIME ZONE",
};

constexpr const char* kActionItems[] = {
    "BROADCAST ID",
    "CLEAR NODES",
    "CLEAR MSGS",
    "RESET RADIO",
};

constexpr const char* kMessageMenuItems[] = {
    "INFO",
    "REPLY",
};

constexpr size_t kNodeActionItemCount = 7;

constexpr const char* kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char* kComposeCharset =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,!?-_/:#@+*=()[]";
constexpr const char* kHexCharset = "0123456789ABCDEF";
constexpr const char* kComposeAbcKeys = " ETAOINSHRDLUCMFWYPVBGKQJXZ";
constexpr const char* kComposeNumKeys = "0123456789";
constexpr const char* kComposeSymKeys = " .,!?/-:@#()[]+=";
constexpr const char* kComposeActionLabels[] = {"ABC", "SP", "BACK", "DEL", "SEND"};
constexpr uint32_t kConversationScrollStartPauseMs = 500;
constexpr uint32_t kConversationScrollStepMs = 250;
constexpr uint32_t kConversationScrollEndPauseMs = 800;
struct ComposeGroupDef
{
    const char* input;
    const char* label;
};

constexpr ComposeGroupDef kComposeAbcGroups[] = {
    {"ABC", "ABC"},
    {"DEF", "DEF"},
    {"GHI", "GHI"},
    {"JKL", "JKL"},
    {"!/-", "!/-"},
    {"MNO", "MNO"},
    {"PQRS", "PQRS"},
    {"TUV", "TUV"},
    {"WXYZ", "WXYZ"},
    {".,?", ".,?"},
};
constexpr uint32_t kBootMinMs = 1800;
constexpr uint32_t kComposeMultiTapWindowMs = 700;
constexpr size_t kChatListPageSize = 6;
constexpr size_t kNodeListPageSize = 6;
constexpr size_t kMessageInfoPageSize = 6;
constexpr size_t kNodeInfoPageSize = 6;
constexpr size_t kInfoPageSize = 6;
constexpr size_t kGnssSummaryPageSize = 6;
constexpr size_t kGnssSatPageSize = 5;
constexpr uint32_t kGnssSnapshotRefreshMs = 5000;
constexpr int kTimezoneMin = -12 * 60;
constexpr int kTimezoneMax = 14 * 60;
constexpr int kTimezoneStep = 60;
constexpr const char* kHamTerms[] = {
    "73",
    "88",
    "AGN",
    "ALFA",
    "ATT",
    "BREAK",
    "BRAVO",
    "CALL",
    "CHARLIE",
    "COPY",
    "CQ",
    "CTCSS",
    "DE",
    "DELTA",
    "DIRECT",
    "DUPLEX",
    "DX",
    "ECHO",
    "ES",
    "FB",
    "FM",
    "FREQ",
    "FT8",
    "FT4",
    "HOTEL",
    "ID",
    "INDIA",
    "INFO",
    "JA",
    "JULIETT",
    "KILO",
    "LIMA",
    "LOG",
    "LSB",
    "MIKE",
    "NIL",
    "NOVEMBER",
    "OM",
    "OSCAR",
    "OUT",
    "OVER",
    "PAPA",
    "PSE",
    "QRL",
    "QRM",
    "QRN",
    "QRP",
    "QRQ",
    "QRS",
    "QRT",
    "QRV",
    "QRZ",
    "QSB",
    "QSL",
    "QSO",
    "QSP",
    "QSX",
    "QSY",
    "QTC",
    "QTH",
    "QTR",
    "QUEBEC",
    "RELAY",
    "REPEATER",
    "RIG",
    "ROGER",
    "ROMEO",
    "RST",
    "RX",
    "SEND",
    "SIERRA",
    "SIMPLEX",
    "SOLID",
    "SPLIT",
    "STANDBY",
    "SYM",
    "TANGO",
    "TKS",
    "TNX",
    "TU",
    "TX",
    "UNIFORM",
    "USB",
    "VICTOR",
    "WAIT",
    "WHISKEY",
    "X-RAY",
    "XYL",
    "YANKEE",
    "YL",
    "ZULU",
};

template <typename T, size_t N>
constexpr size_t arrayCount(const T (&)[N])
{
    return N;
}

template <typename T>
constexpr T clampValue(T value, T low, T high)
{
    return value < low ? low : (value > high ? high : value);
}

uint32_t normalizedScreenTimeoutMs(uint32_t timeout_ms)
{
    return platform::ui::screen::clamp_timeout_ms(timeout_ms);
}

size_t screenTimeoutOptionIndex(uint32_t timeout_ms)
{
    timeout_ms = normalizedScreenTimeoutMs(timeout_ms);
    size_t index = 0;
    while (index + 1 < arrayCount(kScreenTimeoutOptionsMs) &&
           kScreenTimeoutOptionsMs[index] < timeout_ms)
    {
        ++index;
    }
    return index;
}

uint32_t stepScreenTimeoutMs(uint32_t current_timeout_ms, int delta)
{
    const int current = static_cast<int>(screenTimeoutOptionIndex(current_timeout_ms));
    const int next = clampValue(current + delta, 0, static_cast<int>(arrayCount(kScreenTimeoutOptionsMs)) - 1);
    return kScreenTimeoutOptionsMs[next];
}

void formatScreenTimeoutLabel(char* out, size_t out_len, uint32_t timeout_ms)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const uint32_t normalized = normalizedScreenTimeoutMs(timeout_ms);
    if (normalized >= kScreenTimeoutAlwaysMs)
    {
        std::snprintf(out, out_len, "Always");
        return;
    }
    if (normalized % 60000UL == 0)
    {
        std::snprintf(out, out_len, "%lumin", static_cast<unsigned long>(normalized / 60000UL));
        return;
    }

    std::snprintf(out, out_len, "%lus", static_cast<unsigned long>(normalized / 1000UL));
}

size_t utf8CharLength(unsigned char lead)
{
    if ((lead & 0x80U) == 0)
    {
        return 1;
    }
    if ((lead & 0xE0U) == 0xC0U)
    {
        return 2;
    }
    if ((lead & 0xF0U) == 0xE0U)
    {
        return 3;
    }
    if ((lead & 0xF8U) == 0xF0U)
    {
        return 4;
    }
    return 1;
}

template <size_t N>
void copyText(char (&dst)[N], const char* src)
{
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, N - 1);
    dst[N - 1] = '\0';
}

void appendChar(char* buffer, size_t capacity, size_t& len, char ch)
{
    if (!buffer || capacity == 0 || len + 1 >= capacity)
    {
        return;
    }
    buffer[len++] = ch;
    buffer[len] = '\0';
}

void popChar(char* buffer, size_t& len)
{
    if (!buffer || len == 0)
    {
        return;
    }
    --len;
    buffer[len] = '\0';
}

const char* composeKeysetForMode(ui::mono_128x64::Runtime::ComposeMode mode)
{
    switch (mode)
    {
    case ui::mono_128x64::Runtime::ComposeMode::Num:
        return kComposeNumKeys;
    case ui::mono_128x64::Runtime::ComposeMode::Sym:
        return kComposeSymKeys;
    case ui::mono_128x64::Runtime::ComposeMode::AbcUpper:
    case ui::mono_128x64::Runtime::ComposeMode::AbcLower:
    default:
        return kComposeAbcKeys;
    }
}

const char* composeModeLabel(ui::mono_128x64::Runtime::ComposeMode mode)
{
    switch (mode)
    {
    case ui::mono_128x64::Runtime::ComposeMode::AbcLower:
        return "abc";
    case ui::mono_128x64::Runtime::ComposeMode::AbcUpper:
        return "ABC";
    case ui::mono_128x64::Runtime::ComposeMode::Num:
        return "123";
    case ui::mono_128x64::Runtime::ComposeMode::Sym:
        return "SYM";
    default:
        return "ABC";
    }
}

bool isAlphaComposeMode(ui::mono_128x64::Runtime::ComposeMode mode)
{
    return mode == ui::mono_128x64::Runtime::ComposeMode::AbcLower ||
           mode == ui::mono_128x64::Runtime::ComposeMode::AbcUpper;
}

char applyComposeAlphaCase(ui::mono_128x64::Runtime::ComposeMode mode, char ch)
{
    if (std::isalpha(static_cast<unsigned char>(ch)) == 0)
    {
        return ch;
    }

    if (mode == ui::mono_128x64::Runtime::ComposeMode::AbcLower)
    {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
}

char upperAscii(char ch)
{
    return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
}

bool isAlphaAscii(char ch)
{
    return std::isalpha(static_cast<unsigned char>(ch)) != 0;
}

bool hasPrefixIgnoreCase(const char* text, const char* prefix)
{
    if (!text || !prefix)
    {
        return false;
    }
    while (*prefix)
    {
        if (*text == '\0' || upperAscii(*text) != upperAscii(*prefix))
        {
            return false;
        }
        ++text;
        ++prefix;
    }
    return true;
}

bool equalsIgnoreCase(const char* a, const char* b)
{
    if (!a || !b)
    {
        return false;
    }
    while (*a != '\0' && *b != '\0')
    {
        if (upperAscii(*a) != upperAscii(*b))
        {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

double degToRad(double deg)
{
    return deg * 3.14159265358979323846 / 180.0;
}

double radToDeg(double rad)
{
    return rad * 180.0 / 3.14159265358979323846;
}

double normalizeBearingDeg(double deg)
{
    while (deg < 0.0)
    {
        deg += 360.0;
    }
    while (deg >= 360.0)
    {
        deg -= 360.0;
    }
    return deg;
}

double haversineMeters(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double kEarthRadiusM = 6371000.0;
    const double dlat = degToRad(lat2 - lat1);
    const double dlon = degToRad(lon2 - lon1);
    const double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
                     std::cos(degToRad(lat1)) * std::cos(degToRad(lat2)) *
                         std::sin(dlon / 2.0) * std::sin(dlon / 2.0);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
    return kEarthRadiusM * c;
}

double bearingDegrees(double lat1, double lon1, double lat2, double lon2)
{
    const double lat1r = degToRad(lat1);
    const double lat2r = degToRad(lat2);
    const double dlonr = degToRad(lon2 - lon1);
    const double y = std::sin(dlonr) * std::cos(lat2r);
    const double x = std::cos(lat1r) * std::sin(lat2r) -
                     std::sin(lat1r) * std::cos(lat2r) * std::cos(dlonr);
    return normalizeBearingDeg(radToDeg(std::atan2(y, x)));
}

const char* bearingCardinal(double bearing_deg)
{
    static constexpr const char* kDirs[] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    const int index = static_cast<int>((normalizeBearingDeg(bearing_deg) + 11.25) / 22.5) % 16;
    return kDirs[index];
}

const char* bearingOctant(double bearing_deg)
{
    static constexpr const char* kDirs[] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    const int index = static_cast<int>((normalizeBearingDeg(bearing_deg) + 22.5) / 45.0) % 8;
    return kDirs[index];
}

void formatBearingCompact(char* out, size_t out_len, double bearing_deg)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const int rounded_deg = static_cast<int>(std::lround(normalizeBearingDeg(bearing_deg))) % 360;
    std::snprintf(out, out_len, "%s%d", bearingOctant(bearing_deg), rounded_deg);
}

void drawBearingArrow(MonoDisplay& display, int center_x, int center_y, double bearing_deg, bool selected_row)
{
    const bool on = !selected_row;
    const int dir = static_cast<int>((normalizeBearingDeg(bearing_deg) + 22.5) / 45.0) % 8;
    display.drawPixel(center_x, center_y, on);

    switch (dir)
    {
    case 0: // N
        display.drawPixel(center_x, center_y - 2, on);
        display.drawPixel(center_x - 1, center_y - 1, on);
        display.drawPixel(center_x + 1, center_y - 1, on);
        break;
    case 1: // NE
        display.drawPixel(center_x + 2, center_y - 2, on);
        display.drawPixel(center_x + 1, center_y - 2, on);
        display.drawPixel(center_x + 2, center_y - 1, on);
        break;
    case 2: // E
        display.drawPixel(center_x + 2, center_y, on);
        display.drawPixel(center_x + 1, center_y - 1, on);
        display.drawPixel(center_x + 1, center_y + 1, on);
        break;
    case 3: // SE
        display.drawPixel(center_x + 2, center_y + 2, on);
        display.drawPixel(center_x + 1, center_y + 2, on);
        display.drawPixel(center_x + 2, center_y + 1, on);
        break;
    case 4: // S
        display.drawPixel(center_x, center_y + 2, on);
        display.drawPixel(center_x - 1, center_y + 1, on);
        display.drawPixel(center_x + 1, center_y + 1, on);
        break;
    case 5: // SW
        display.drawPixel(center_x - 2, center_y + 2, on);
        display.drawPixel(center_x - 1, center_y + 2, on);
        display.drawPixel(center_x - 2, center_y + 1, on);
        break;
    case 6: // W
        display.drawPixel(center_x - 2, center_y, on);
        display.drawPixel(center_x - 1, center_y - 1, on);
        display.drawPixel(center_x - 1, center_y + 1, on);
        break;
    default: // NW
        display.drawPixel(center_x - 2, center_y - 2, on);
        display.drawPixel(center_x - 1, center_y - 2, on);
        display.drawPixel(center_x - 2, center_y - 1, on);
        break;
    }
}

void drawLine(MonoDisplay& display, int x0, int y0, int x1, int y1, bool on = true)
{
    int dx = std::abs(x1 - x0);
    const int sx = (x0 < x1) ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    const int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true)
    {
        display.drawPixel(x0, y0, on);
        if (x0 == x1 && y0 == y1)
        {
            break;
        }
        const int e2 = err * 2;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void drawCircle(MonoDisplay& display, int cx, int cy, int radius, bool on = true)
{
    if (radius <= 0)
    {
        return;
    }

    int x = radius;
    int y = 0;
    int err = 0;
    while (x >= y)
    {
        display.drawPixel(cx + x, cy + y, on);
        display.drawPixel(cx + y, cy + x, on);
        display.drawPixel(cx - y, cy + x, on);
        display.drawPixel(cx - x, cy + y, on);
        display.drawPixel(cx - x, cy - y, on);
        display.drawPixel(cx - y, cy - x, on);
        display.drawPixel(cx + y, cy - x, on);
        display.drawPixel(cx + x, cy - y, on);
        ++y;
        if (err <= 0)
        {
            err += (2 * y) + 1;
        }
        if (err > 0)
        {
            --x;
            err -= (2 * x) + 1;
        }
    }
}

void drawCompassArrow(MonoDisplay& display, int cx, int cy, double bearing_deg, int length, bool on = true)
{
    const double angle = degToRad(normalizeBearingDeg(bearing_deg) - 90.0);
    const int tip_x = cx + static_cast<int>(std::lround(std::cos(angle) * length));
    const int tip_y = cy + static_cast<int>(std::lround(std::sin(angle) * length));
    const int tail_x = cx - static_cast<int>(std::lround(std::cos(angle) * (length / 4.0)));
    const int tail_y = cy - static_cast<int>(std::lround(std::sin(angle) * (length / 4.0)));
    const double left_angle = angle + degToRad(150.0);
    const double right_angle = angle - degToRad(150.0);
    const int wing = std::max(3, length / 3);
    const int left_x = tip_x + static_cast<int>(std::lround(std::cos(left_angle) * wing));
    const int left_y = tip_y + static_cast<int>(std::lround(std::sin(left_angle) * wing));
    const int right_x = tip_x + static_cast<int>(std::lround(std::cos(right_angle) * wing));
    const int right_y = tip_y + static_cast<int>(std::lround(std::sin(right_angle) * wing));

    drawLine(display, tail_x, tail_y, tip_x, tip_y, on);
    drawLine(display, tip_x, tip_y, left_x, left_y, on);
    drawLine(display, tip_x, tip_y, right_x, right_y, on);
}

void drawFrame(MonoDisplay& display, int x, int y, int w, int h, bool filled = false)
{
    if (w <= 0 || h <= 0)
    {
        return;
    }

    if (filled)
    {
        display.fillRect(x, y, w, h, true);
        return;
    }

    display.fillRect(x, y, w, 1, true);
    display.fillRect(x, y + h - 1, w, 1, true);
    display.fillRect(x, y, 1, h, true);
    display.fillRect(x + w - 1, y, 1, h, true);
}

void drawBatteryIcon(MonoDisplay& display, int x, int y, int level, bool charging)
{
    drawFrame(display, x, y, 14, 7);
    display.fillRect(x + 14, y + 2, 2, 3, true);
    const int fill_w = clampValue((level < 0 ? 0 : level), 0, 100) / 25;
    for (int i = 0; i < fill_w; ++i)
    {
        display.fillRect(x + 2 + i * 3, y + 2, 2, 3, true);
    }
    if (charging)
    {
        display.fillRect(x + 6, y + 1, 1, 5, true);
        display.drawPixel(x + 7, y + 2, true);
        display.drawPixel(x + 5, y + 4, true);
    }
}

void drawMessageIcon(MonoDisplay& display, int x, int y, bool active)
{
    drawFrame(display, x, y + 1, 10, 7);
    display.drawPixel(x + 1, y + 2, true);
    display.drawPixel(x + 2, y + 3, true);
    display.drawPixel(x + 3, y + 4, true);
    display.drawPixel(x + 4, y + 5, true);
    display.drawPixel(x + 8, y + 2, true);
    display.drawPixel(x + 7, y + 3, true);
    display.drawPixel(x + 6, y + 4, true);
    display.drawPixel(x + 5, y + 5, true);
    if (active)
    {
        display.fillRect(x + 11, y, 2, 2, true);
    }
}

void formatToggleLabel(char* out, size_t out_len, const char* name, bool enabled)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s %s", name ? name : "--", enabled ? "ON" : "OFF");
}

void drawClockSegmentH(MonoDisplay& display, int x, int y, int w)
{
    if (w < 6)
    {
        display.fillRect(x, y, w, 2, true);
        return;
    }
    display.fillRect(x + 1, y, w - 2, 1, true);
    display.fillRect(x, y + 1, w, 1, true);
    display.drawPixel(x + 1, y + 2, true);
    display.drawPixel(x + w - 2, y + 2, true);
}

void drawClockSegmentV(MonoDisplay& display, int x, int y, int h)
{
    if (h < 6)
    {
        display.fillRect(x, y, 2, h, true);
        return;
    }
    display.drawPixel(x + 1, y, true);
    display.fillRect(x, y + 1, 2, h - 2, true);
    display.drawPixel(x, y + h - 1, true);
}

void drawClockDigit(MonoDisplay& display, int x, int y, char ch)
{
    if (ch == ':')
    {
        display.fillRect(x + 1, y + 5, 2, 2, true);
        display.fillRect(x + 1, y + 13, 2, 2, true);
        return;
    }

    if (ch < '0' || ch > '9')
    {
        return;
    }

    static constexpr uint8_t kDigitSegments[] = {
        0b1111110, // 0
        0b0110000, // 1
        0b1101101, // 2
        0b1111001, // 3
        0b0110011, // 4
        0b1011011, // 5
        0b1011111, // 6
        0b1110000, // 7
        0b1111111, // 8
        0b1111011, // 9
    };

    const uint8_t seg = kDigitSegments[ch - '0'];
    constexpr int kDigitW = 8;
    constexpr int kDigitH = 15;
    constexpr int kMidY = 6;
    constexpr int kBottomY = kDigitH - 3;
    constexpr int kLeftX = 0;
    constexpr int kRightX = kDigitW - 2;
    constexpr int kSegW = 6;
    constexpr int kSegH = 4;

    if (seg & 0b1000000)
    {
        drawClockSegmentH(display, x + 1, y, kSegW);
    }
    if (seg & 0b0100000)
    {
        drawClockSegmentV(display, x + kRightX, y + 1, kSegH);
    }
    if (seg & 0b0010000)
    {
        drawClockSegmentV(display, x + kRightX, y + 8, kSegH);
    }
    if (seg & 0b0001000)
    {
        drawClockSegmentH(display, x + 1, y + kBottomY, kSegW);
    }
    if (seg & 0b0000100)
    {
        drawClockSegmentV(display, x + kLeftX, y + 8, kSegH);
    }
    if (seg & 0b0000010)
    {
        drawClockSegmentV(display, x + kLeftX, y + 1, kSegH);
    }
    if (seg & 0b0000001)
    {
        drawClockSegmentH(display, x + 1, y + kMidY, kSegW);
    }
}

void splitClockText(const char* time_text, char* main_out, size_t main_len, char* sec_out, size_t sec_len)
{
    if (main_out && main_len > 0)
    {
        main_out[0] = '\0';
    }
    if (sec_out && sec_len > 0)
    {
        sec_out[0] = '\0';
    }
    if (!time_text || !main_out || main_len == 0)
    {
        return;
    }

    if (std::strlen(time_text) >= 5)
    {
        std::snprintf(main_out, main_len, "%.5s", time_text);
    }
    else
    {
        std::snprintf(main_out, main_len, "%s", time_text);
    }

    if (sec_out && sec_len > 0 && std::strlen(time_text) >= 8)
    {
        std::snprintf(sec_out, sec_len, "%.2s", time_text + 6);
    }
}

int clockGlyphWidth(char ch)
{
    return ch == ':' ? 2 : 8;
}

int measureClockText(const char* text)
{
    if (!text || *text == '\0')
    {
        return 0;
    }

    int width = 0;
    for (const char* p = text; *p != '\0'; ++p)
    {
        if (p != text)
        {
            width += 2;
        }
        width += clockGlyphWidth(*p);
    }
    return width;
}

void drawClockText(MonoDisplay& display, int x, int y, const char* text)
{
    if (!text)
    {
        return;
    }

    int cursor_x = x;
    for (const char* p = text; *p != '\0'; ++p)
    {
        drawClockDigit(display, cursor_x, y, *p);
        cursor_x += clockGlyphWidth(*p) + 2;
    }
}

const char* signalRatingLabel(float snr, float rssi)
{
    if (snr >= 9.0f || rssi >= -90.0f)
    {
        return "STR";
    }
    if (snr >= 4.0f || rssi >= -102.0f)
    {
        return "OK";
    }
    if (snr > -2.0f || rssi >= -112.0f)
    {
        return "WEAK";
    }
    return "POOR";
}

void formatElapsedShort(time_t now_s, uint32_t then_s, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (then_s == 0 || now_s < static_cast<time_t>(1700000000) || now_s < static_cast<time_t>(then_s))
    {
        std::snprintf(out, out_len, "-");
        return;
    }

    uint32_t delta = static_cast<uint32_t>(now_s - static_cast<time_t>(then_s));
    if (delta < 60U)
    {
        std::snprintf(out, out_len, "%lus", static_cast<unsigned long>(delta));
    }
    else if (delta < 3600U)
    {
        std::snprintf(out, out_len, "%lum", static_cast<unsigned long>(delta / 60U));
    }
    else if (delta < 86400U)
    {
        std::snprintf(out, out_len, "%luh", static_cast<unsigned long>(delta / 3600U));
    }
    else
    {
        std::snprintf(out, out_len, "%lud", static_cast<unsigned long>(delta / 86400U));
    }
}

void formatDistanceShort(double meters, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!(meters >= 0.0))
    {
        std::snprintf(out, out_len, "-");
        return;
    }
    if (meters < 1000.0)
    {
        std::snprintf(out, out_len, "%.0fm", meters);
    }
    else if (meters < 10000.0)
    {
        std::snprintf(out, out_len, "%.1fk", meters / 1000.0);
    }
    else
    {
        std::snprintf(out, out_len, "%.0fk", meters / 1000.0);
    }
}

const char* gnssFixLabel(::gps::GnssFix fix)
{
    switch (fix)
    {
    case ::gps::GnssFix::FIX2D:
        return "2D";
    case ::gps::GnssFix::FIX3D:
        return "3D";
    case ::gps::GnssFix::NOFIX:
    default:
        return "NO";
    }
}

const char* gnssSystemLabel(::gps::GnssSystem sys)
{
    switch (sys)
    {
    case ::gps::GnssSystem::GPS:
        return "GPS";
    case ::gps::GnssSystem::GLN:
        return "GLN";
    case ::gps::GnssSystem::GAL:
        return "GAL";
    case ::gps::GnssSystem::BD:
        return "BDS";
    case ::gps::GnssSystem::UNKNOWN:
    default:
        return "UNK";
    }
}

template <size_t N, typename... Args>
void pushFormattedLine(char (&lines)[N][40], size_t& line_count, const char* fmt, Args... args)
{
    if (!fmt || line_count >= N)
    {
        return;
    }
    std::snprintf(lines[line_count], sizeof(lines[line_count]), fmt, args...);
    ++line_count;
}

const char* composeAbcGroupLetters(size_t index)
{
    return index < arrayCount(kComposeAbcGroups) ? kComposeAbcGroups[index].input : "";
}

const char* composeAbcGroupLabel(size_t index)
{
    return index < arrayCount(kComposeAbcGroups) ? kComposeAbcGroups[index].label : "";
}

const char* protocolShortLabel(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::MeshCore ? "MC" : "MT";
}

const char* protocolLabel(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::MeshCore ? "MeshCore" : "Meshtastic";
}

const char* const* radioItemsFor(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::MeshCore ? kMeshCoreRadioItems : kMeshtasticRadioItems;
}

size_t radioItemCount(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::MeshCore ? arrayCount(kMeshCoreRadioItems) : arrayCount(kMeshtasticRadioItems);
}

const char* radioRegionLabel(const app::AppConfig& cfg)
{
    if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
    {
        const auto* region = chat::meshtastic::findRegion(
            static_cast<meshtastic_Config_LoRaConfig_RegionCode>(cfg.meshtastic_config.region));
        return region ? region->label : "-";
    }

    const auto* preset = chat::meshcore::findRegionPresetById(cfg.meshcore_config.meshcore_region_preset);
    return preset ? preset->title : "Custom";
}

const char* gpsSatMaskLabel(uint8_t mask)
{
    switch (mask)
    {
    case 0x1:
        return "GPS";
    case 0x1 | 0x8:
        return "GPS+BDS";
    case 0x1 | 0x4:
        return "GPS+GAL";
    case 0x1 | 0x8 | 0x4:
        return "GPS+BDS+GAL";
    case 0x1 | 0x8 | 0x4 | 0x2:
        return "GPS+BDS+GAL+GLO";
    default:
        return "CUSTOM";
    }
}

uint8_t nextGpsSatMask(uint8_t current, int delta)
{
    static constexpr uint8_t kGpsMasks[] = {
        0x1 | 0x8 | 0x4,
        0x1,
        0x1 | 0x8,
        0x1 | 0x4,
        0x1 | 0x8 | 0x4 | 0x2,
    };

    int index = 0;
    for (size_t i = 0; i < arrayCount(kGpsMasks); ++i)
    {
        if (kGpsMasks[i] == current)
        {
            index = static_cast<int>(i);
            break;
        }
    }
    index = clampValue(index + delta, 0, static_cast<int>(arrayCount(kGpsMasks)) - 1);
    return kGpsMasks[index];
}

void formatTimezoneLabel(int offset_min, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const int hours = offset_min / 60;
    const int minutes = std::abs(offset_min % 60);
    if (minutes == 0)
    {
        std::snprintf(out, out_len, "UTC%+d", hours);
    }
    else
    {
        std::snprintf(out, out_len, "UTC%+d:%02d", hours, minutes);
    }
}

void formatUptimeLabel(uint32_t uptime_seconds, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const uint32_t days = uptime_seconds / 86400U;
    const uint32_t hours = (uptime_seconds / 3600U) % 24U;
    const uint32_t minutes = (uptime_seconds / 60U) % 60U;
    std::snprintf(out,
                  out_len,
                  "%lu d %lu h %lu m",
                  static_cast<unsigned long>(days),
                  static_cast<unsigned long>(hours),
                  static_cast<unsigned long>(minutes));
}

void appendStatus(Runtime* runtime, const char* fmt, ...)
{
    if (!runtime || !fmt)
    {
        return;
    }

    char line[40] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    runtime->appendBootLog(line);
}

template <size_t N>
void appendInfoLine(char (&dst)[N], const char* label, const char* value)
{
    if (!label || !value)
    {
        dst[0] = '\0';
        return;
    }
    std::snprintf(dst, N, "%s:%s", label, value);
}

template <size_t N>
void setInfoSection(char (&dst)[N], const char* title)
{
    if (!title)
    {
        dst[0] = '\0';
        return;
    }
    std::snprintf(dst, N, "[%s]", title);
}

bool encryptEnabled(const app::AppConfig& config)
{
    return config.privacy_encrypt_mode != 0;
}

void setEncryptEnabled(app::AppConfig& config, bool enabled)
{
    config.privacy_encrypt_mode = enabled ? 1 : 0;
}

bool loadBlePairingStatus(app::IAppFacade* app, ble::BlePairingStatus* out)
{
    if (!app || !out)
    {
        return false;
    }
    ble::BleManager* manager = app->getBleManager();
    return manager ? manager->getPairingStatus(out) : false;
}

const char* blePairingModeLabel(const ble::BlePairingStatus& status)
{
    if (!status.requires_passkey)
    {
        return "OPEN";
    }
    return status.is_fixed_pin ? "FIXED" : "RANDOM";
}

} // namespace

Runtime::Runtime(MonoDisplay& display, const HostCallbacks& host)
    : display_(display),
      text_renderer_(host.ui_font ? *host.ui_font : builtin_ui_font()),
      accent_text_renderer_(host.accent_font ? *host.accent_font
                                             : (host.ui_font ? *host.ui_font : builtin_ui_font())),
      host_(host)
{
}

bool Runtime::begin()
{
    if (initialized_)
    {
        return true;
    }
    initialized_ = display_.begin();
    boot_started_ms_ = nowMs();
    page_entered_ms_ = boot_started_ms_;
    last_interaction_ms_ = boot_started_ms_;
    return initialized_;
}

void Runtime::appendBootLog(const char* line)
{
    if (!line || line[0] == '\0')
    {
        return;
    }

    if (boot_log_count_ < kBootLogLines)
    {
        copyText(boot_log_[boot_log_count_], line);
        ++boot_log_count_;
        return;
    }

    for (size_t i = 1; i < kBootLogLines; ++i)
    {
        std::memcpy(boot_log_[i - 1], boot_log_[i], sizeof(boot_log_[i - 1]));
    }
    copyText(boot_log_[kBootLogLines - 1], line);
}

void Runtime::tick(InputAction action)
{
    if (!begin())
    {
        return;
    }

    expireTransientPopup();
    ensureBootExit();
    ensureSleepTimeout(action);
    handleInput(action);
    render();
}

void Runtime::bindChatObservers()
{
    if (chat_observers_bound_ || !app())
    {
        return;
    }

    app()->getChatService().addIncomingTextObserver(this);
    chat_observers_bound_ = true;
}

void Runtime::onIncomingText(const chat::MeshIncomingText& msg)
{
    MessageMetaEntry& entry = message_meta_[message_meta_cursor_ % kMessageMetaCapacity];
    entry.used = true;
    entry.protocol = app() ? app()->getChatService().getActiveProtocol() : chat::MeshProtocol::Meshtastic;
    entry.channel = msg.channel;
    entry.from = msg.from;
    entry.msg_id = msg.msg_id;
    entry.to = msg.to;
    entry.hop_limit = msg.hop_limit;
    entry.encrypted = msg.encrypted;
    entry.rx_meta = msg.rx_meta;
    ++message_meta_cursor_;
}

void Runtime::handleInput(InputAction action)
{
    if (page_ == Page::Compose && action != InputAction::None && host_.debug_log_fn)
    {
        char preedit[32] = {};
        for (size_t i = 0; i < compose_preedit_len_ && i + 1 < sizeof(preedit); ++i)
        {
            preedit[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(compose_preedit_[i])));
            preedit[i + 1] = '\0';
        }

        char line[160] = {};
        std::snprintf(line, sizeof(line),
                      "[gat562][ui] compose action=%s focus=%u mode=%u group=%u tap=%u preedit=\"%s\" cand=%u body_len=%u\n",
                      inputActionName(action),
                      static_cast<unsigned>(compose_focus_),
                      static_cast<unsigned>(compose_mode_),
                      static_cast<unsigned>(compose_abc_group_index_),
                      static_cast<unsigned>(compose_abc_tap_index_),
                      preedit,
                      static_cast<unsigned>(compose_candidate_count_),
                      static_cast<unsigned>(compose_len_));
        host_.debug_log_fn(line);
    }

    if (action == InputAction::None)
    {
        return;
    }

    if (page_ == Page::BootLog)
    {
        if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::MainMenu);
        }
        return;
    }

    if (page_ == Page::Screensaver)
    {
        if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::MainMenu);
        }
        return;
    }

    if (page_ == Page::Sleep)
    {
        if (action != InputAction::None)
        {
            last_interaction_ms_ = nowMs();
            enterPage(page_before_sleep_);
        }
        return;
    }

    if (action != InputAction::None)
    {
        last_interaction_ms_ = nowMs();
    }

    if (setting_popup_active_)
    {
        if (action == InputAction::Back)
        {
            cancelSettingPopup();
        }
        else if (action == InputAction::Select || action == InputAction::Primary)
        {
            confirmSettingPopup();
        }
        else if (action == InputAction::Up || action == InputAction::Right)
        {
            adjustSettingPopup(1);
        }
        else if (action == InputAction::Down || action == InputAction::Left)
        {
            adjustSettingPopup(-1);
        }
        return;
    }

    switch (page_)
    {
    case Page::MainMenu:
        if (action == InputAction::Up && main_menu_index_ > 0)
        {
            --main_menu_index_;
        }
        else if (action == InputAction::Down && main_menu_index_ + 1 < arrayCount(kMainMenuItems))
        {
            ++main_menu_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::Screensaver);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            switch (main_menu_index_)
            {
            case 0:
                enterPage(Page::ChatList);
                break;
            case 1:
                enterPage(Page::NodeList);
                break;
            case 2:
                enterPage(Page::GnssPage);
                break;
            case 3:
                enterPage(Page::SettingsMenu);
                break;
            case 4:
                enterPage(Page::InfoPage);
                break;
            default:
                break;
            }
        }
        break;

    case Page::ChatList:
        if (action == InputAction::Up && chat_list_index_ > 0)
        {
            --chat_list_index_;
        }
        else if (action == InputAction::Down && chat_list_index_ + 1 < conversation_count_)
        {
            ++chat_list_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if ((action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary) &&
                 chat_list_index_ < conversation_count_)
        {
            active_conversation_ = conversations_[chat_list_index_].id;
            enterPage(Page::Conversation);
        }
        break;

    case Page::NodeList:
        if (action == InputAction::Up && node_list_index_ > 0)
        {
            --node_list_index_;
        }
        else if (action == InputAction::Down && node_list_index_ + 1 < node_count_)
        {
            ++node_list_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if (action == InputAction::Right)
        {
            enterPage(Page::NodeActionMenu);
        }
        else if (action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::NodeInfo);
        }
        break;

    case Page::NodeActionMenu:
        if (action == InputAction::Up && node_action_index_ > 0)
        {
            --node_action_index_;
        }
        else if (action == InputAction::Down && node_action_index_ + 1 < nodeActionCount())
        {
            ++node_action_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::NodeList);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            executeNodeAction();
        }
        break;

    case Page::NodeInfo:
        if (action == InputAction::Up && node_info_scroll_ > 0)
        {
            node_info_scroll_ = (node_info_scroll_ >= kNodeInfoPageSize)
                                    ? (node_info_scroll_ - kNodeInfoPageSize)
                                    : 0U;
        }
        else if (action == InputAction::Down && (node_info_scroll_ + kNodeInfoPageSize) < node_info_count_)
        {
            node_info_scroll_ += kNodeInfoPageSize;
        }
        else if (action == InputAction::Left || action == InputAction::Back ||
                 action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::NodeList);
        }
        break;

    case Page::NodeCompass:
        if (action == InputAction::Left || action == InputAction::Back ||
            action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::NodeActionMenu);
        }
        break;

    case Page::Conversation:
        if (action == InputAction::Up && message_index_ > 0)
        {
            --message_index_;
            message_focus_started_ms_ = nowMs();
        }
        else if (action == InputAction::Down && message_index_ + 1 < message_count_)
        {
            ++message_index_;
            message_focus_started_ms_ = nowMs();
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::ChatList);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::MessageMenu);
        }
        break;

    case Page::MessageMenu:
        if (action == InputAction::Up && message_menu_index_ > 0)
        {
            --message_menu_index_;
        }
        else if (action == InputAction::Down && message_menu_index_ + 1 < arrayCount(kMessageMenuItems))
        {
            ++message_menu_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::Conversation);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            if (message_menu_index_ == 0)
            {
                enterPage(Page::MessageInfo);
            }
            else
            {
                openCompose(EditTarget::Message);
            }
        }
        break;

    case Page::MessageInfo:
        if (action == InputAction::Up && message_info_scroll_ > 0)
        {
            message_info_scroll_ = (message_info_scroll_ >= kMessageInfoPageSize)
                                       ? (message_info_scroll_ - kMessageInfoPageSize)
                                       : 0U;
        }
        else if (action == InputAction::Down && (message_info_scroll_ + kMessageInfoPageSize) < message_info_count_)
        {
            message_info_scroll_ += kMessageInfoPageSize;
        }
        else if (action == InputAction::Left || action == InputAction::Back ||
                 action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::MessageMenu);
        }
        break;

    case Page::Compose:
        if (!usesSmartCompose())
        {
            if (action == InputAction::Up)
            {
                adjustComposeSelection(-1);
            }
            else if (action == InputAction::Down)
            {
                adjustComposeSelection(1);
            }
            else if (action == InputAction::Left || action == InputAction::Back)
            {
                if (compose_len_ > 0)
                {
                    removeComposeChar();
                }
                else
                {
                    finishTextEdit(false);
                }
            }
            else if (action == InputAction::Right || action == InputAction::Primary)
            {
                addComposeChar();
            }
            else if (action == InputAction::Secondary)
            {
                appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, ' ');
            }
            else if (action == InputAction::Select)
            {
                if (edit_target_ == EditTarget::Message)
                {
                    sendComposeMessage();
                }
                else
                {
                    finishTextEdit(true);
                }
            }
            break;
        }

        if (action == InputAction::Back)
        {
            finishTextEdit(false);
        }
        else if (compose_focus_ == ComposeFocus::Body)
        {
            if (isAlphaComposeMode(compose_mode_))
            {
                if (action == InputAction::Up)
                {
                    if (compose_abc_group_index_ >= 5U)
                    {
                        compose_abc_group_index_ -= 5U;
                    }
                    else if (compose_candidate_count_ > 0)
                    {
                        compose_focus_ = ComposeFocus::Candidate;
                    }
                }
                else if (action == InputAction::Down)
                {
                    if (compose_abc_group_index_ < 5U)
                    {
                        compose_abc_group_index_ += 5U;
                    }
                    else
                    {
                        compose_focus_ = ComposeFocus::Action;
                    }
                }
                else if (action == InputAction::Left)
                {
                    compose_abc_group_index_ = (compose_abc_group_index_ % 5U == 0U)
                                                   ? (compose_abc_group_index_ + 4U)
                                                   : (compose_abc_group_index_ - 1U);
                }
                else if (action == InputAction::Right)
                {
                    compose_abc_group_index_ = (compose_abc_group_index_ % 5U == 4U)
                                                   ? (compose_abc_group_index_ - 4U)
                                                   : (compose_abc_group_index_ + 1U);
                }
                else if (action == InputAction::Select || action == InputAction::Primary)
                {
                    const char* letters = composeAbcGroupLetters(compose_abc_group_index_);
                    const size_t letter_count = std::strlen(letters);
                    if (letter_count > 0)
                    {
                        const uint32_t now = nowMs();
                        const bool can_cycle = compose_preedit_len_ > 0 &&
                                               compose_abc_last_group_index_ == static_cast<int>(compose_abc_group_index_) &&
                                               (now - compose_abc_last_tap_ms_) <= kComposeMultiTapWindowMs;
                        if (can_cycle)
                        {
                            compose_abc_tap_index_ = (compose_abc_tap_index_ + 1U) % letter_count;
                            compose_preedit_[compose_preedit_len_ - 1U] = letters[compose_abc_tap_index_];
                            compose_abc_last_tap_ms_ = now;
                            rebuildComposeCandidates();
                        }
                        else
                        {
                            compose_abc_tap_index_ = 0;
                            appendComposeChar(letters[0]);
                            compose_abc_last_group_index_ = static_cast<int>(compose_abc_group_index_);
                            compose_abc_last_tap_ms_ = now;
                        }
                    }
                }
                else if (action == InputAction::Secondary)
                {
                    appendComposeChar(' ');
                }
            }
            else if (action == InputAction::Up && compose_candidate_count_ > 0)
            {
                compose_focus_ = ComposeFocus::Candidate;
            }
            else if (action == InputAction::Down)
            {
                compose_focus_ = ComposeFocus::Action;
            }
            else if (action == InputAction::Left)
            {
                adjustComposeSelection(-1);
            }
            else if (action == InputAction::Right)
            {
                adjustComposeSelection(1);
            }
            else if (action == InputAction::Select || action == InputAction::Primary)
            {
                addComposeChar();
            }
            else if (action == InputAction::Secondary)
            {
                appendComposeChar(' ');
            }
        }
        else if (compose_focus_ == ComposeFocus::Candidate)
        {
            if (action == InputAction::Left)
            {
                adjustComposeCandidate(-1);
            }
            else if (action == InputAction::Right)
            {
                adjustComposeCandidate(1);
            }
            else if (action == InputAction::Down)
            {
                compose_focus_ = ComposeFocus::Body;
            }
            else if (action == InputAction::Select || action == InputAction::Primary)
            {
                (void)commitComposeCandidate();
                compose_focus_ = ComposeFocus::Body;
            }
        }
        else
        {
            if (action == InputAction::Left)
            {
                adjustComposeAction(-1);
            }
            else if (action == InputAction::Right)
            {
                adjustComposeAction(1);
            }
            else if (action == InputAction::Up)
            {
                compose_focus_ = compose_candidate_count_ > 0 ? ComposeFocus::Candidate : ComposeFocus::Body;
            }
            else if (action == InputAction::Select || action == InputAction::Primary)
            {
                activateComposeAction();
            }
        }
        break;

    case Page::SettingsMenu:
        if (action == InputAction::Up && settings_menu_index_ > 0)
        {
            --settings_menu_index_;
        }
        else if (action == InputAction::Down && settings_menu_index_ + 1 < arrayCount(kSettingsMenuItems))
        {
            ++settings_menu_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            switch (settings_menu_index_)
            {
            case 0:
                enterPage(Page::RadioSettings);
                break;
            case 1:
                enterPage(Page::DeviceSettings);
                break;
            case 2:
                enterPage(Page::ActionPage);
                break;
            default:
                break;
            }
        }
        break;

    case Page::InfoPage:
        if (action == InputAction::Up && info_scroll_ > 0)
        {
            info_scroll_ = (info_scroll_ >= kInfoPageSize) ? (info_scroll_ - kInfoPageSize) : 0U;
        }
        else if (action == InputAction::Down)
        {
            info_scroll_ += kInfoPageSize;
        }
        else if (action == InputAction::Left || action == InputAction::Back ||
                 action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::MainMenu);
        }
        break;

    case Page::RadioSettings:
        if (action == InputAction::Up && radio_index_ > 0)
        {
            --radio_index_;
        }
        else if (action == InputAction::Down && radio_index_ + 1 < radioItemCount(app()->getConfig().mesh_protocol))
        {
            ++radio_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::SettingsMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            if (app()->getConfig().mesh_protocol == chat::MeshProtocol::MeshCore &&
                radio_index_ + 1 == radioItemCount(app()->getConfig().mesh_protocol))
            {
                openCompose(EditTarget::MeshCoreChannelName, app()->getConfig().meshcore_config.meshcore_channel_name);
            }
            else
            {
                beginSettingPopup(Page::RadioSettings, radio_index_);
            }
        }
        break;

    case Page::DeviceSettings:
        if (action == InputAction::Up && device_index_ > 0)
        {
            --device_index_;
        }
        else if (action == InputAction::Down && device_index_ + 1 < arrayCount(kDeviceItems))
        {
            ++device_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::SettingsMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            beginSettingPopup(Page::DeviceSettings, device_index_);
        }
        break;

    case Page::GnssPage:
        if (action == InputAction::Up && gnss_page_index_ > 0)
        {
            --gnss_page_index_;
        }
        else if (action == InputAction::Down)
        {
            ++gnss_page_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back || action == InputAction::Select)
        {
            enterPage(Page::MainMenu);
        }
        break;

    case Page::ActionPage:
        if (action == InputAction::Up && action_index_ > 0)
        {
            --action_index_;
        }
        else if (action == InputAction::Down && action_index_ + 1 < arrayCount(kActionItems))
        {
            ++action_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::SettingsMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            if (action_index_ == 1 || action_index_ == 2)
            {
                beginSettingPopup(Page::ActionPage, action_index_);
            }
            else
            {
                executeActionPageItem(action_index_);
            }
        }
        break;

    default:
        break;
    }
}

void Runtime::render()
{
    display_.clear();
    switch (page_)
    {
    case Page::BootLog:
        renderBootLog();
        break;
    case Page::Screensaver:
        renderScreensaver();
        break;
    case Page::Sleep:
        renderSleep();
        break;
    case Page::MainMenu:
        renderMainMenu();
        break;
    case Page::ChatList:
        renderChatList();
        break;
    case Page::NodeList:
        renderNodeList();
        break;
    case Page::NodeActionMenu:
        renderNodeActionMenu();
        break;
    case Page::NodeInfo:
        renderNodeInfo();
        break;
    case Page::NodeCompass:
        renderNodeCompass();
        break;
    case Page::Conversation:
        renderConversation();
        break;
    case Page::MessageMenu:
        renderMessageMenu();
        break;
    case Page::MessageInfo:
        renderMessageInfo();
        break;
    case Page::Compose:
        renderCompose();
        break;
    case Page::SettingsMenu:
        renderSettingsMenu();
        break;
    case Page::RadioSettings:
        renderRadioSettings();
        break;
    case Page::DeviceSettings:
        renderDeviceSettings();
        break;
    case Page::InfoPage:
        renderInfoPage();
        break;
    case Page::GnssPage:
        renderGnssPage();
        break;
    case Page::ActionPage:
        renderActionPage();
        break;
    default:
        renderScreensaver();
        break;
    }

    if (setting_popup_active_)
    {
        renderSettingPopup();
    }

    if (transient_popup_active_)
    {
        renderTransientPopup();
    }

    ble::BlePairingStatus ble_status{};
    if (loadBlePairingStatus(app(), &ble_status) &&
        ble_status.requires_passkey &&
        ble_status.is_pairing_active)
    {
        display_.fillRect(0, 45, display_.width(), 19, false);
        display_.drawHLine(0, 44, display_.width());
        char line1[24] = {};
        char line2[24] = {};
        std::snprintf(line1, sizeof(line1), "BLE %s PIN", ble_status.is_fixed_pin ? "FIXED" : "PAIR");
        std::snprintf(line2, sizeof(line2), "%06lu", static_cast<unsigned long>(ble_status.passkey));
        text_renderer_.drawText(display_, 0, 46, line1);
        text_renderer_.drawText(display_, 0, 54, line2);
    }

    display_.present();
}

void Runtime::renderBootLog()
{
    drawTitleBar("BOOT", nullptr);
    const int line_h = text_renderer_.lineHeight();
    const int start_y = 10;
    const size_t visible = std::min(boot_log_count_, static_cast<size_t>(6));
    for (size_t i = 0; i < visible; ++i)
    {
        drawTextClipped(0, start_y + static_cast<int>(i * line_h), display_.width(), boot_log_[boot_log_count_ - visible + i]);
    }
}

void Runtime::refreshGnssSnapshot(bool force)
{
    const uint32_t now = nowMs();
    const bool stale = !gnss_snapshot_valid_ ||
                       now < gnss_snapshot_updated_ms_ ||
                       (now - gnss_snapshot_updated_ms_) >= kGnssSnapshotRefreshMs;
    if (!force && !stale)
    {
        return;
    }

    gnss_snapshot_state_ = host_.gps_data_fn ? host_.gps_data_fn() : platform::ui::gps::GpsState{};
    gnss_snapshot_status_ = platform::ui::gps::GnssStatus{};
    gnss_snapshot_count_ = 0;
    gnss_snapshot_sats_.fill(platform::ui::gps::GnssSatInfo{});
    (void)platform::ui::gps::get_gnss_snapshot(gnss_snapshot_sats_.data(),
                                               gnss_snapshot_sats_.size(),
                                               &gnss_snapshot_count_,
                                               &gnss_snapshot_status_);
    gnss_snapshot_updated_ms_ = now;
    gnss_snapshot_valid_ = true;
}

void Runtime::renderScreensaver()
{
    char protocol[8] = {};
    char freq[20] = {};
    char time_buf[16] = {};
    char time_main_buf[8] = {};
    char time_sec_buf[4] = {};
    char date_buf[24] = {};
    char status_buf[16] = {};
    char node_buf[12] = {};
    char tz_buf[16] = {};
    char unread_buf[8] = {};
    char bat_pct_buf[8] = {};
    char sat_buf[16] = {};
    char top_left_buf[32] = {};
    char top_right_buf[32] = {};
    char left_toggle_buf[12] = {};
    char right_toggle_buf[12] = {};
    formatProtocol(protocol, sizeof(protocol));
    formatNodeLabel(node_buf, sizeof(node_buf));
    formatTime(time_buf, sizeof(time_buf), date_buf, sizeof(date_buf));
    splitClockText(time_buf, time_main_buf, sizeof(time_main_buf), time_sec_buf, sizeof(time_sec_buf));
    formatTimezoneLabel(host_.timezone_offset_min_fn ? host_.timezone_offset_min_fn() : 0, tz_buf, sizeof(tz_buf));
    if (host_.format_frequency_fn)
    {
        host_.format_frequency_fn(host_.active_lora_frequency_hz_fn ? host_.active_lora_frequency_hz_fn() : 0U,
                                  freq,
                                  sizeof(freq));
    }

    refreshGnssSnapshot();
    const auto battery = host_.battery_info_fn ? host_.battery_info_fn() : platform::ui::device::BatteryInfo{};
    const auto& gps = gnss_snapshot_state_;
    const auto& gnss_status = gnss_snapshot_status_;
    const int unread = app() ? app()->getChatService().getTotalUnread() : 0;
    const bool gps_enabled = host_.gps_enabled_fn && host_.gps_enabled_fn();
    const bool ble_enabled = app() && app()->isBleEnabled();
    std::snprintf(unread_buf, sizeof(unread_buf), unread > 99 ? "99+" : "%d", unread);
    if (battery.available && battery.level >= 0)
    {
        std::snprintf(bat_pct_buf, sizeof(bat_pct_buf), "BAT:%d%%", battery.level);
    }
    else
    {
        std::snprintf(bat_pct_buf, sizeof(bat_pct_buf), "BAT:--");
    }
    const unsigned satellite_count = gnss_status.sats_in_view > 0
                                         ? static_cast<unsigned>(gnss_status.sats_in_view)
                                         : (gnss_snapshot_count_ > 0
                                                ? static_cast<unsigned>(gnss_snapshot_count_)
                                                : static_cast<unsigned>(gps.satellites));
    if (satellite_count > 0)
    {
        std::snprintf(sat_buf, sizeof(sat_buf), "SAT %u", satellite_count);
    }
    else if (gps_enabled)
    {
        std::snprintf(sat_buf, sizeof(sat_buf), "SAT --");
    }
    else
    {
        std::snprintf(sat_buf, sizeof(sat_buf), "SAT OFF");
    }
    const bool clock_unsynced = std::strcmp(date_buf, "TIME UNSYNC") == 0;
    if (clock_unsynced)
    {
        std::snprintf(status_buf, sizeof(status_buf), "CLOCK UNSYNC");
    }
    else
    {
        std::snprintf(status_buf, sizeof(status_buf), "%s", date_buf);
    }
    std::snprintf(top_left_buf, sizeof(top_left_buf), "%s %s", protocol[0] ? protocol : "--", bat_pct_buf);
    std::snprintf(top_right_buf, sizeof(top_right_buf), "%s", freq[0] ? freq : "--");
    formatToggleLabel(left_toggle_buf, sizeof(left_toggle_buf), "GPS", gps_enabled);
    formatToggleLabel(right_toggle_buf, sizeof(right_toggle_buf), "BLE", ble_enabled);

    constexpr int kTopY = 1;
    constexpr int kTopDetailY = 9;
    constexpr int kTimeY = 22;
    constexpr int kSideToggleY = 26;
    constexpr int kSecY = 28;
    constexpr int kDateY = 42;
    constexpr int kFooterY = 55;

    drawTextClipped(2, kTopY, 60, top_left_buf);
    const int top_right_w = text_renderer_.measureTextWidth(top_right_buf);
    text_renderer_.drawText(display_, std::max(66, display_.width() - top_right_w - 2), kTopY, top_right_buf);
    drawTextClipped(2, kTopDetailY, 42, sat_buf);
    char top_detail_right[16] = {};
    std::snprintf(top_detail_right, sizeof(top_detail_right), "MSG %s", unread_buf);
    const int top_detail_right_w = text_renderer_.measureTextWidth(top_detail_right);
    text_renderer_.drawText(display_, std::max(70, display_.width() - top_detail_right_w - 2), kTopDetailY, top_detail_right);

    const int time_w = measureClockText(time_main_buf);
    const int time_x = std::max(0, (display_.width() - time_w) / 2);
    drawClockText(display_, time_x, kTimeY, time_main_buf);
    if (time_sec_buf[0] != '\0')
    {
        const int sec_x = std::min(display_.width() - 12, time_x + time_w + 4);
        text_renderer_.drawText(display_, sec_x, kSecY, time_sec_buf);
    }
    drawTextClipped(0, kSideToggleY, 28, left_toggle_buf);
    const int right_toggle_w = text_renderer_.measureTextWidth(right_toggle_buf);
    text_renderer_.drawText(display_, std::max(96, display_.width() - right_toggle_w), kSideToggleY, right_toggle_buf);
    const int status_w = text_renderer_.measureTextWidth(status_buf);
    text_renderer_.drawText(display_, std::max(0, (display_.width() - status_w) / 2), kDateY, status_buf);

    char footer_left[24] = {};
    char footer_right[24] = {};
    std::snprintf(footer_left, sizeof(footer_left), "ID %s", node_buf[0] ? node_buf : "--");
    std::snprintf(footer_right, sizeof(footer_right), "%s", tz_buf[0] ? tz_buf : "UTC+0");
    drawTextClipped(3, kFooterY, 76, footer_left);
    const int right_w = text_renderer_.measureTextWidth(footer_right);
    text_renderer_.drawText(display_, std::max(80, display_.width() - right_w - 3), kFooterY, footer_right);

    if (battery.available && battery.level >= 0 && battery.level <= 20)
    {
        display_.fillRect(display_.width() - 10, kFooterY + text_renderer_.lineHeight() - 2, 7, 2, true);
    }
}

void Runtime::renderSleep()
{
}

void Runtime::renderMainMenu()
{
    drawMenuList("MENU", kMainMenuItems, arrayCount(kMainMenuItems), main_menu_index_);
}

void Runtime::renderChatList()
{
    rebuildConversationList();
    const size_t total_pages = std::max<size_t>(1U, (conversation_count_ + kChatListPageSize - 1U) / kChatListPageSize);
    const size_t selected = (conversation_count_ > 0)
                                ? std::min(chat_list_index_, conversation_count_ - 1U)
                                : 0U;
    const size_t start = (conversation_count_ > 0) ? ((selected / kChatListPageSize) * kChatListPageSize) : 0U;
    const size_t current_page = (conversation_count_ > 0) ? ((start / kChatListPageSize) + 1U) : 1U;
    char pos[16] = {};
    std::snprintf(pos, sizeof(pos), "%u/%u",
                  static_cast<unsigned>(current_page),
                  static_cast<unsigned>(total_pages));
    drawTitleBar("CHATS", pos);
    if (conversation_count_ == 0)
    {
        text_renderer_.drawText(display_, 0, 18, "NO CONVERSATIONS");
        return;
    }

    const int line_h = text_renderer_.lineHeight();
    const size_t visible = std::min(conversation_count_ - start, kChatListPageSize);
    for (size_t i = 0; i < visible; ++i)
    {
        const size_t conversation_index = start + i;
        const bool selected_row = (conversation_index == selected);
        char line[32] = {};
        const auto& conv = conversations_[conversation_index];
        std::snprintf(line, sizeof(line), "%s%s",
                      conv.unread > 0 ? "*" : "",
                      conv.name.c_str());
        const int y = 10 + static_cast<int>(i * line_h);
        drawTextClipped(0, y, display_.width(), line, selected_row);
    }
}

void Runtime::renderNodeList()
{
    rebuildNodeList();
    const size_t selected = (node_count_ > 0)
                                ? std::min(node_list_index_, node_count_ - 1U)
                                : 0U;
    const size_t start = (node_count_ > 0) ? ((selected / kNodeListPageSize) * kNodeListPageSize) : 0U;
    constexpr int kHeaderY = 0;
    const int line_h = text_renderer_.lineHeight();
    constexpr int kRowStartY = 10;
    constexpr int kNameX = 0;
    constexpr int kNameW = 22;
    constexpr int kAgeX = 24;
    constexpr int kAgeW = 14;
    constexpr int kDistX = 40;
    constexpr int kDistW = 18;
    constexpr int kBrgX = 60;
    constexpr int kBrgW = 28;
    constexpr int kHopsX = 90;
    constexpr int kHopsW = 12;
    constexpr int kBarsX = 106;

    display_.fillRect(0, kHeaderY, display_.width(), line_h + 1, true);
    drawTextClipped(kAgeX, kHeaderY, kAgeW, "AGE", true);
    drawTextClipped(kDistX, kHeaderY, kDistW, "DST", true);
    drawTextClipped(kBrgX, kHeaderY, kBrgW, "BRG", true);
    drawTextClipped(kHopsX, kHeaderY, kHopsW, "HP", true);
    if (node_count_ == 0)
    {
        text_renderer_.drawText(display_, 0, kRowStartY, "NO NODES");
        return;
    }

    const size_t visible = std::min(node_count_ - start, kNodeListPageSize);

    for (size_t i = 0; i < visible; ++i)
    {
        const size_t node_index = start + i;
        const auto& node = nodes_[node_index];
        const int row_y = kRowStartY + static_cast<int>(i * line_h);
        const bool selected_row = (node_index == selected);
        char node_id[8] = {};
        std::snprintf(node_id, sizeof(node_id), "%04lX",
                      static_cast<unsigned long>(node.node_id & 0xFFFFUL));
        char label[16] = {};
        std::snprintf(label, sizeof(label), "%s", node_id);

        if (selected_row)
        {
            display_.fillRect(0, row_y, display_.width(), line_h, true);
        }

        char age_buf[8] = {};
        const time_t now_s = host_.utc_now_fn ? host_.utc_now_fn() : 0;
        formatElapsedShort(now_s, node.last_seen, age_buf, sizeof(age_buf));

        char dist_buf[12] = {};
        const auto gps = host_.gps_data_fn ? host_.gps_data_fn() : platform::ui::gps::GpsState{};
        if (gps.valid && node.position.valid)
        {
            const double node_lat = static_cast<double>(node.position.latitude_i) / 1e7;
            const double node_lon = static_cast<double>(node.position.longitude_i) / 1e7;
            formatDistanceShort(haversineMeters(gps.lat, gps.lng, node_lat, node_lon), dist_buf, sizeof(dist_buf));
        }
        else
        {
            std::snprintf(dist_buf, sizeof(dist_buf), "-");
        }

        bool has_bearing = false;
        double bearing_deg = 0.0;
        if (gps.valid && node.position.valid)
        {
            const double node_lat = static_cast<double>(node.position.latitude_i) / 1e7;
            const double node_lon = static_cast<double>(node.position.longitude_i) / 1e7;
            bearing_deg = bearingDegrees(gps.lat, gps.lng, node_lat, node_lon);
            has_bearing = true;
        }

        const char* sig = signalRatingLabel(node.snr, node.rssi);
        char hops_buf[6] = {};
        if (node.hops_away == 0xFF)
        {
            std::snprintf(hops_buf, sizeof(hops_buf), "-");
        }
        else
        {
            std::snprintf(hops_buf, sizeof(hops_buf), "%u", static_cast<unsigned>(node.hops_away));
        }
        drawTextClipped(kNameX, row_y, kNameW, label, selected_row);
        drawTextClipped(kAgeX, row_y, kAgeW, age_buf, selected_row);
        drawTextClipped(kDistX, row_y, kDistW, dist_buf, selected_row);
        drawTextClipped(kHopsX, row_y, kHopsW, hops_buf, selected_row);
        if (has_bearing)
        {
            char bearing_buf[10] = {};
            formatBearingCompact(bearing_buf, sizeof(bearing_buf), bearing_deg);
            drawTextClipped(kBrgX, row_y, kBrgW, bearing_buf, selected_row);
        }
        else
        {
            drawTextClipped(kBrgX, row_y, kBrgW, "-", selected_row);
        }

        const int bars = std::strcmp(sig, "STR") == 0 ? 3 : std::strcmp(sig, "OK") == 0 ? 2
                                                        : std::strcmp(sig, "WEAK") == 0 ? 1
                                                                                        : 0;
        for (int bar = 0; bar < 3; ++bar)
        {
            if (bar >= bars)
            {
                continue;
            }
            const int bar_h = 2 + bar * 3;
            const int bar_x = kBarsX + bar * 4;
            const int bar_y = row_y + 6 - bar_h;
            display_.fillRect(bar_x, bar_y, 3, bar_h, !selected_row);
        }
    }
}

void Runtime::renderNodeInfo()
{
    buildNodeInfo();
    char pos[24] = {};
    if (node_info_count_ > 0)
    {
        const size_t total_pages = (node_info_count_ + kNodeInfoPageSize - 1U) / kNodeInfoPageSize;
        const size_t current_page = (node_info_scroll_ / kNodeInfoPageSize) + 1U;
        std::snprintf(pos, sizeof(pos), "%u/%u",
                      static_cast<unsigned>(current_page),
                      static_cast<unsigned>(total_pages));
    }
    drawTitleBar("NODE", pos[0] != '\0' ? pos : nullptr);
    if (node_info_count_ == 0)
    {
        text_renderer_.drawText(display_, 0, 18, "NO INFO");
        return;
    }

    const int line_h = text_renderer_.lineHeight();
    const size_t start = std::min(node_info_scroll_, node_info_count_);
    const size_t visible = std::min(node_info_count_ - start, kNodeInfoPageSize);
    for (size_t i = 0; i < visible && (start + i) < node_info_count_; ++i)
    {
        drawTextClipped(0, 10 + static_cast<int>(i * line_h), display_.width(), node_info_lines_[start + i], false);
    }
}

void Runtime::renderNodeCompass()
{
    const chat::contacts::NodeInfo* node = selectedNode();
    char right[12] = {};
    if (node != nullptr)
    {
        std::snprintf(right, sizeof(right), "%04lX",
                      static_cast<unsigned long>(node->node_id & 0xFFFFUL));
    }
    drawTitleBar("COMPASS", right[0] != '\0' ? right : nullptr);

    if (node == nullptr)
    {
        text_renderer_.drawText(display_, 0, 18, "NO NODE");
        return;
    }

    const char* title_name = node->display_name.empty()
                                 ? (node->short_name[0] != '\0' ? node->short_name : "NODE")
                                 : node->display_name.c_str();
    drawTextClipped(0, 10, display_.width(), title_name);

    const auto gps = host_.gps_data_fn ? host_.gps_data_fn() : platform::ui::gps::GpsState{};
    if (!gps.valid)
    {
        text_renderer_.drawText(display_, 0, 26, "GPS UNAVAILABLE");
        text_renderer_.drawText(display_, 0, 36, "NEED LOCAL FIX");
        return;
    }
    if (!node->position.valid)
    {
        text_renderer_.drawText(display_, 0, 26, "NODE POSITION");
        text_renderer_.drawText(display_, 0, 36, "UNAVAILABLE");
        return;
    }

    const double node_lat = static_cast<double>(node->position.latitude_i) / 1e7;
    const double node_lon = static_cast<double>(node->position.longitude_i) / 1e7;
    const double dist_m = haversineMeters(gps.lat, gps.lng, node_lat, node_lon);
    const double abs_bearing_deg = bearingDegrees(gps.lat, gps.lng, node_lat, node_lon);
    const double rel_bearing_deg = gps.has_course ? normalizeBearingDeg(abs_bearing_deg - gps.course_deg) : abs_bearing_deg;

    constexpr int kCenterX = 25;
    constexpr int kCenterY = 39;
    constexpr int kRadius = 17;
    constexpr int kInfoRightX = 127;
    constexpr int kInfoW = 58;
    constexpr int kInfoMinX = kInfoRightX - kInfoW + 1;
    auto drawInfoRight = [this](int y, const char* text)
    {
        if (!text || text[0] == '\0')
        {
            return;
        }

        const char* draw_text = text;
        char clipped[32] = {};
        if (text_renderer_.measureTextWidth(text) > kInfoW)
        {
            if (kInfoW > text_renderer_.ellipsisWidth())
            {
                const size_t keep_bytes = text_renderer_.clipTextToWidth(text, kInfoW - text_renderer_.ellipsisWidth());
                std::memcpy(clipped, text, keep_bytes);
                clipped[keep_bytes] = '\0';
                std::strcat(clipped, "...");
            }
            else
            {
                const size_t keep_bytes = text_renderer_.clipTextToWidth(text, kInfoW);
                std::memcpy(clipped, text, keep_bytes);
                clipped[keep_bytes] = '\0';
            }
            draw_text = clipped;
        }

        const int draw_w = text_renderer_.measureTextWidth(draw_text);
        const int draw_x = std::max(kInfoMinX, kInfoRightX - draw_w);
        text_renderer_.drawText(display_, draw_x, y, draw_text);
    };
    drawCircle(display_, kCenterX, kCenterY, kRadius);
    display_.drawPixel(kCenterX, kCenterY, true);
    text_renderer_.drawText(display_, kCenterX - 2, kCenterY - kRadius - 7, "N");
    text_renderer_.drawText(display_, kCenterX - 2, kCenterY + kRadius + 1, "S");
    text_renderer_.drawText(display_, kCenterX - kRadius - 7, kCenterY - 3, "W");
    text_renderer_.drawText(display_, kCenterX + kRadius + 3, kCenterY - 3, "E");
    display_.drawPixel(kCenterX, kCenterY - kRadius + 3, true);
    display_.drawPixel(kCenterX - 1, kCenterY - kRadius + 4, true);
    display_.drawPixel(kCenterX + 1, kCenterY - kRadius + 4, true);

    if (gps.has_course)
    {
        drawCompassArrow(display_, kCenterX, kCenterY, rel_bearing_deg, kRadius - 4, true);
    }
    else
    {
        drawCompassArrow(display_, kCenterX, kCenterY, abs_bearing_deg, kRadius - 4, true);
    }

    char line[24] = {};
    if (dist_m < 1000.0)
    {
        std::snprintf(line, sizeof(line), "DST %.0fm", dist_m);
    }
    else
    {
        std::snprintf(line, sizeof(line), "DST %.2fkm", dist_m / 1000.0);
    }
    drawInfoRight(18, line);

    std::snprintf(line, sizeof(line), "BRG %s %.0f", bearingCardinal(abs_bearing_deg), abs_bearing_deg);
    drawInfoRight(26, line);

    if (gps.has_course)
    {
        std::snprintf(line, sizeof(line), "HDG %.0f", gps.course_deg);
        drawInfoRight(34, line);
        std::snprintf(line, sizeof(line), "REL %.0f", rel_bearing_deg);
        drawInfoRight(42, line);
    }
    else
    {
        drawInfoRight(34, "HDG N/A");
        drawInfoRight(42, "REL N/A");
    }

    if (node->position.has_altitude)
    {
        std::snprintf(line, sizeof(line), "ALT %ldm", static_cast<long>(node->position.altitude));
    }
    else
    {
        std::snprintf(line, sizeof(line), "ALT -");
    }
    drawInfoRight(50, line);

    char age_buf[12] = {};
    formatElapsedShort(host_.utc_now_fn ? host_.utc_now_fn() : 0, node->last_seen, age_buf, sizeof(age_buf));
    char footer[24] = {};
    const char* proto = node->protocol == chat::contacts::NodeProtocolType::MeshCore     ? "MC"
                        : node->protocol == chat::contacts::NodeProtocolType::Meshtastic ? "MT"
                                                                                         : "?";
    if (node->hops_away == 0xFF)
    {
        std::snprintf(footer, sizeof(footer), "%s  %s", proto, age_buf);
    }
    else
    {
        std::snprintf(footer, sizeof(footer), "%s H%u %s",
                      proto,
                      static_cast<unsigned>(node->hops_away),
                      age_buf);
    }
    drawInfoRight(58, footer);
}

void Runtime::renderNodeActionMenu()
{
    const auto* node = selectedNode();
    std::array<const char*, kNodeActionItemCount> items{};
    for (size_t i = 0; i < items.size(); ++i)
    {
        items[i] = nodeActionLabel(i);
    }
    char title[20] = {};
    if (node)
    {
        std::snprintf(title, sizeof(title), "%04lX", static_cast<unsigned long>(node->node_id & 0xFFFFUL));
    }
    drawMenuList(title[0] != '\0' ? title : "NODE", items.data(), items.size(), node_action_index_);
}

void Runtime::renderConversation()
{
    rebuildMessages();
    char title[20] = {};
    if (active_conversation_.peer == 0)
    {
        copyText(title, "BROADCAST");
    }
    else
    {
        std::snprintf(title, sizeof(title), "%08lX", static_cast<unsigned long>(active_conversation_.peer));
    }
    drawTitleBar(title, nullptr);

    if (message_count_ == 0)
    {
        text_renderer_.drawText(display_, 0, 18, "NO MESSAGES");
        return;
    }

    const int line_h = text_renderer_.lineHeight();
    constexpr int kConversationStartY = 10;
    constexpr int kBubbleGap = 1;
    constexpr int kBubbleWidth = 118;
    constexpr int kBubbleBodyBottomPadding = 2;
    constexpr size_t kVisibleConversationBubbles = 2;
    const int bubble_h = (line_h * 2) + 1 + kBubbleBodyBottomPadding;
    const int row_h = bubble_h + kBubbleGap;
    const int bubble_w = std::max(8, std::min(kBubbleWidth, display_.width() - 2));
    const size_t visible = std::min(message_count_, kVisibleConversationBubbles);
    const size_t selected_index = std::min(message_index_, message_count_ - 1U);
    size_t start = 0;
    if (message_count_ > visible)
    {
        start = (selected_index + 1 > visible) ? (selected_index + 1 - visible) : 0;
        if (start + visible > message_count_)
        {
            start = message_count_ - visible;
        }
    }
    for (size_t i = 0; i < visible; ++i)
    {
        const size_t msg_index = start + i;
        const auto& msg = messages_[msg_index];
        const bool selected = (msg_index == selected_index);
        char sender[16] = {};
        char time_text[16] = {};
        uint32_t display_timestamp = msg.timestamp;
        if (selected && display_timestamp < 1700000000U)
        {
            for (size_t meta_index = 0; meta_index < kMessageMetaCapacity; ++meta_index)
            {
                const MessageMetaEntry& candidate = message_meta_[meta_index];
                if (!candidate.used)
                {
                    continue;
                }
                if (candidate.protocol == msg.protocol &&
                    candidate.channel == msg.channel &&
                    candidate.from == msg.from &&
                    candidate.msg_id == msg.msg_id &&
                    candidate.rx_meta.rx_timestamp_s >= 1700000000U)
                {
                    display_timestamp = candidate.rx_meta.rx_timestamp_s;
                    break;
                }
            }
        }
        formatConversationSender(sender, sizeof(sender), msg, selected);
        formatConversationTime(time_text, sizeof(time_text), display_timestamp, selected);

        const int y = kConversationStartY + static_cast<int>(i * row_h);
        const bool is_self = (msg.from == 0);
        const int x = is_self ? (display_.width() - bubble_w - 1) : 1;
        drawConversationBubble(x, y, bubble_w, sender, time_text, msg.text.c_str(), selected, is_self);
    }
}

void Runtime::renderMessageMenu()
{
    drawMenuList("MESSAGE", kMessageMenuItems, arrayCount(kMessageMenuItems), message_menu_index_);
}

void Runtime::renderMessageInfo()
{
    buildMessageInfo();
    char pos[24] = {};
    if (message_info_count_ > 0)
    {
        const size_t total_pages = (message_info_count_ + kMessageInfoPageSize - 1U) / kMessageInfoPageSize;
        const size_t current_page = (message_info_scroll_ / kMessageInfoPageSize) + 1U;
        std::snprintf(pos, sizeof(pos), "%u/%u",
                      static_cast<unsigned>(current_page),
                      static_cast<unsigned>(total_pages));
    }
    drawTitleBar("INFO", pos[0] != '\0' ? pos : nullptr);
    if (message_info_count_ == 0)
    {
        text_renderer_.drawText(display_, 0, 18, "NO INFO");
        return;
    }

    const int line_h = text_renderer_.lineHeight();
    const size_t start = std::min(message_info_scroll_, message_info_count_);
    const size_t visible = std::min(message_info_count_ - start, kMessageInfoPageSize);
    for (size_t i = 0; i < visible && (start + i) < message_info_count_; ++i)
    {
        drawTextClipped(0, 10 + static_cast<int>(i * line_h), display_.width(), message_info_lines_[start + i], false);
    }
}

void Runtime::renderCompose()
{
    if (!usesSmartCompose())
    {
        drawTitleBar(edit_target_ == EditTarget::Message ? "COMPOSE" : "EDIT", nullptr);
        drawTextClipped(0, 12, display_.width(), compose_buffer_);

        const char* charset = editUsesHexCharset() ? kHexCharset : kComposeCharset;
        const size_t charset_len = std::strlen(charset);
        const char current = charset[compose_charset_index_ % charset_len];
        char pick[8] = {};
        std::snprintf(pick, sizeof(pick), "[%c]", current);
        text_renderer_.drawText(display_, 0, 34, pick);

        text_renderer_.drawText(display_, 40, 34, "U/D PICK");
        text_renderer_.drawText(display_, 40, 44, "R ADD");
        text_renderer_.drawText(display_, 40, 54, "L DEL OK");
        return;
    }

    const int line_h = text_renderer_.lineHeight();

    char target[16] = {};
    formatComposeTarget(target, sizeof(target));
    char to_line[24] = {};
    std::snprintf(to_line, sizeof(to_line), "TO:%s", target[0] != '\0' ? target : "-");
    drawTextClipped(0, 0, display_.width(), to_line, false);

    constexpr size_t kBodyVisiblePerLine = 18;
    char body_text[kComposeMax + 1] = {};
    if (compose_len_ > 0)
    {
        copyText(body_text, compose_buffer_);
    }

    const size_t body_text_len = std::strlen(body_text);
    const size_t body_window = kBodyVisiblePerLine * 2U;
    const size_t body_start = body_text_len > body_window ? (body_text_len - body_window) : 0U;
    const size_t visible_len = body_text_len - body_start;
    const size_t split = visible_len > kBodyVisiblePerLine ? (visible_len - kBodyVisiblePerLine) : 0U;

    char body_line_1[kBodyVisiblePerLine + 1] = {};
    char body_line_2[kBodyVisiblePerLine + 1] = {};
    if (visible_len == 0)
    {
        std::snprintf(body_line_1, sizeof(body_line_1), "_");
    }
    else
    {
        const size_t first_len = std::min(split, kBodyVisiblePerLine);
        if (first_len > 0)
        {
            std::memcpy(body_line_1, body_text + body_start, first_len);
            body_line_1[first_len] = '\0';
        }

        const size_t second_start = body_start + split;
        const size_t second_len = std::min(body_text_len - second_start, kBodyVisiblePerLine);
        if (second_len > 0)
        {
            std::memcpy(body_line_2, body_text + second_start, second_len);
            body_line_2[second_len] = '\0';
        }
    }

    drawTextClipped(0, line_h, display_.width(), body_line_1, false);
    drawTextClipped(0, line_h * 2, display_.width(), body_line_2, false);

    char candidate_line[48] = {};
    size_t pos = 0;
    if (compose_preedit_len_ > 0)
    {
        for (size_t i = 0; i < compose_preedit_len_ && pos + 2 < sizeof(candidate_line); ++i)
        {
            candidate_line[pos++] = static_cast<char>(std::tolower(static_cast<unsigned char>(compose_preedit_[i])));
        }
        candidate_line[pos++] = '_';
        candidate_line[pos] = '\0';
        if (compose_candidate_count_ > 0 && pos + 1 < sizeof(candidate_line))
        {
            candidate_line[pos++] = ' ';
            candidate_line[pos] = '\0';
        }
    }

    if (compose_candidate_count_ == 0)
    {
        if (pos == 0)
        {
            std::snprintf(candidate_line, sizeof(candidate_line), "-");
        }
    }
    else
    {
        for (size_t i = 0; i < compose_candidate_count_ && pos + 4 < sizeof(candidate_line); ++i)
        {
            const bool selected = (i == compose_candidate_index_);
            pos += static_cast<size_t>(std::snprintf(candidate_line + pos, sizeof(candidate_line) - pos,
                                                     selected ? "[%s]" : "%s",
                                                     compose_candidates_[i]));
            if (i + 1 < compose_candidate_count_ && pos + 1 < sizeof(candidate_line))
            {
                candidate_line[pos++] = ' ';
                candidate_line[pos] = '\0';
            }
        }
    }
    drawTextClipped(0, line_h * 3, display_.width(), candidate_line, false);

    if (isAlphaComposeMode(compose_mode_))
    {
        constexpr int kGroupCols = 5;
        constexpr int kGroupX[kGroupCols] = {0, 26, 52, 78, 104};
        constexpr int kGroupCellW[kGroupCols] = {26, 26, 26, 26, 24};
        for (int row = 0; row < 2; ++row)
        {
            for (int col = 0; col < kGroupCols; ++col)
            {
                const size_t group_index = static_cast<size_t>(row * kGroupCols + col);
                if (group_index >= arrayCount(kComposeAbcGroups))
                {
                    continue;
                }

                char cell[8] = {};
                std::snprintf(cell, sizeof(cell), "%s", composeAbcGroupLabel(group_index));
                const bool selected = compose_focus_ == ComposeFocus::Body && compose_abc_group_index_ == group_index;
                drawTextClipped(kGroupX[col], line_h * (5 + row), kGroupCellW[col], cell, selected);
            }
        }
    }
    else
    {
        const char* keyset = composeKeysetForMode(compose_mode_);
        const size_t key_count = std::strlen(keyset);
        const size_t page_start = key_count == 0 ? 0U : ((compose_charset_index_ / 12U) * 12U);
        constexpr int kKeyCols = 6;
        constexpr int kKeyCellW = 20;
        for (int row = 0; row < 2; ++row)
        {
            for (int col = 0; col < kKeyCols; ++col)
            {
                const size_t key_index = page_start + static_cast<size_t>(row * kKeyCols + col);
                if (key_index >= key_count)
                {
                    continue;
                }

                const bool selected = compose_focus_ == ComposeFocus::Body && key_index == compose_charset_index_;
                const char key_char = keyset[key_index];
                char cell[8] = {};
                std::snprintf(cell, sizeof(cell), selected ? "[%c]" : " %c ",
                              key_char == ' ' ? '_' : key_char);
                text_renderer_.drawText(display_, col * kKeyCellW, line_h * (5 + row), cell, false);
            }
        }
    }

    const int action_y = line_h * 7;
    const int action_x[5] = {0, 24, 48, 72, 96};
    const char* action_labels[5] = {composeModeLabel(compose_mode_), "  ", "\xE2\x86\x90", "\xE2\x87\xA4", "SEND"};
    for (size_t i = 0; i < 5; ++i)
    {
        char action_cell[10] = {};
        std::snprintf(action_cell, sizeof(action_cell), "[%s]", action_labels[i]);
        const bool selected = compose_focus_ == ComposeFocus::Action && compose_action_index_ == i;
        text_renderer_.drawText(display_, action_x[i], action_y, action_cell, selected);
    }
}

void Runtime::renderSettingsMenu()
{
    drawMenuList("SETTINGS", kSettingsMenuItems, arrayCount(kSettingsMenuItems), settings_menu_index_);
}

void Runtime::renderRadioSettings()
{
    const auto protocol = app()->getConfig().mesh_protocol;
    const char* const* items = radioItemsFor(protocol);
    const size_t item_count = radioItemCount(protocol);

    drawTitleBar("LORA", protocolShortLabel(protocol));
    char value[40] = {};
    auto& cfg = app()->getConfig();
    for (size_t i = 0; i < item_count; ++i)
    {
        value[0] = '\0';
        switch (i)
        {
        case 0:
            copyText(value, protocolLabel(cfg.mesh_protocol));
            break;
        case 1:
            copyText(value, radioRegionLabel(cfg));
            break;
        case 2:
            std::snprintf(value, sizeof(value), "%ddBm", static_cast<int>(cfg.activeMeshConfig().tx_power));
            break;
        case 3:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                copyText(value,
                         chat::meshtastic::presetDisplayName(
                             static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.meshtastic_config.modem_preset)));
            }
            else
            {
                copyText(value, radioRegionLabel(cfg));
            }
            break;
        case 4:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                formatMeshtasticChannelSlot(value, sizeof(value), cfg.meshtastic_config.channel_num);
            }
            else
            {
                std::snprintf(value, sizeof(value), "%u",
                              static_cast<unsigned>(cfg.meshcore_config.meshcore_channel_slot));
            }
            break;
        case 5:
            copyText(value, encryptEnabled(cfg) ? "On" : "Off");
            break;
        case 6:
            if (cfg.mesh_protocol == chat::MeshProtocol::MeshCore)
            {
                copyText(value, cfg.meshcore_config.meshcore_channel_name);
            }
            break;
        }

        char line[48] = {};
        std::snprintf(line, sizeof(line), "%s: %s", items[i], value);
        drawTextClipped(0, 10 + static_cast<int>(i * text_renderer_.lineHeight()),
                        display_.width(), line, i == radio_index_);
    }
}

void Runtime::renderDeviceSettings()
{
    drawTitleBar("DEVICE", nullptr);
    ble::BlePairingStatus ble_status{};
    const bool has_ble_status = loadBlePairingStatus(app(), &ble_status);
    char line[48] = {};
    for (size_t i = 0; i < arrayCount(kDeviceItems); ++i)
    {
        if (i == 0)
        {
            if (has_ble_status && ble_status.requires_passkey && ble_status.passkey != 0)
            {
                std::snprintf(line, sizeof(line), "BLE: ON %s %06lu",
                              ble_status.is_fixed_pin ? "FIX" : "PIN",
                              static_cast<unsigned long>(ble_status.passkey));
            }
            else if (has_ble_status && ble_status.requires_passkey)
            {
                std::snprintf(line, sizeof(line), "BLE: ON %s",
                              ble_status.is_fixed_pin ? "FIXED" : "RANDOM");
            }
            else
            {
                std::snprintf(line, sizeof(line), "BLE: %s", app()->isBleEnabled() ? "ON" : "OFF");
            }
        }
        else if (i == 1)
        {
            std::snprintf(line, sizeof(line), "GPS: %s", app()->getConfig().gps_mode != 0 ? "ON" : "OFF");
        }
        else if (i == 2)
        {
            std::snprintf(line, sizeof(line), "SATS: %s", gpsSatMaskLabel(app()->getConfig().gps_sat_mask));
        }
        else if (i == 3)
        {
            std::snprintf(line, sizeof(line), "GPS INT: %lus",
                          static_cast<unsigned long>(app()->getConfig().gps_interval_ms / 1000UL));
        }
        else if (i == 4)
        {
            char timeout_label[16] = {};
            formatScreenTimeoutLabel(timeout_label, sizeof(timeout_label), platform::ui::screen::timeout_ms());
            std::snprintf(line, sizeof(line), "SCREEN OFF: %s", timeout_label);
        }
        else if (i == 5)
        {
            const int tz = host_.timezone_offset_min_fn ? host_.timezone_offset_min_fn() : 0;
            char tz_label[16] = {};
            formatTimezoneLabel(tz, tz_label, sizeof(tz_label));
            std::snprintf(line, sizeof(line), "TIME ZONE: %s", tz_label);
        }
        drawTextClipped(0, 10 + static_cast<int>(i * text_renderer_.lineHeight()),
                        display_.width(), line, i == device_index_);
    }
}

void Runtime::renderSettingPopup()
{
    char title[24] = {};
    char value[32] = {};
    const bool is_radio = setting_popup_owner_ == Page::RadioSettings;
    const bool is_device = setting_popup_owner_ == Page::DeviceSettings;
    const bool is_action = setting_popup_owner_ == Page::ActionPage;
    if (!is_radio && !is_device && !is_action)
    {
        return;
    }

    if (is_radio)
    {
        const auto protocol = setting_popup_config_.mesh_protocol;
        const auto* items = (protocol == chat::MeshProtocol::Meshtastic) ? kMeshtasticRadioItems : kMeshCoreRadioItems;
        const size_t count = radioItemCount(protocol);
        if (setting_popup_index_ >= count)
        {
            return;
        }
        std::snprintf(title, sizeof(title), "%s", items[setting_popup_index_]);
    }
    else if (is_device)
    {
        if (setting_popup_index_ >= arrayCount(kDeviceItems))
        {
            return;
        }
        std::snprintf(title, sizeof(title), "%s", kDeviceItems[setting_popup_index_]);
    }
    else
    {
        if (setting_popup_index_ >= arrayCount(kActionItems))
        {
            return;
        }
        std::snprintf(title, sizeof(title), "%s", kActionItems[setting_popup_index_]);
        std::snprintf(value, sizeof(value), "SELECT TO CONFIRM");
    }

    if (!is_action)
    {
        formatSettingPopupValue(value, sizeof(value));
    }

    constexpr int kBoxX = 8;
    constexpr int kBoxY = 14;
    constexpr int kBoxW = 112;
    constexpr int kBoxH = 36;
    display_.fillRect(kBoxX, kBoxY, kBoxW, kBoxH, false);
    drawFrame(display_, kBoxX, kBoxY, kBoxW, kBoxH);
    const int title_w = text_renderer_.measureTextWidth(title);
    text_renderer_.drawText(display_, kBoxX + std::max(2, (kBoxW - title_w) / 2), kBoxY + 3, title);
    const int value_w = text_renderer_.measureTextWidth(value);
    text_renderer_.drawText(display_, kBoxX + std::max(2, (kBoxW - value_w) / 2), kBoxY + 14, value);
    if (is_action)
    {
        drawTextClipped(kBoxX + 2, kBoxY + 25, kBoxW - 4, "SELECT CONFIRM  BACK CANCEL");
    }
    else
    {
        drawTextClipped(kBoxX + 2, kBoxY + 25, kBoxW - 4, "ADJ ARROWS SEL OK BACK ESC");
    }
}

void Runtime::renderTransientPopup()
{
    constexpr int kBoxX = 14;
    constexpr int kBoxY = 18;
    constexpr int kBoxW = 100;
    constexpr int kBoxH = 28;
    display_.fillRect(kBoxX, kBoxY, kBoxW, kBoxH, false);
    drawFrame(display_, kBoxX, kBoxY, kBoxW, kBoxH);
    if (transient_popup_title_[0] != '\0')
    {
        drawTextClipped(kBoxX + 4, kBoxY + 4, kBoxW - 8, transient_popup_title_);
    }
    if (transient_popup_message_[0] != '\0')
    {
        drawTextClipped(kBoxX + 4, kBoxY + 15, kBoxW - 8, transient_popup_message_);
    }
}

void Runtime::renderInfoPage()
{
    char lines[32][40] = {};
    size_t line_count = 0;
    auto push_line = [&](const char* text)
    {
        if (!text || text[0] == '\0' || line_count >= arrayCount(lines))
        {
            return;
        }
        copyText(lines[line_count++], text);
    };
    auto push_kv = [&](const char* key, const char* value)
    {
        if (!key || !value || line_count >= arrayCount(lines))
        {
            return;
        }
        appendInfoLine(lines[line_count++], key, value);
    };

    char long_name[24] = {};
    char short_name[12] = {};
    app()->getEffectiveUserInfo(long_name, sizeof(long_name), short_name, sizeof(short_name));
    const auto& cfg = app()->getConfig();
    const auto battery = host_.battery_info_fn ? host_.battery_info_fn() : platform::ui::device::BatteryInfo{};
    const auto gps = host_.gps_data_fn ? host_.gps_data_fn() : platform::ui::gps::GpsState{};
    const auto ram = host_.ram_usage_fn ? host_.ram_usage_fn() : HostCallbacks::ResourceUsage{};
    const auto flash = host_.flash_usage_fn ? host_.flash_usage_fn() : HostCallbacks::ResourceUsage{};
    ble::BlePairingStatus ble_status{};
    const bool has_ble_status = loadBlePairingStatus(app(), &ble_status);

    push_line("[DEVICE]");
    push_kv("NAME", long_name[0] ? long_name : "-");
    push_kv("SHORT", short_name[0] ? short_name : "-");
    char self_id[16] = {};
    formatNodeLabel(self_id, sizeof(self_id));
    push_kv("NODE", self_id[0] ? self_id : "-");

    char value[40] = {};
    std::snprintf(value, sizeof(value), "%s", protocolLabel(cfg.mesh_protocol));
    push_kv("PROTO", value);
    push_kv("BLE", app()->isBleEnabled() ? "ON" : "OFF");
    if (has_ble_status && ble_status.requires_passkey)
    {
        push_kv("BLE MODE", blePairingModeLabel(ble_status));
        if (ble_status.passkey != 0)
        {
            std::snprintf(value, sizeof(value), "%06lu", static_cast<unsigned long>(ble_status.passkey));
            push_kv("BLE PIN", value);
        }
    }

    if (host_.format_frequency_fn)
    {
        char freq[24] = {};
        host_.format_frequency_fn(host_.active_lora_frequency_hz_fn ? host_.active_lora_frequency_hz_fn() : 0U,
                                  freq,
                                  sizeof(freq));
        push_kv("FREQ", freq[0] ? freq : "-");
    }

    push_line("[RADIO]");
    std::snprintf(value, sizeof(value), "%s", protocolLabel(cfg.mesh_protocol));
    push_kv("PROTO", value);
    push_kv("REGION", radioRegionLabel(cfg));
    std::snprintf(value, sizeof(value), "%ddBm", static_cast<int>(cfg.activeMeshConfig().tx_power));
    push_kv("TX", value);
    if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
    {
        push_kv("MODEM", chat::meshtastic::presetDisplayName(
                             static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.meshtastic_config.modem_preset)));
        formatMeshtasticChannelSlot(value, sizeof(value), cfg.meshtastic_config.channel_num);
        push_kv("CH SLOT", value);
        push_kv("ENCRYPT", encryptEnabled(cfg) ? "ON" : "OFF");
    }
    else
    {
        push_kv("PRESET", radioRegionLabel(cfg));
        push_kv("NAME", cfg.meshcore_config.meshcore_channel_name);
        std::snprintf(value, sizeof(value), "%u",
                      static_cast<unsigned>(cfg.meshcore_config.meshcore_channel_slot));
        push_kv("SLOT", value);
        push_kv("ENCRYPT", encryptEnabled(cfg) ? "ON" : "OFF");
    }

    push_line("[SYSTEM]");
    push_kv("BLE", app()->isBleEnabled() ? "ON" : "OFF");
    if (has_ble_status && ble_status.requires_passkey)
    {
        push_kv("BLE MODE", blePairingModeLabel(ble_status));
        if (ble_status.passkey != 0)
        {
            std::snprintf(value, sizeof(value), "%06lu", static_cast<unsigned long>(ble_status.passkey));
            push_kv("BLE PIN", value);
        }
    }
    if (battery.available && battery.level >= 0)
    {
        std::snprintf(value, sizeof(value), "%d%%%s", battery.level, battery.charging ? "+" : "");
        push_kv("BAT", value);
    }
    else
    {
        push_kv("BAT", "-");
    }
    char tz_label[16] = {};
    formatTimezoneLabel(host_.timezone_offset_min_fn ? host_.timezone_offset_min_fn() : 0, tz_label, sizeof(tz_label));
    push_kv("TZ", tz_label[0] ? tz_label : "UTC+0");

    char time_buf[16] = {};
    char date_buf[24] = {};
    formatTime(time_buf, sizeof(time_buf), date_buf, sizeof(date_buf));
    push_kv("TIME", time_buf[0] ? time_buf : "-");
    push_kv("DATE", date_buf[0] ? date_buf : "-");
    formatUptimeLabel(host_.millis_fn ? (host_.millis_fn() / 1000U) : 0U, value, sizeof(value));
    push_kv("UPTIME", value);
    if (ram.available && ram.total_bytes > 0)
    {
        std::snprintf(value, sizeof(value), "%lu/%luK",
                      static_cast<unsigned long>(ram.used_bytes / 1024U),
                      static_cast<unsigned long>(ram.total_bytes / 1024U));
        push_kv("SRAM", value);
    }
    if (flash.available && flash.total_bytes > 0)
    {
        const uint32_t flash_free_bytes =
            flash.total_bytes > flash.used_bytes ? (flash.total_bytes - flash.used_bytes) : 0U;
        std::snprintf(value, sizeof(value), "%lu/%luK free",
                      static_cast<unsigned long>(flash_free_bytes / 1024U),
                      static_cast<unsigned long>(flash.total_bytes / 1024U));
        push_kv("FLASH", value);
    }

    push_line("[GPS]");
    push_kv("GPS", cfg.gps_mode != 0 ? "ON" : "OFF");
    push_kv("SATS", gpsSatMaskLabel(cfg.gps_sat_mask));
    std::snprintf(value, sizeof(value), "%lus", static_cast<unsigned long>(cfg.gps_interval_ms / 1000UL));
    push_kv("INT", value);
    push_kv("PWR", (host_.gps_powered_fn && host_.gps_powered_fn()) ? "ON" : "OFF");
    if (gps.valid)
    {
        std::snprintf(value, sizeof(value), "%.5f", gps.lat);
        push_kv("LAT", value);
        std::snprintf(value, sizeof(value), "%.5f", gps.lng);
        push_kv("LON", value);
        std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(gps.satellites));
        push_kv("SATS", value);
        if (gps.has_alt)
        {
            std::snprintf(value, sizeof(value), "%.0fm", gps.alt_m);
            push_kv("ALT", value);
        }
        if (gps.has_speed)
        {
            std::snprintf(value, sizeof(value), "%.1fkmh", gps.speed_mps * 3.6);
            push_kv("SPD", value);
        }
        if (gps.has_course)
        {
            std::snprintf(value, sizeof(value), "%.0fdeg", gps.course_deg);
            push_kv("CRS", value);
        }
        std::snprintf(value, sizeof(value), "%lus", static_cast<unsigned long>(gps.age / 1000UL));
        push_kv("AGE", value);
    }
    else
    {
        push_kv("FIX", "SEARCH");
    }

    const size_t max_scroll = line_count > kInfoPageSize ? (line_count - kInfoPageSize) : 0U;
    if (info_scroll_ > max_scroll)
    {
        info_scroll_ = max_scroll;
    }

    const size_t total_pages = std::max<size_t>(1U, (line_count + kInfoPageSize - 1U) / kInfoPageSize);
    const size_t current_page = (line_count > 0) ? ((info_scroll_ / kInfoPageSize) + 1U) : 1U;
    char pos[16] = {};
    std::snprintf(pos, sizeof(pos), "%u/%u",
                  static_cast<unsigned>(current_page),
                  static_cast<unsigned>(total_pages));
    drawTitleBar("INFO", pos);

    const int line_h = text_renderer_.lineHeight();
    const int start_y = 10;
    const size_t visible = std::min(line_count - info_scroll_, kInfoPageSize);
    for (size_t i = 0; i < visible; ++i)
    {
        drawTextClipped(0,
                        start_y + static_cast<int>(i * line_h),
                        display_.width(),
                        lines[info_scroll_ + i]);
    }
}

void Runtime::renderGnssPage()
{
    refreshGnssSnapshot();
    const auto& state = gnss_snapshot_state_;
    const auto& status = gnss_snapshot_status_;
    const std::size_t sat_count = gnss_snapshot_count_;
    const auto* sats = gnss_snapshot_sats_.data();
    char summary_lines[14][40] = {};
    size_t summary_count = 0;

    pushFormattedLine(summary_lines, summary_count, "USE:%u/%u",
                      static_cast<unsigned>(status.sats_in_use),
                      static_cast<unsigned>(status.sats_in_view > 0 ? status.sats_in_view : sat_count));
    pushFormattedLine(summary_lines, summary_count, "FIX:%s", gnssFixLabel(status.fix));
    pushFormattedLine(summary_lines, summary_count, "HDOP:%.1f", static_cast<double>(status.hdop));
    if ((host_.gps_enabled_fn && host_.gps_enabled_fn()) && (host_.gps_powered_fn && host_.gps_powered_fn()))
    {
        if (sat_count == 0)
        {
            pushFormattedLine(summary_lines, summary_count, "STATE:TIME ONLY");
        }
        else if (!state.valid)
        {
            pushFormattedLine(summary_lines, summary_count, "STATE:SEARCH FIX");
        }
        else
        {
            pushFormattedLine(summary_lines, summary_count, "STATE:LOCKED");
        }
    }
    else
    {
        pushFormattedLine(summary_lines, summary_count, "STATE:GPS OFF");
    }
    pushFormattedLine(summary_lines, summary_count, "EN:%s", (host_.gps_enabled_fn && host_.gps_enabled_fn()) ? "YES" : "NO");
    pushFormattedLine(summary_lines, summary_count, "PWR:%s", (host_.gps_powered_fn && host_.gps_powered_fn()) ? "YES" : "NO");
    pushFormattedLine(summary_lines, summary_count, "AGE:%lums", static_cast<unsigned long>(state.age));

    if (state.valid)
    {
        pushFormattedLine(summary_lines, summary_count, "LAT:%.5f", state.lat);
        pushFormattedLine(summary_lines, summary_count, "LON:%.5f", state.lng);
    }
    else
    {
        pushFormattedLine(summary_lines, summary_count, "LAT:-");
        pushFormattedLine(summary_lines, summary_count, "LON:-");
    }

    if (state.has_alt)
    {
        pushFormattedLine(summary_lines, summary_count, "ALT:%.0fm", state.alt_m);
    }
    else
    {
        pushFormattedLine(summary_lines, summary_count, "ALT:-");
    }

    if (state.has_speed)
    {
        pushFormattedLine(summary_lines, summary_count, "SPD:%.1fkmh", state.speed_mps * 3.6);
    }
    else
    {
        pushFormattedLine(summary_lines, summary_count, "SPD:-");
    }

    if (state.has_course)
    {
        pushFormattedLine(summary_lines, summary_count, "CRS:%.0f %s", state.course_deg, bearingCardinal(state.course_deg));
    }
    else
    {
        pushFormattedLine(summary_lines, summary_count, "CRS:-");
    }

    const uint32_t last_motion_ms = platform::ui::gps::last_motion_ms();
    if (last_motion_ms > 0)
    {
        const uint32_t age_s = nowMs() >= last_motion_ms ? (nowMs() - last_motion_ms) / 1000U : 0U;
        pushFormattedLine(summary_lines, summary_count, "MOVE:%lus", static_cast<unsigned long>(age_s));
    }
    else
    {
        pushFormattedLine(summary_lines, summary_count, "MOVE:-");
    }

    const size_t summary_pages = std::max<size_t>(1U, (summary_count + kGnssSummaryPageSize - 1U) / kGnssSummaryPageSize);
    const size_t sat_pages = std::max<size_t>(1U, (sat_count + kGnssSatPageSize - 1U) / kGnssSatPageSize);
    const size_t total_pages = summary_pages + sat_pages;
    if (gnss_page_index_ >= total_pages)
    {
        gnss_page_index_ = total_pages - 1U;
    }

    char pos[16] = {};
    std::snprintf(pos, sizeof(pos), "%u/%u",
                  static_cast<unsigned>(gnss_page_index_ + 1U),
                  static_cast<unsigned>(total_pages));
    drawTitleBar("GPS", pos);

    const int line_h = text_renderer_.lineHeight();

    if (gnss_page_index_ < summary_pages)
    {
        const size_t start = gnss_page_index_ * kGnssSummaryPageSize;
        const size_t visible = std::min(kGnssSummaryPageSize, summary_count - std::min(start, summary_count));
        for (size_t i = 0; i < visible; ++i)
        {
            text_renderer_.drawText(display_, 0, 10 + static_cast<int>(i * line_h), summary_lines[start + i]);
        }
        return;
    }

    constexpr int kSatHeaderY = 10;
    constexpr int kSatRowY = 20;
    constexpr int kSysX = 0;
    constexpr int kSysW = 16;
    constexpr int kIdX = 18;
    constexpr int kIdW = 14;
    constexpr int kUseX = 34;
    constexpr int kUseW = 18;
    constexpr int kSnrX = 54;
    constexpr int kSnrW = 22;
    constexpr int kElvX = 78;
    constexpr int kElvW = 20;
    constexpr int kAziX = 100;
    constexpr int kAziW = 28;

    display_.fillRect(0, kSatHeaderY, display_.width(), line_h, true);
    drawTextClipped(kSysX, kSatHeaderY, kSysW, "SAT", true);
    drawTextClipped(kIdX, kSatHeaderY, kIdW, "ID", true);
    drawTextClipped(kUseX, kSatHeaderY, kUseW, "USE", true);
    drawTextClipped(kSnrX, kSatHeaderY, kSnrW, "SNR", true);
    drawTextClipped(kElvX, kSatHeaderY, kElvW, "ELV", true);
    drawTextClipped(kAziX, kSatHeaderY, kAziW, "AZI", true);
    const size_t sat_page = gnss_page_index_ - summary_pages;
    const size_t sat_start = sat_page * kGnssSatPageSize;
    const size_t sat_visible = std::min(kGnssSatPageSize, sat_count - std::min(sat_start, sat_count));
    if (sat_visible == 0)
    {
        text_renderer_.drawText(display_, 0, 22, "NO SATELLITE DATA");
        return;
    }

    for (size_t i = 0; i < sat_visible; ++i)
    {
        const auto& sat = sats[sat_start + i];
        const int row_y = kSatRowY + static_cast<int>(i * line_h);
        char cell[12] = {};

        std::snprintf(cell, sizeof(cell), "%s", gnssSystemLabel(sat.sys));
        drawTextClipped(kSysX, row_y, kSysW, cell);

        std::snprintf(cell, sizeof(cell), "%02u", static_cast<unsigned>(sat.id));
        drawTextClipped(kIdX, row_y, kIdW, cell);

        std::snprintf(cell, sizeof(cell), "%c", sat.used ? 'Y' : '-');
        drawTextClipped(kUseX, row_y, kUseW, cell);

        std::snprintf(cell, sizeof(cell), "%03d", static_cast<int>(sat.snr >= 0 ? sat.snr : 0));
        drawTextClipped(kSnrX, row_y, kSnrW, cell);

        std::snprintf(cell, sizeof(cell), "%02u", static_cast<unsigned>(sat.elevation));
        drawTextClipped(kElvX, row_y, kElvW, cell);

        std::snprintf(cell, sizeof(cell), "%03u", static_cast<unsigned>(sat.azimuth));
        drawTextClipped(kAziX, row_y, kAziW, cell);
    }
}

void Runtime::renderActionPage()
{
    drawMenuList("ACTIONS", kActionItems, arrayCount(kActionItems), action_index_);
}

void Runtime::enterPage(Page page)
{
    page_ = page;
    page_entered_ms_ = nowMs();
    if (page == Page::Screensaver || page == Page::GnssPage)
    {
        refreshGnssSnapshot(true);
    }
    if (page == Page::ChatList)
    {
        rebuildConversationList();
        chat_list_index_ = std::min(chat_list_index_, conversation_count_ == 0 ? 0U : conversation_count_ - 1U);
    }
    else if (page == Page::NodeList)
    {
        rebuildNodeList();
        node_list_index_ = std::min(node_list_index_, node_count_ == 0 ? 0U : node_count_ - 1U);
    }
    else if (page == Page::NodeInfo)
    {
        node_info_scroll_ = 0;
        buildNodeInfo();
    }
    else if (page == Page::NodeCompass)
    {
        node_action_index_ = std::min(node_action_index_, nodeActionCount() - 1U);
    }
    else if (page == Page::Conversation)
    {
        if (app())
        {
            app()->getChatService().markConversationRead(active_conversation_);
        }
        rebuildMessages();
        message_focus_started_ms_ = nowMs();
        message_menu_index_ = 0;
        message_info_scroll_ = 0;
    }
    else if (page == Page::MessageMenu)
    {
        message_menu_index_ = 0;
    }
    else if (page == Page::MessageInfo)
    {
        message_info_scroll_ = 0;
        buildMessageInfo();
    }
    else if (page == Page::InfoPage)
    {
        info_scroll_ = 0;
    }
    else if (page == Page::GnssPage)
    {
        gnss_page_index_ = 0;
    }
}

void Runtime::openCompose(EditTarget target, const char* seed_text)
{
    edit_target_ = target;
    page_before_compose_ = page_;
    compose_buffer_[0] = '\0';
    compose_len_ = 0;
    compose_charset_index_ = 0;
    compose_mode_ = ComposeMode::AbcLower;
    compose_focus_ = ComposeFocus::Body;
    compose_action_index_ = 0;
    compose_abc_group_index_ = 0;
    compose_abc_tap_index_ = 0;
    compose_abc_last_tap_ms_ = 0;
    compose_abc_last_group_index_ = -1;
    compose_preedit_[0] = '\0';
    compose_preedit_len_ = 0;
    compose_candidate_count_ = 0;
    compose_candidate_index_ = 0;
    for (size_t i = 0; i < kComposeCandidateMax; ++i)
    {
        compose_candidates_[i][0] = '\0';
    }
    if (seed_text)
    {
        copyText(compose_buffer_, seed_text);
        compose_len_ = std::strlen(compose_buffer_);
    }
    rebuildComposeCandidates();
    page_ = Page::Compose;
    page_entered_ms_ = nowMs();
}

void Runtime::finishTextEdit(bool accept)
{
    if (accept)
    {
        saveEditedTextToConfig();
    }
    edit_target_ = EditTarget::None;
    enterPage(page_before_compose_);
}

void Runtime::rebuildConversationList()
{
    conversation_count_ = 0;
    conversation_total_ = 0;
    if (!app())
    {
        return;
    }

    size_t total = 0;
    const auto list = app()->getChatService().getConversations(0, kMaxConversationItems, &total);
    conversation_total_ = total;
    conversation_count_ = std::min(list.size(), static_cast<size_t>(kMaxConversationItems));
    for (size_t i = 0; i < conversation_count_; ++i)
    {
        conversations_[i] = list[i];
    }
}

void Runtime::rebuildNodeList()
{
    node_count_ = 0;
    if (!app())
    {
        return;
    }

    auto contacts = app()->getContactService().getContacts();
    auto nearby = app()->getContactService().getNearby();
    auto ignored = app()->getContactService().getIgnoredNodes();
    contacts.insert(contacts.end(), nearby.begin(), nearby.end());
    contacts.insert(contacts.end(), ignored.begin(), ignored.end());
    std::sort(contacts.begin(), contacts.end(),
              [](const chat::contacts::NodeInfo& a, const chat::contacts::NodeInfo& b)
              {
                  if (a.last_seen != b.last_seen)
                  {
                      return a.last_seen > b.last_seen;
                  }
                  return a.node_id < b.node_id;
              });

    node_count_ = std::min(contacts.size(), static_cast<size_t>(kMaxNodeItems));
    for (size_t i = 0; i < node_count_; ++i)
    {
        nodes_[i] = contacts[i];
    }

    if (node_count_ == 0)
    {
        node_list_index_ = 0;
    }
    else if (node_list_index_ >= node_count_)
    {
        node_list_index_ = node_count_ - 1U;
    }
}

void Runtime::buildNodeInfo()
{
    node_info_count_ = 0;
    if (node_count_ == 0)
    {
        return;
    }

    const size_t index = std::min(node_list_index_, node_count_ - 1U);
    const auto& node = nodes_[index];

    auto push_line = [this](const char* text)
    {
        if (!text || text[0] == '\0' || node_info_count_ >= kNodeInfoLines)
        {
            return;
        }
        copyText(node_info_lines_[node_info_count_], text);
        ++node_info_count_;
    };

    auto push_kv = [this](const char* key, const char* value)
    {
        if (!key || !value || node_info_count_ >= kNodeInfoLines)
        {
            return;
        }
        appendInfoLine(node_info_lines_[node_info_count_], key, value);
        ++node_info_count_;
    };

    auto push_section = [this](const char* title)
    {
        if (!title || node_info_count_ >= kNodeInfoLines)
        {
            return;
        }
        setInfoSection(node_info_lines_[node_info_count_], title);
        ++node_info_count_;
    };

    char value[40] = {};
    push_section("NODE");
    std::snprintf(value, sizeof(value), "%08lX", static_cast<unsigned long>(node.node_id));
    push_kv("ID", value);
    push_kv("NM", node.display_name.empty() ? "-" : node.display_name.c_str());
    push_kv("SH", node.short_name[0] != '\0' ? node.short_name : "-");
    push_kv("LN", node.long_name[0] != '\0' ? node.long_name : "-");

    push_section("LINK");
    push_kv("P", node.protocol == chat::contacts::NodeProtocolType::MeshCore ? "MC" : node.protocol == chat::contacts::NodeProtocolType::Meshtastic ? "MT"
                                                                                                                                                    : "?");
    std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(node.hops_away));
    push_kv("HP", node.hops_away == 0xFF ? "-" : value);
    std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(node.channel));
    push_kv("CH", node.channel == 0xFF ? "-" : value);
    std::snprintf(value, sizeof(value), "%.1f", static_cast<double>(node.snr));
    push_kv("SN", value);
    std::snprintf(value, sizeof(value), "%.1f", static_cast<double>(node.rssi));
    push_kv("RS", value);
    formatTimestamp(value, sizeof(value), node.last_seen);
    push_kv("SEEN", value[0] != '\0' ? value : "-");
    formatElapsedShort(host_.utc_now_fn ? host_.utc_now_fn() : 0, node.last_seen, value, sizeof(value));
    push_kv("AGO", value);
    push_kv("SIG", signalRatingLabel(node.snr, node.rssi));
    std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(node.hw_model));
    push_kv("HW", node.hw_model == 0 ? "-" : value);
    std::snprintf(value, sizeof(value), "%02X", static_cast<unsigned>(node.next_hop));
    push_kv("NH", node.next_hop == 0 ? "-" : value);
    push_kv("MQTT", node.via_mqtt ? "Y" : "N");
    push_kv("IGN", node.is_ignored ? "Y" : "N");
    if (node.has_macaddr)
    {
        char mac_buf[24];
        std::snprintf(mac_buf,
                      sizeof(mac_buf),
                      "%02X:%02X:%02X:%02X:%02X:%02X",
                      static_cast<unsigned>(node.macaddr[0]),
                      static_cast<unsigned>(node.macaddr[1]),
                      static_cast<unsigned>(node.macaddr[2]),
                      static_cast<unsigned>(node.macaddr[3]),
                      static_cast<unsigned>(node.macaddr[4]),
                      static_cast<unsigned>(node.macaddr[5]));
        push_kv("MAC", mac_buf);
    }
    push_kv("PKI", node.key_manually_verified ? "VERIFIED" : (node.has_public_key ? "KNOWN" : "-"));
    if (node.has_device_metrics)
    {
        if (node.device_metrics.has_battery_level)
        {
            std::snprintf(value, sizeof(value), "%lu%%",
                          static_cast<unsigned long>(node.device_metrics.battery_level));
            push_kv("BAT", value);
        }
        if (node.device_metrics.has_voltage)
        {
            std::snprintf(value, sizeof(value), "%.2fV", static_cast<double>(node.device_metrics.voltage));
            push_kv("V", value);
        }
        if (node.device_metrics.has_channel_utilization)
        {
            std::snprintf(value, sizeof(value), "%.1f%%",
                          static_cast<double>(node.device_metrics.channel_utilization));
            push_kv("UTIL", value);
        }
        if (node.device_metrics.has_air_util_tx)
        {
            std::snprintf(value, sizeof(value), "%.1f%%",
                          static_cast<double>(node.device_metrics.air_util_tx));
            push_kv("AIR", value);
        }
        if (node.device_metrics.has_uptime_seconds)
        {
            std::snprintf(value, sizeof(value), "%lu",
                          static_cast<unsigned long>(node.device_metrics.uptime_seconds));
            push_kv("UP", value);
        }
    }

    push_section("POS");
    if (node.position.valid)
    {
        std::snprintf(value, sizeof(value), "%.5f", static_cast<double>(node.position.latitude_i) / 1e7);
        push_kv("LAT", value);
        std::snprintf(value, sizeof(value), "%.5f", static_cast<double>(node.position.longitude_i) / 1e7);
        push_kv("LON", value);
        if (node.position.has_altitude)
        {
            std::snprintf(value, sizeof(value), "%ldm", static_cast<long>(node.position.altitude));
            push_kv("ALT", value);
        }
        formatTimestamp(value, sizeof(value), node.position.timestamp);
        push_kv("TIME", value[0] != '\0' ? value : "-");
    }
    else
    {
        push_line("NO POSITION");
    }

    const auto gps = host_.gps_data_fn ? host_.gps_data_fn() : platform::ui::gps::GpsState{};
    if (gps.valid && node.position.valid)
    {
        const double node_lat = static_cast<double>(node.position.latitude_i) / 1e7;
        const double node_lon = static_cast<double>(node.position.longitude_i) / 1e7;
        const double dist_m = haversineMeters(gps.lat, gps.lng, node_lat, node_lon);
        const double brg_deg = bearingDegrees(gps.lat, gps.lng, node_lat, node_lon);

        push_section("NAV");
        if (dist_m < 1000.0)
        {
            std::snprintf(value, sizeof(value), "%.0fm", dist_m);
        }
        else
        {
            std::snprintf(value, sizeof(value), "%.2fkm", dist_m / 1000.0);
        }
        push_kv("DST", value);

        std::snprintf(value, sizeof(value), "%s %.0f", bearingCardinal(brg_deg), brg_deg);
        push_kv("BRG", value);

        if (gps.has_course)
        {
            const double rel = normalizeBearingDeg(brg_deg - gps.course_deg);
            std::snprintf(value, sizeof(value), "%.0f", rel);
            push_kv("REL", value);
        }
    }
}

void Runtime::rebuildMessages()
{
    message_count_ = 0;
    if (!app())
    {
        return;
    }

    const auto list = app()->getChatService().getRecentMessages(active_conversation_, kMaxMessageItems);
    message_count_ = std::min(list.size(), static_cast<size_t>(kMaxMessageItems));
    for (size_t i = 0; i < message_count_; ++i)
    {
        messages_[i] = list[message_count_ - 1U - i];
    }
    if (message_count_ == 0)
    {
        message_index_ = 0;
    }
    else if (message_index_ >= message_count_)
    {
        message_index_ = message_count_ - 1U;
    }
}

void Runtime::buildMessageInfo()
{
    message_info_count_ = 0;
    const chat::ChatMessage* msg = selectedMessage();
    if (!msg)
    {
        return;
    }

    auto push_line = [this](const char* text)
    {
        if (!text || text[0] == '\0' || message_info_count_ >= kMessageInfoLines)
        {
            return;
        }
        copyText(message_info_lines_[message_info_count_], text);
        ++message_info_count_;
    };

    auto push_kv = [this, &push_line](const char* key, const char* value)
    {
        if (message_info_count_ >= kMessageInfoLines)
        {
            return;
        }
        appendInfoLine(message_info_lines_[message_info_count_], key, value ? value : "-");
        ++message_info_count_;
    };

    char value[40] = {};
    auto push_section = [this](const char* title)
    {
        if (!title || message_info_count_ >= kMessageInfoLines)
        {
            return;
        }
        setInfoSection(message_info_lines_[message_info_count_], title);
        ++message_info_count_;
    };

    auto fitUtf8Bytes = [](const char* text, size_t max_bytes)
    {
        if (!text || max_bytes == 0)
        {
            return static_cast<size_t>(0);
        }

        size_t used = 0;
        while (text[used] != '\0')
        {
            const size_t char_len = utf8CharLength(static_cast<unsigned char>(text[used]));
            if (used + char_len > max_bytes)
            {
                break;
            }
            used += char_len;
        }
        return used;
    };

    auto push_text_block = [this, &push_line, &fitUtf8Bytes](const char* value)
    {
        if (!value || message_info_count_ >= kMessageInfoLines)
        {
            return;
        }

        const int available_width = std::max(1, display_.width());
        const size_t storage_limit = std::max<size_t>(1U, kMessageInfoWidth - 1U);

        const char* cursor = value;
        while (message_info_count_ < kMessageInfoLines)
        {
            const char* logical_end = cursor;
            while (*logical_end != '\0' && *logical_end != '\n' && *logical_end != '\r')
            {
                ++logical_end;
            }

            const size_t logical_len = static_cast<size_t>(logical_end - cursor);
            if (logical_len == 0)
            {
                push_line(" ");
            }
            else
            {
                std::string logical_line(cursor, logical_len);
                const char* wrapped = logical_line.c_str();
                while (*wrapped != '\0' && message_info_count_ < kMessageInfoLines)
                {
                    size_t keep_bytes = text_renderer_.clipTextToWidth(wrapped, available_width);
                    if (keep_bytes == 0)
                    {
                        keep_bytes = utf8CharLength(static_cast<unsigned char>(*wrapped));
                    }
                    keep_bytes = std::min(keep_bytes, fitUtf8Bytes(wrapped, storage_limit));
                    if (keep_bytes == 0)
                    {
                        keep_bytes = std::min(storage_limit, static_cast<size_t>(1));
                    }

                    char chunk[kMessageInfoWidth] = {};
                    std::memcpy(chunk, wrapped, std::min(keep_bytes, sizeof(chunk) - 1));
                    chunk[std::min(keep_bytes, sizeof(chunk) - 1)] = '\0';
                    push_line(chunk);
                    wrapped += keep_bytes;
                }
            }

            if (*logical_end == '\0')
            {
                break;
            }

            if (*logical_end == '\r' && logical_end[1] == '\n')
            {
                cursor = logical_end + 2;
            }
            else
            {
                cursor = logical_end + 1;
            }
        }
    };

    auto formatInfoTime = [this](uint32_t timestamp_s, char* out, size_t out_len)
    {
        if (!out || out_len == 0)
        {
            return;
        }
        out[0] = '\0';
        if (timestamp_s == 0)
        {
            return;
        }

        const int tz_offset_s = (host_.timezone_offset_min_fn ? host_.timezone_offset_min_fn() : 0) * 60;
        const time_t adjusted = static_cast<time_t>(timestamp_s + tz_offset_s);
        const std::tm* tm = std::gmtime(&adjusted);
        if (!tm)
        {
            std::snprintf(out, out_len, "%lu", static_cast<unsigned long>(timestamp_s));
            return;
        }

        std::snprintf(out, out_len, "%02d-%02d %02d:%02d",
                      tm->tm_mon + 1,
                      tm->tm_mday,
                      tm->tm_hour,
                      tm->tm_min);
    };

    std::snprintf(value, sizeof(value), "%s", msg->protocol == chat::MeshProtocol::MeshCore ? "MC" : "MT");
    push_section("MSG");
    push_kv("P", value);

    std::snprintf(value, sizeof(value), "%s", msg->channel == chat::ChannelId::SECONDARY ? "SECONDARY" : "PRIMARY");
    push_kv("CH", value);

    if (msg->from == 0)
    {
        push_kv("FR", "SELF");
    }
    else
    {
        std::snprintf(value, sizeof(value), "%08lX", static_cast<unsigned long>(msg->from));
        push_kv("FR", value);
    }

    std::snprintf(value, sizeof(value), "%08lX", static_cast<unsigned long>(msg->peer));
    push_kv("PEER", value);

    std::snprintf(value, sizeof(value), "%08lX", static_cast<unsigned long>(msg->msg_id));
    push_kv("ID", value);

    formatInfoTime(msg->timestamp, value, sizeof(value));
    push_kv("TIME", value[0] != '\0' ? value : "-");

    std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(msg->text.size()));
    push_kv("LEN", value);

    if (!msg->text.empty())
    {
        push_section("TXT");
        push_text_block(msg->text.c_str());
    }

    const MessageMetaEntry* meta = nullptr;
    for (size_t i = 0; i < kMessageMetaCapacity; ++i)
    {
        const MessageMetaEntry& candidate = message_meta_[i];
        if (!candidate.used)
        {
            continue;
        }
        if (candidate.protocol == msg->protocol &&
            candidate.channel == msg->channel &&
            candidate.from == msg->from &&
            candidate.msg_id == msg->msg_id)
        {
            meta = &candidate;
            break;
        }
    }

    if (meta)
    {
        push_section("RX");
        std::snprintf(value, sizeof(value), "%08lX", static_cast<unsigned long>(meta->to));
        push_kv("TO", value);

        std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(meta->hop_limit));
        push_kv("HOP", value);

        push_kv("ENC", meta->encrypted ? "YES" : "NO");

        std::snprintf(value, sizeof(value), "%d.%01d",
                      meta->rx_meta.rssi_dbm_x10 / 10,
                      std::abs(meta->rx_meta.rssi_dbm_x10 % 10));
        push_kv("RS", value);

        std::snprintf(value, sizeof(value), "%d.%01d",
                      meta->rx_meta.snr_db_x10 / 10,
                      std::abs(meta->rx_meta.snr_db_x10 % 10));
        push_kv("SN", value);

        std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(meta->rx_meta.hop_count));
        push_kv("HP", value);

        std::snprintf(value, sizeof(value), "%02X", static_cast<unsigned>(meta->rx_meta.channel_hash));
        push_kv("HS", value);

        std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(meta->rx_meta.relay_node));
        push_kv("RY", value);
    }

    if (msg->from != 0 && app())
    {
        if (const auto* node = app()->getContactService().getNodeInfo(msg->from))
        {
            push_section("NODE");
            push_kv("NM", node->display_name.empty() ? "-" : node->display_name.c_str());
            push_kv("SH", node->short_name[0] != '\0' ? node->short_name : "-");
            push_kv("LN", node->long_name[0] != '\0' ? node->long_name : "-");

            formatInfoTime(node->last_seen, value, sizeof(value));
            push_kv("SEEN", value[0] != '\0' ? value : "-");

            std::snprintf(value, sizeof(value), "%.1f", static_cast<double>(node->snr));
            push_kv("SN", value);

            std::snprintf(value, sizeof(value), "%.1f", static_cast<double>(node->rssi));
            push_kv("RS", value);

            std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(node->hops_away));
            push_kv("HP", value);

            std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(node->channel));
            push_kv("CH", value);
        }
    }
}

void Runtime::sendComposeMessage()
{
    if (usesSmartCompose())
    {
        (void)commitComposePreedit(true);
    }

    if (!app() || compose_len_ == 0)
    {
        finishTextEdit(false);
        return;
    }

    app()->getChatService().sendText(active_conversation_.channel, compose_buffer_, active_conversation_.peer);
    finishTextEdit(false);
    enterPage(Page::Conversation);
}

void Runtime::commitConfig()
{
    if (!app())
    {
        return;
    }
    app()->saveConfig();
}

void Runtime::ensureBootExit()
{
    if (page_ == Page::BootLog && (nowMs() - boot_started_ms_) >= kBootMinMs)
    {
        enterPage(Page::Screensaver);
    }
}

void Runtime::ensureSleepTimeout(InputAction action)
{
    if (page_ == Page::BootLog || page_ == Page::Sleep)
    {
        return;
    }

    const uint32_t now = nowMs();
    if (action != InputAction::None)
    {
        return;
    }

    if (platform::ui::screen::is_sleep_disabled())
    {
        return;
    }

    const uint32_t timeout_ms = platform::ui::screen::timeout_ms();
    if (timeout_ms >= kScreenTimeoutAlwaysMs)
    {
        return;
    }

    if ((now - last_interaction_ms_) < timeout_ms)
    {
        return;
    }

    page_before_sleep_ = page_;
    enterPage(Page::Sleep);
}

void Runtime::adjustRadioSetting(int delta)
{
    if (!app())
    {
        return;
    }

    auto& cfg = app()->getConfig();
    switch (radio_index_)
    {
    case 0:
        app()->switchMeshProtocol(cfg.mesh_protocol == chat::MeshProtocol::Meshtastic
                                      ? chat::MeshProtocol::MeshCore
                                      : chat::MeshProtocol::Meshtastic,
                                  false);
        appendStatus(this, "proto %s", protocolShortLabel(app()->getConfig().mesh_protocol));
        break;
    case 1:
        if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
        {
            size_t count = 0;
            const auto* table = chat::meshtastic::getRegionTable(&count);
            if (count > 0)
            {
                size_t index = 0;
                for (size_t i = 0; i < count; ++i)
                {
                    if (table[i].code ==
                        static_cast<meshtastic_Config_LoRaConfig_RegionCode>(cfg.meshtastic_config.region))
                    {
                        index = i;
                        break;
                    }
                }
                index = static_cast<size_t>(clampValue<int>(static_cast<int>(index) + delta, 0, static_cast<int>(count) - 1));
                cfg.meshtastic_config.region = static_cast<uint8_t>(table[index].code);
            }
        }
        else
        {
            size_t count = 0;
            const auto* table = chat::meshcore::getRegionPresetTable(&count);
            if (count > 0)
            {
                int index = -1;
                for (size_t i = 0; i < count; ++i)
                {
                    if (table[i].id == cfg.meshcore_config.meshcore_region_preset)
                    {
                        index = static_cast<int>(i);
                        break;
                    }
                }
                index = clampValue(index + delta, 0, static_cast<int>(count) - 1);
                cfg.meshcore_config.meshcore_region_preset = table[index].id;
            }
        }
        appendStatus(this, "region %s", radioRegionLabel(cfg));
        break;
    case 2:
        cfg.activeMeshConfig().tx_power = static_cast<int8_t>(clampValue<int>(
            static_cast<int>(cfg.activeMeshConfig().tx_power) + delta,
            static_cast<int>(app::AppConfig::kTxPowerMinDbm),
            static_cast<int>(app::AppConfig::kTxPowerMaxDbm)));
        appendStatus(this, "tx %ddBm", static_cast<int>(cfg.activeMeshConfig().tx_power));
        break;
    case 3:
        if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
        {
            constexpr int kPresetMin = static_cast<int>(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
            constexpr int kPresetMax = static_cast<int>(meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO);
            cfg.meshtastic_config.modem_preset = static_cast<uint8_t>(clampValue(
                static_cast<int>(cfg.meshtastic_config.modem_preset) + delta, kPresetMin, kPresetMax));
            cfg.meshtastic_config.use_preset = true;
        }
        else
        {
            size_t count = 0;
            const auto* table = chat::meshcore::getRegionPresetTable(&count);
            if (count > 0)
            {
                int index = -1;
                for (size_t i = 0; i < count; ++i)
                {
                    if (table[i].id == cfg.meshcore_config.meshcore_region_preset)
                    {
                        index = static_cast<int>(i);
                        break;
                    }
                }
                index = clampValue(index + delta, 0, static_cast<int>(count) - 1);
                cfg.meshcore_config.meshcore_region_preset = table[index].id;
                cfg.meshcore_config.meshcore_freq_mhz = table[index].freq_mhz;
                cfg.meshcore_config.meshcore_bw_khz = table[index].bw_khz;
                cfg.meshcore_config.meshcore_sf = table[index].sf;
                cfg.meshcore_config.meshcore_cr = table[index].cr;
            }
        }
        if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
        {
            appendStatus(this, "modem %s",
                         chat::meshtastic::presetDisplayName(
                             static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.meshtastic_config.modem_preset)));
        }
        else
        {
            appendStatus(this, "preset %s", radioRegionLabel(cfg));
        }
        break;
    case 4:
        if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
        {
            const int current = static_cast<int>(normalizedMeshtasticChannelNum(cfg.meshtastic_config.channel_num));
            cfg.meshtastic_config.channel_num = static_cast<uint16_t>(clampValue<int>(
                current + delta, 0, static_cast<int>(kMeshtasticChannelNumMax)));
            if (cfg.meshtastic_config.channel_num == 0)
            {
                appendStatus(this, "channel auto");
            }
            else
            {
                appendStatus(this, "channel slot %u", static_cast<unsigned>(cfg.meshtastic_config.channel_num));
            }
        }
        else
        {
            cfg.meshcore_config.meshcore_channel_slot = static_cast<uint8_t>(clampValue<int>(
                static_cast<int>(cfg.meshcore_config.meshcore_channel_slot) + delta, 0, 15));
            appendStatus(this, "slot %u", static_cast<unsigned>(cfg.meshcore_config.meshcore_channel_slot));
        }
        break;
    case 5:
        setEncryptEnabled(cfg, !encryptEnabled(cfg));
        appendStatus(this, "encrypt %s", encryptEnabled(cfg) ? "on" : "off");
        break;
    default:
        break;
    }

    commitConfig();
}

void Runtime::adjustDeviceSetting(int delta)
{
    if (!app())
    {
        return;
    }

    if (device_index_ == 0)
    {
        app()->setBleEnabled(!app()->isBleEnabled());
        appendStatus(this, "ble %s", app()->isBleEnabled() ? "on" : "off");
    }
    else if (device_index_ == 1)
    {
        app()->getConfig().gps_mode = (app()->getConfig().gps_mode == 0) ? 1 : 0;
        commitConfig();
        appendStatus(this, "gps %s", app()->getConfig().gps_mode != 0 ? "on" : "off");
    }
    else if (device_index_ == 2)
    {
        app()->getConfig().gps_sat_mask = nextGpsSatMask(app()->getConfig().gps_sat_mask, delta);
        commitConfig();
        appendStatus(this, "sats %s", gpsSatMaskLabel(app()->getConfig().gps_sat_mask));
    }
    else if (device_index_ == 3)
    {
        static constexpr uint32_t kGpsIntervals[] = {15000UL, 30000UL, 60000UL, 300000UL, 600000UL};
        size_t index = 0;
        while (index + 1 < arrayCount(kGpsIntervals) &&
               kGpsIntervals[index] < app()->getConfig().gps_interval_ms)
        {
            ++index;
        }
        const int next = clampValue<int>(static_cast<int>(index) + delta, 0, static_cast<int>(arrayCount(kGpsIntervals)) - 1);
        app()->getConfig().gps_interval_ms = kGpsIntervals[next];
        commitConfig();
        appendStatus(this, "gps int %lus", static_cast<unsigned long>(app()->getConfig().gps_interval_ms / 1000UL));
    }
    else if (device_index_ == 4)
    {
        const uint32_t next = stepScreenTimeoutMs(platform::ui::screen::timeout_ms(), delta);
        platform::ui::screen::set_timeout_ms(next);
        char timeout_label[16] = {};
        formatScreenTimeoutLabel(timeout_label, sizeof(timeout_label), next);
        appendStatus(this, "screen %s", timeout_label);
    }
    else if (device_index_ == 5 && host_.timezone_offset_min_fn && host_.set_timezone_offset_min_fn)
    {
        const int current = host_.timezone_offset_min_fn();
        const int next = clampValue(current + delta * kTimezoneStep, kTimezoneMin, kTimezoneMax);
        host_.set_timezone_offset_min_fn(next);
        char tz_label[16] = {};
        formatTimezoneLabel(next, tz_label, sizeof(tz_label));
        appendStatus(this, "tz %s", tz_label);
    }
}

void Runtime::beginSettingPopup(Page owner, size_t index)
{
    if (!app())
    {
        return;
    }
    setting_popup_active_ = true;
    setting_popup_owner_ = owner;
    setting_popup_index_ = index;
    setting_popup_config_ = app()->getConfig();
    setting_popup_ble_enabled_ = app()->isBleEnabled();
    setting_popup_screen_timeout_ms_ = platform::ui::screen::timeout_ms();
    setting_popup_timezone_min_ = host_.timezone_offset_min_fn ? host_.timezone_offset_min_fn() : 0;
}

void Runtime::cancelSettingPopup()
{
    setting_popup_active_ = false;
}

void Runtime::confirmSettingPopup()
{
    if (!setting_popup_active_ || !app())
    {
        return;
    }

    if (setting_popup_owner_ == Page::ActionPage)
    {
        const size_t action = setting_popup_index_;
        setting_popup_active_ = false;
        executeActionPageItem(action);
        return;
    }

    auto& cfg = app()->getConfig();
    sanitizeMeshtasticChannelNum(setting_popup_config_);
    cfg = setting_popup_config_;
    cfg.ble_enabled = setting_popup_ble_enabled_;
    if (host_.set_timezone_offset_min_fn)
    {
        host_.set_timezone_offset_min_fn(setting_popup_timezone_min_);
    }
    platform::ui::screen::set_timeout_ms(setting_popup_screen_timeout_ms_);
    app()->setBleEnabled(setting_popup_ble_enabled_);
    app()->saveConfig();

    char value[32] = {};
    formatSettingPopupValue(value, sizeof(value));
    appendStatus(this, "%s", value);
    setting_popup_active_ = false;
}

void Runtime::adjustSettingPopup(int delta)
{
    if (!setting_popup_active_ || !app() || delta == 0)
    {
        return;
    }

    if (setting_popup_owner_ == Page::ActionPage)
    {
        return;
    }

    if (setting_popup_owner_ == Page::RadioSettings)
    {
        auto& cfg = setting_popup_config_;
        switch (setting_popup_index_)
        {
        case 0:
            cfg.mesh_protocol = (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
                                    ? chat::MeshProtocol::MeshCore
                                    : chat::MeshProtocol::Meshtastic;
            break;
        case 1:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                size_t count = 0;
                const auto* table = chat::meshtastic::getRegionTable(&count);
                if (count > 0)
                {
                    size_t index = 0;
                    for (size_t i = 0; i < count; ++i)
                    {
                        if (table[i].code ==
                            static_cast<meshtastic_Config_LoRaConfig_RegionCode>(cfg.meshtastic_config.region))
                        {
                            index = i;
                            break;
                        }
                    }
                    index = static_cast<size_t>(
                        clampValue<int>(static_cast<int>(index) + delta, 0, static_cast<int>(count) - 1));
                    cfg.meshtastic_config.region = static_cast<uint8_t>(table[index].code);
                }
            }
            else
            {
                size_t count = 0;
                const auto* table = chat::meshcore::getRegionPresetTable(&count);
                if (count > 0)
                {
                    int index = -1;
                    for (size_t i = 0; i < count; ++i)
                    {
                        if (table[i].id == cfg.meshcore_config.meshcore_region_preset)
                        {
                            index = static_cast<int>(i);
                            break;
                        }
                    }
                    index = clampValue(index + delta, 0, static_cast<int>(count) - 1);
                    cfg.meshcore_config.meshcore_region_preset = table[index].id;
                }
            }
            break;
        case 2:
            cfg.activeMeshConfig().tx_power = static_cast<int8_t>(clampValue<int>(
                static_cast<int>(cfg.activeMeshConfig().tx_power) + delta,
                static_cast<int>(app::AppConfig::kTxPowerMinDbm),
                static_cast<int>(app::AppConfig::kTxPowerMaxDbm)));
            break;
        case 3:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                constexpr int kPresetMin = static_cast<int>(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
                constexpr int kPresetMax = static_cast<int>(meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO);
                cfg.meshtastic_config.modem_preset = static_cast<uint8_t>(clampValue(
                    static_cast<int>(cfg.meshtastic_config.modem_preset) + delta, kPresetMin, kPresetMax));
                cfg.meshtastic_config.use_preset = true;
            }
            else
            {
                size_t count = 0;
                const auto* table = chat::meshcore::getRegionPresetTable(&count);
                if (count > 0)
                {
                    int index = -1;
                    for (size_t i = 0; i < count; ++i)
                    {
                        if (table[i].id == cfg.meshcore_config.meshcore_region_preset)
                        {
                            index = static_cast<int>(i);
                            break;
                        }
                    }
                    index = clampValue(index + delta, 0, static_cast<int>(count) - 1);
                    cfg.meshcore_config.meshcore_region_preset = table[index].id;
                    cfg.meshcore_config.meshcore_freq_mhz = table[index].freq_mhz;
                    cfg.meshcore_config.meshcore_bw_khz = table[index].bw_khz;
                    cfg.meshcore_config.meshcore_sf = table[index].sf;
                    cfg.meshcore_config.meshcore_cr = table[index].cr;
                }
            }
            break;
        case 4:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                const int current = static_cast<int>(normalizedMeshtasticChannelNum(cfg.meshtastic_config.channel_num));
                cfg.meshtastic_config.channel_num = static_cast<uint16_t>(clampValue<int>(
                    current + delta, 0, static_cast<int>(kMeshtasticChannelNumMax)));
            }
            else
            {
                cfg.meshcore_config.meshcore_channel_slot = static_cast<uint8_t>(clampValue<int>(
                    static_cast<int>(cfg.meshcore_config.meshcore_channel_slot) + delta, 0, 15));
            }
            break;
        case 5:
            setEncryptEnabled(cfg, !encryptEnabled(cfg));
            break;
        default:
            break;
        }
        return;
    }

    if (setting_popup_owner_ == Page::DeviceSettings)
    {
        switch (setting_popup_index_)
        {
        case 0:
            setting_popup_ble_enabled_ = !setting_popup_ble_enabled_;
            break;
        case 1:
            setting_popup_config_.gps_mode = (setting_popup_config_.gps_mode == 0) ? 1 : 0;
            break;
        case 2:
            setting_popup_config_.gps_sat_mask = nextGpsSatMask(setting_popup_config_.gps_sat_mask, delta);
            break;
        case 3:
        {
            static constexpr uint32_t kGpsIntervals[] = {15000UL, 30000UL, 60000UL, 300000UL, 600000UL};
            size_t index = 0;
            while (index + 1 < arrayCount(kGpsIntervals) &&
                   kGpsIntervals[index] < setting_popup_config_.gps_interval_ms)
            {
                ++index;
            }
            const int next = clampValue<int>(static_cast<int>(index) + delta, 0, static_cast<int>(arrayCount(kGpsIntervals)) - 1);
            setting_popup_config_.gps_interval_ms = kGpsIntervals[next];
            break;
        }
        case 4:
            setting_popup_screen_timeout_ms_ = stepScreenTimeoutMs(setting_popup_screen_timeout_ms_, delta);
            break;
        case 5:
            setting_popup_timezone_min_ = clampValue(setting_popup_timezone_min_ + delta * kTimezoneStep,
                                                     kTimezoneMin,
                                                     kTimezoneMax);
            break;
        default:
            break;
        }
    }
}

void Runtime::formatSettingPopupValue(char* out, size_t out_len) const
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';

    if (setting_popup_owner_ == Page::RadioSettings)
    {
        const auto& cfg = setting_popup_config_;
        switch (setting_popup_index_)
        {
        case 0:
            std::snprintf(out, out_len, "%s", protocolShortLabel(cfg.mesh_protocol));
            return;
        case 1:
            std::snprintf(out, out_len, "%s", radioRegionLabel(cfg));
            return;
        case 2:
            std::snprintf(out, out_len, "%ddBm", static_cast<int>(cfg.activeMeshConfig().tx_power));
            return;
        case 3:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                std::snprintf(out, out_len, "%s",
                              chat::meshtastic::presetDisplayName(
                                  static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.meshtastic_config.modem_preset)));
            }
            else
            {
                std::snprintf(out, out_len, "%s", radioRegionLabel(cfg));
            }
            return;
        case 4:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                formatMeshtasticChannelSlot(out, out_len, cfg.meshtastic_config.channel_num);
            }
            else
            {
                std::snprintf(out, out_len, "SLOT %u", static_cast<unsigned>(cfg.meshcore_config.meshcore_channel_slot));
            }
            return;
        case 5:
            std::snprintf(out, out_len, "%s", encryptEnabled(cfg) ? "ON" : "OFF");
            return;
        default:
            break;
        }
    }

    if (setting_popup_owner_ == Page::DeviceSettings)
    {
        switch (setting_popup_index_)
        {
        case 0:
            std::snprintf(out, out_len, "%s", setting_popup_ble_enabled_ ? "ON" : "OFF");
            return;
        case 1:
            std::snprintf(out, out_len, "%s", setting_popup_config_.gps_mode != 0 ? "ON" : "OFF");
            return;
        case 2:
            std::snprintf(out, out_len, "%s", gpsSatMaskLabel(setting_popup_config_.gps_sat_mask));
            return;
        case 3:
            std::snprintf(out, out_len, "%lus", static_cast<unsigned long>(setting_popup_config_.gps_interval_ms / 1000UL));
            return;
        case 4:
            formatScreenTimeoutLabel(out, out_len, setting_popup_screen_timeout_ms_);
            return;
        case 5:
        {
            char tz_label[16] = {};
            formatTimezoneLabel(setting_popup_timezone_min_, tz_label, sizeof(tz_label));
            std::snprintf(out, out_len, "%s", tz_label);
            return;
        }
        default:
            break;
        }
    }
}

void Runtime::adjustComposeSelection(int delta)
{
    if (usesSmartCompose())
    {
        const char* charset = composeKeysetForMode(compose_mode_);
        const size_t len = std::strlen(charset);
        if (len == 0)
        {
            compose_charset_index_ = 0;
            return;
        }
        const int next = static_cast<int>(compose_charset_index_) + delta;
        if (next < 0)
        {
            compose_charset_index_ = len - 1;
        }
        else
        {
            compose_charset_index_ = static_cast<size_t>(next) % len;
        }
        return;
    }

    const char* charset = editUsesHexCharset() ? kHexCharset : kComposeCharset;
    const size_t len = std::strlen(charset);
    if (len == 0)
    {
        compose_charset_index_ = 0;
        return;
    }
    const int next = static_cast<int>(compose_charset_index_) + delta;
    if (next < 0)
    {
        compose_charset_index_ = len - 1;
    }
    else
    {
        compose_charset_index_ = static_cast<size_t>(next) % len;
    }
}

void Runtime::adjustComposeCandidate(int delta)
{
    if (compose_candidate_count_ == 0)
    {
        compose_candidate_index_ = 0;
        return;
    }
    const int next = static_cast<int>(compose_candidate_index_) + delta;
    if (next < 0)
    {
        compose_candidate_index_ = compose_candidate_count_ - 1;
    }
    else
    {
        compose_candidate_index_ = static_cast<size_t>(next) % compose_candidate_count_;
    }
}

void Runtime::adjustComposeAction(int delta)
{
    constexpr size_t kActionCount = 5;
    const int next = static_cast<int>(compose_action_index_) + delta;
    if (next < 0)
    {
        compose_action_index_ = kActionCount - 1;
    }
    else
    {
        compose_action_index_ = static_cast<size_t>(next) % kActionCount;
    }
}

void Runtime::addComposeChar()
{
    if (usesSmartCompose())
    {
        const char* keyset = composeKeysetForMode(compose_mode_);
        const size_t len = std::strlen(keyset);
        if (len == 0)
        {
            return;
        }
        appendComposeChar(keyset[compose_charset_index_ % len]);
        return;
    }

    const char* charset = editUsesHexCharset() ? kHexCharset : kComposeCharset;
    const size_t len = std::strlen(charset);
    if (len == 0)
    {
        return;
    }
    appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, charset[compose_charset_index_ % len]);
}

void Runtime::removeComposeChar()
{
    if (usesSmartCompose())
    {
        compose_abc_last_group_index_ = -1;
        compose_abc_last_tap_ms_ = 0;
        compose_abc_tap_index_ = 0;
        if (compose_preedit_len_ > 0)
        {
            popChar(compose_preedit_, compose_preedit_len_);
            rebuildComposeCandidates();
        }
        else
        {
            popChar(compose_buffer_, compose_len_);
        }
        return;
    }

    popChar(compose_buffer_, compose_len_);
}

void Runtime::cycleComposeMode()
{
    if (compose_mode_ == ComposeMode::AbcLower)
    {
        compose_mode_ = ComposeMode::AbcUpper;
    }
    else if (compose_mode_ == ComposeMode::AbcUpper)
    {
        compose_mode_ = ComposeMode::Num;
    }
    else if (compose_mode_ == ComposeMode::Num)
    {
        compose_mode_ = ComposeMode::Sym;
    }
    else
    {
        compose_mode_ = ComposeMode::AbcLower;
    }
    compose_charset_index_ = 0;
    compose_abc_tap_index_ = 0;
    compose_abc_last_tap_ms_ = 0;
    compose_abc_last_group_index_ = -1;
    rebuildComposeCandidates();
}

void Runtime::rebuildComposeCandidates()
{
    compose_candidate_count_ = 0;
    compose_candidate_index_ = 0;
    for (size_t i = 0; i < kComposeCandidateMax; ++i)
    {
        compose_candidates_[i][0] = '\0';
    }

    if (!usesSmartCompose() || !isAlphaComposeMode(compose_mode_) || compose_preedit_len_ == 0)
    {
        return;
    }

    for (const char* term : kHamTerms)
    {
        if (!hasPrefixIgnoreCase(term, compose_preedit_))
        {
            continue;
        }
        copyText(compose_candidates_[compose_candidate_count_], term);
        ++compose_candidate_count_;
        if (compose_candidate_count_ >= kComposeCandidateMax)
        {
            break;
        }
    }
}

bool Runtime::commitComposeCandidate()
{
    if (compose_candidate_count_ == 0 || compose_candidate_index_ >= compose_candidate_count_)
    {
        return false;
    }
    appendComposeWord(compose_candidates_[compose_candidate_index_]);
    compose_preedit_[0] = '\0';
    compose_preedit_len_ = 0;
    compose_abc_last_group_index_ = -1;
    compose_abc_last_tap_ms_ = 0;
    compose_abc_tap_index_ = 0;
    rebuildComposeCandidates();
    return true;
}

bool Runtime::commitComposePreedit(bool prefer_candidate)
{
    if (compose_preedit_len_ == 0)
    {
        return false;
    }
    if (prefer_candidate && commitComposeCandidate())
    {
        return true;
    }
    appendComposeWord(compose_preedit_);
    compose_preedit_[0] = '\0';
    compose_preedit_len_ = 0;
    compose_abc_last_group_index_ = -1;
    compose_abc_last_tap_ms_ = 0;
    compose_abc_tap_index_ = 0;
    rebuildComposeCandidates();
    return true;
}

void Runtime::appendComposeChar(char ch)
{
    if (!usesSmartCompose())
    {
        appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, ch);
        return;
    }

    if (isAlphaComposeMode(compose_mode_) && isAlphaAscii(ch))
    {
        appendChar(compose_preedit_, sizeof(compose_preedit_), compose_preedit_len_, upperAscii(ch));
        rebuildComposeCandidates();
        return;
    }

    if (ch == ' ')
    {
        (void)commitComposePreedit(true);
        compose_abc_last_group_index_ = -1;
        compose_abc_last_tap_ms_ = 0;
        compose_abc_tap_index_ = 0;
        if (compose_len_ > 0 && compose_buffer_[compose_len_ - 1] != ' ')
        {
            appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, ' ');
        }
        return;
    }

    (void)commitComposePreedit(true);
    compose_abc_last_group_index_ = -1;
    compose_abc_last_tap_ms_ = 0;
    compose_abc_tap_index_ = 0;
    appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, ch);
}

void Runtime::appendComposeWord(const char* text)
{
    if (!text || text[0] == '\0')
    {
        return;
    }

    if (compose_len_ > 0 && compose_buffer_[compose_len_ - 1] != ' ')
    {
        appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, ' ');
    }

    for (size_t i = 0; text[i] != '\0'; ++i)
    {
        appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, applyComposeAlphaCase(compose_mode_, text[i]));
    }
}

void Runtime::activateComposeAction()
{
    switch (static_cast<ComposeAction>(compose_action_index_))
    {
    case ComposeAction::Mode:
        cycleComposeMode();
        compose_focus_ = ComposeFocus::Action;
        break;
    case ComposeAction::Space:
        appendComposeChar(' ');
        compose_focus_ = ComposeFocus::Body;
        break;
    case ComposeAction::Back:
        finishTextEdit(false);
        break;
    case ComposeAction::Delete:
        removeComposeChar();
        compose_focus_ = ComposeFocus::Body;
        break;
    case ComposeAction::Send:
        sendComposeMessage();
        break;
    }
}

void Runtime::saveEditedTextToConfig()
{
    if (!app())
    {
        return;
    }

    auto& cfg = app()->getConfig();
    switch (edit_target_)
    {
    case EditTarget::MeshCoreChannelName:
        copyText(cfg.meshcore_config.meshcore_channel_name, compose_buffer_);
        break;
    default:
        break;
    }
    commitConfig();
}

void Runtime::formatTime(char* out_time, size_t out_len, char* out_date, size_t date_len) const
{
    if (out_time && out_len > 0)
    {
        out_time[0] = '\0';
    }
    if (out_date && date_len > 0)
    {
        out_date[0] = '\0';
    }

    if (!host_.utc_now_fn && !host_.millis_fn)
    {
        return;
    }

    time_t now = host_.utc_now_fn ? host_.utc_now_fn() : 0;
    const bool has_valid_wall_clock = now >= static_cast<time_t>(1700000000);

    if (has_valid_wall_clock && host_.timezone_offset_min_fn)
    {
        now += static_cast<time_t>(host_.timezone_offset_min_fn()) * 60;
    }

    if (!has_valid_wall_clock)
    {
        const uint32_t uptime_s = host_.millis_fn ? (host_.millis_fn() / 1000U) : 0U;
        const uint32_t hours = uptime_s / 3600U;
        const uint32_t minutes = (uptime_s / 60U) % 60U;
        const uint32_t seconds = uptime_s % 60U;

        if (out_time && out_len > 0)
        {
            std::snprintf(out_time, out_len, "%02lu:%02lu:%02lu",
                          static_cast<unsigned long>(hours),
                          static_cast<unsigned long>(minutes),
                          static_cast<unsigned long>(seconds));
        }
        if (out_date && date_len > 0)
        {
            std::snprintf(out_date, date_len, "TIME UNSYNC");
        }
        return;
    }

    const tm* local = gmtime(&now);
    if (!local)
    {
        return;
    }

    if (out_time && out_len > 0)
    {
        std::snprintf(out_time, out_len, "%02d:%02d:%02d", local->tm_hour, local->tm_min, local->tm_sec);
    }
    if (out_date && date_len > 0)
    {
        const char* weekday = (local->tm_wday >= 0 && local->tm_wday < 7) ? kWeekdays[local->tm_wday] : "---";
        std::snprintf(out_date, date_len, "%04d-%02d-%02d %s",
                      local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, weekday);
    }
}

void Runtime::formatTimestamp(char* out, size_t out_len, uint32_t timestamp_s) const
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (timestamp_s == 0)
    {
        return;
    }

    const int tz_offset_s = (host_.timezone_offset_min_fn ? host_.timezone_offset_min_fn() : 0) * 60;
    const time_t adjusted = static_cast<time_t>(timestamp_s + tz_offset_s);
    const std::tm* tm = std::gmtime(&adjusted);
    if (!tm)
    {
        std::snprintf(out, out_len, "%lu", static_cast<unsigned long>(timestamp_s));
        return;
    }

    std::snprintf(out, out_len, "%02d-%02d %02d:%02d",
                  tm->tm_mon + 1,
                  tm->tm_mday,
                  tm->tm_hour,
                  tm->tm_min);
}

void Runtime::formatConversationTime(char* out, size_t out_len, uint32_t timestamp_s, bool expanded) const
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (timestamp_s == 0)
    {
        std::snprintf(out, out_len, "%s", "--:--");
        return;
    }

    if (timestamp_s >= 1700000000U)
    {
        const int tz_offset_s = (host_.timezone_offset_min_fn ? host_.timezone_offset_min_fn() : 0) * 60;
        const time_t adjusted = static_cast<time_t>(timestamp_s + tz_offset_s);
        const std::tm* tm = std::gmtime(&adjusted);
        if (tm)
        {
            if (expanded)
            {
                std::snprintf(out, out_len, "%02d/%02d %02d:%02d",
                              tm->tm_mon + 1,
                              tm->tm_mday,
                              tm->tm_hour,
                              tm->tm_min);
            }
            else
            {
                std::snprintf(out, out_len, "%02d/%02d", tm->tm_mon + 1, tm->tm_mday);
            }
            return;
        }
    }

    const unsigned long hours = static_cast<unsigned long>((timestamp_s / 3600U) % 24U);
    const unsigned long minutes = static_cast<unsigned long>((timestamp_s / 60U) % 60U);
    if (expanded)
    {
        std::snprintf(out, out_len, "--/-- %02lu:%02lu", hours, minutes);
    }
    else
    {
        std::snprintf(out, out_len, "%02lu:%02lu", hours, minutes);
    }
}

void Runtime::formatConversationSender(char* out, size_t out_len, const chat::ChatMessage& msg, bool expanded) const
{
    if (!out || out_len == 0)
    {
        return;
    }

    if (msg.from == 0)
    {
        const chat::NodeId self_id = app() ? app()->getSelfNodeId() : 0;
        if (expanded && self_id != 0)
        {
            std::snprintf(out, out_len, "%08lX", static_cast<unsigned long>(self_id));
        }
        else
        {
            std::snprintf(out, out_len, "%s", "ME");
        }
        return;
    }

    if (expanded)
    {
        std::snprintf(out, out_len, "%08lX", static_cast<unsigned long>(msg.from));
    }
    else
    {
        std::snprintf(out, out_len, "%04lX", static_cast<unsigned long>(msg.from & 0xFFFFUL));
    }
}

void Runtime::formatProtocol(char* out, size_t out_len) const
{
    if (!out || out_len == 0 || !app())
    {
        return;
    }
    std::snprintf(out, out_len, "%s", protocolShortLabel(app()->getConfig().mesh_protocol));
}

void Runtime::formatNodeLabel(char* out, size_t out_len) const
{
    if (!out || out_len == 0 || !app())
    {
        return;
    }
    chat::runtime::formatScreenNodeLabel(app()->getSelfNodeId(), out, out_len);
}

void Runtime::formatComposeTarget(char* out, size_t out_len) const
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';

    if (active_conversation_.peer == 0)
    {
        std::snprintf(out, out_len, "BCST");
        return;
    }

    std::snprintf(out, out_len, "%04lX", static_cast<unsigned long>(active_conversation_.peer & 0xFFFFUL));
}

void Runtime::drawTitleBar(const char* left, const char* right)
{
    if (left && left[0] != '\0')
    {
        text_renderer_.drawText(display_, 0, 0, left);
    }
    if (right && right[0] != '\0')
    {
        const int w = text_renderer_.measureTextWidth(right);
        text_renderer_.drawText(display_, std::max(0, display_.width() - w), 0, right);
    }
    display_.drawHLine(0, 8, display_.width());
}

void Runtime::drawMenuList(const char* title, const char* const* items, size_t count, size_t selected)
{
    drawTitleBar(title, nullptr);
    const int line_h = text_renderer_.lineHeight();
    const size_t visible = std::min(count, static_cast<size_t>(6));
    size_t start = 0;
    if (count > visible)
    {
        start = (selected + 1 > visible) ? (selected + 1 - visible) : 0;
        if (start + visible > count)
        {
            start = count - visible;
        }
    }

    for (size_t i = 0; i < visible && (start + i) < count; ++i)
    {
        const size_t item_index = start + i;
        drawTextClipped(0, 10 + static_cast<int>(i * line_h), display_.width(), items[item_index], item_index == selected);
    }
}

void Runtime::drawFooterHint(const char* hint)
{
    if (!hint)
    {
        return;
    }
    drawTextClipped(0, 56, display_.width(), hint);
}

void Runtime::drawTextClipped(int x, int y, int w, const char* text, bool inverse)
{
    if (!text || w <= 0)
    {
        return;
    }

    char clipped[48] = {};
    if (text_renderer_.measureTextWidth(text) <= w)
    {
        copyText(clipped, text);
    }
    else if (w > text_renderer_.ellipsisWidth())
    {
        const size_t keep_bytes = text_renderer_.clipTextToWidth(text, w - text_renderer_.ellipsisWidth());
        std::memcpy(clipped, text, std::min(keep_bytes, sizeof(clipped) - 4));
        clipped[std::min(keep_bytes, sizeof(clipped) - 4)] = '\0';
        std::strcat(clipped, "...");
    }
    else
    {
        const size_t keep_bytes = text_renderer_.clipTextToWidth(text, w);
        std::memcpy(clipped, text, std::min(keep_bytes, sizeof(clipped) - 1));
        clipped[std::min(keep_bytes, sizeof(clipped) - 1)] = '\0';
    }
    text_renderer_.drawText(display_, x, y, clipped, inverse);
}

void Runtime::drawConversationText(int x, int y, int w, const char* text, bool selected, bool align_right)
{
    if (!text || w <= 0)
    {
        return;
    }

    const int full_width = text_renderer_.measureTextWidth(text);

    if (full_width <= w)
    {
        const int draw_x = align_right ? (x + std::max(0, w - full_width)) : x;
        text_renderer_.drawText(display_, draw_x, y, text, false);
        return;
    }

    char clipped[96] = {};
    if (!selected)
    {
        if (w > text_renderer_.ellipsisWidth())
        {
            const size_t keep_bytes = text_renderer_.clipTextToWidth(text, w - text_renderer_.ellipsisWidth());
            std::memcpy(clipped, text, std::min(keep_bytes, sizeof(clipped) - 4));
            clipped[std::min(keep_bytes, sizeof(clipped) - 4)] = '\0';
            std::strcat(clipped, "...");
        }
        else
        {
            const size_t keep_bytes = text_renderer_.clipTextToWidth(text, w);
            std::memcpy(clipped, text, std::min(keep_bytes, sizeof(clipped) - 1));
            clipped[std::min(keep_bytes, sizeof(clipped) - 1)] = '\0';
        }
        const int clipped_width = text_renderer_.measureTextWidth(clipped);
        const int draw_x = align_right ? (x + std::max(0, w - clipped_width)) : x;
        text_renderer_.drawText(display_, draw_x, y, clipped, false);
        return;
    }

    const size_t text_len = std::strlen(text);
    std::vector<size_t> offsets;
    offsets.reserve(text_len + 1);
    for (size_t off = 0; off < text_len;)
    {
        offsets.push_back(off);
        off += utf8CharLength(static_cast<unsigned char>(text[off]));
    }
    offsets.push_back(text_len);

    size_t max_scroll_index = 0;
    for (size_t i = 0; i + 1 < offsets.size(); ++i)
    {
        if (text_renderer_.measureTextWidth(text + offsets[i]) > w)
        {
            max_scroll_index = i + 1;
        }
    }

    size_t scroll_index = 0;
    const uint32_t elapsed_ms = nowMs() >= message_focus_started_ms_ ? (nowMs() - message_focus_started_ms_) : 0U;
    if (max_scroll_index > 0 && elapsed_ms > kConversationScrollStartPauseMs)
    {
        const uint32_t rolling_ms = static_cast<uint32_t>(max_scroll_index) * kConversationScrollStepMs;
        const uint32_t cycle_ms = rolling_ms + kConversationScrollEndPauseMs;
        const uint32_t within_cycle = (elapsed_ms - kConversationScrollStartPauseMs) % std::max<uint32_t>(1U, cycle_ms);
        if (within_cycle < rolling_ms)
        {
            scroll_index = std::min<size_t>(max_scroll_index, within_cycle / kConversationScrollStepMs);
        }
        else
        {
            scroll_index = max_scroll_index;
        }
    }

    const char* window_text = text + offsets[scroll_index];
    const size_t keep_bytes = text_renderer_.clipTextToWidth(window_text, w);
    std::memcpy(clipped, window_text, std::min(keep_bytes, sizeof(clipped) - 1));
    clipped[std::min(keep_bytes, sizeof(clipped) - 1)] = '\0';
    const int clipped_width = text_renderer_.measureTextWidth(clipped);
    const int draw_x = align_right ? (x + std::max(0, w - clipped_width)) : x;
    text_renderer_.drawText(display_, draw_x, y, clipped, false);
}

void Runtime::drawConversationBubble(int x, int y, int w, const char* sender, const char* time_text,
                                     const char* text, bool selected, bool align_right)
{
    if (!text || w <= 4)
    {
        return;
    }

    const int line_h = text_renderer_.lineHeight();
    constexpr int kBubbleBodyBottomPadding = 2;
    const int bubble_h = (line_h * 2) + 1 + kBubbleBodyBottomPadding;
    drawFrame(display_, x, y, w, bubble_h);

    const int inner_x = x + 1;
    const int inner_w = w - 2;
    const int divider_y = y + line_h;
    const int band_h = std::max(1, line_h - 1);
    display_.fillRect(inner_x, divider_y, inner_w, 1, true);

    display_.fillRect(inner_x, y + 1, inner_w, band_h, true);

    const bool header_inverse = true;
    const int header_pad = 1;
    const int header_x = inner_x + header_pad;
    const int header_w = std::max(0, inner_w - (header_pad * 2));
    const int time_w = (time_text && time_text[0] != '\0') ? text_renderer_.measureTextWidth(time_text) : 0;
    const int time_x = x + w - 2 - header_pad - time_w;
    const int sender_w = std::max(0, time_x - header_x - 2);

    if (sender && sender[0] != '\0' && sender_w > 0)
    {
        drawTextClipped(header_x, y, sender_w, sender, header_inverse);
    }
    if (time_text && time_text[0] != '\0' && header_w > 0)
    {
        text_renderer_.drawText(display_, std::max(header_x, time_x), y, time_text, header_inverse);
    }

    const int body_x = inner_x + 1;
    const int body_w = std::max(0, inner_w - 2);
    drawConversationText(body_x, y + line_h + 1, body_w, text, selected, align_right);
}

void Runtime::executeActionPageItem(size_t index)
{
    if (!app())
    {
        return;
    }

    switch (index)
    {
    case 0:
        app()->broadcastNodeInfo();
        appendBootLog("nodeinfo tx");
        break;
    case 1:
        app()->clearNodeDb();
        appendBootLog("nodes cleared");
        break;
    case 2:
        app()->clearMessageDb();
        appendBootLog("messages cleared");
        break;
    case 3:
        if (auto* ble_app = static_cast<app::IAppBleFacade*>(app()))
        {
            ble_app->resetMeshConfig();
            appendBootLog("radio reset");
        }
        break;
    default:
        break;
    }
}

void Runtime::showTransientPopup(const char* title, const char* message, uint32_t duration_ms)
{
    copyText(transient_popup_title_, title ? title : "");
    copyText(transient_popup_message_, message ? message : "");
    transient_popup_active_ = true;
    transient_popup_expires_ms_ = nowMs() + duration_ms;
}

void Runtime::expireTransientPopup()
{
    if (!transient_popup_active_)
    {
        return;
    }
    const uint32_t now = nowMs();
    if (now >= transient_popup_expires_ms_)
    {
        transient_popup_active_ = false;
        transient_popup_title_[0] = '\0';
        transient_popup_message_[0] = '\0';
        transient_popup_expires_ms_ = 0;
    }
}

bool Runtime::editUsesHexCharset() const
{
    return false;
}

bool Runtime::usesSmartCompose() const
{
    return edit_target_ == EditTarget::Message;
}

uint32_t Runtime::nowMs() const
{
    return host_.millis_fn ? host_.millis_fn() : 0U;
}

app::IAppFacade* Runtime::app() const
{
    return host_.app;
}

const chat::ChatMessage* Runtime::selectedMessage() const
{
    if (message_count_ == 0)
    {
        return nullptr;
    }
    const size_t index = std::min(message_index_, message_count_ - 1U);
    return &messages_[index];
}

const chat::contacts::NodeInfo* Runtime::selectedNode() const
{
    if (node_count_ == 0)
    {
        return nullptr;
    }
    const size_t index = std::min(node_list_index_, node_count_ - 1U);
    return &nodes_[index];
}

size_t Runtime::nodeActionCount() const
{
    const bool meshtastic_mode = app() && app()->getConfig().mesh_protocol != chat::MeshProtocol::MeshCore;
    return meshtastic_mode ? kNodeActionItemCount : (kNodeActionItemCount - 1U);
}

const char* Runtime::nodeActionLabel(size_t index) const
{
    const chat::contacts::NodeInfo* node = selectedNode();
    const bool meshtastic_mode = app() && app()->getConfig().mesh_protocol != chat::MeshProtocol::MeshCore;
    switch (index)
    {
    case 0:
        return "DETAIL";
    case 1:
        return "REPLY";
    case 2:
        return "ADD CONTACT";
    case 3:
        return (node && node->is_ignored) ? "UNIGNORE NODE" : "IGNORE NODE";
    case 4:
        return "TRACE ROUTE";
    case 5:
        return meshtastic_mode ? "EXCHANGE POSITION" : "OPEN COMPASS";
    case 6:
        return meshtastic_mode ? "OPEN COMPASS" : "";
    default:
        return "";
    }
}

void Runtime::executeNodeAction()
{
    auto* mesh = app() ? app()->getMeshAdapter() : nullptr;
    auto* contacts = app() ? &app()->getContactService() : nullptr;
    const chat::contacts::NodeInfo* node = selectedNode();
    const bool meshtastic_mode = app() && app()->getConfig().mesh_protocol != chat::MeshProtocol::MeshCore;
    if (!node)
    {
        appendBootLog("node action na");
        showTransientPopup("NODE", "ACTION UNAVAILABLE");
        return;
    }

    switch (node_action_index_)
    {
    case 0:
        node_info_scroll_ = 0;
        enterPage(Page::NodeInfo);
        return;
    case 1:
    {
        active_conversation_ = chat::ConversationId(chat::ChannelId::PRIMARY,
                                                    node->node_id,
                                                    chat::infra::meshProtocolFromRaw(
                                                        static_cast<uint8_t>(node->protocol),
                                                        app()->getConfig().mesh_protocol));
        openCompose(EditTarget::Message);
        return;
    }
    case 2:
    {
        if (!contacts)
        {
            appendBootLog("contact svc na");
            showTransientPopup("ADD CONTACT", "UNAVAILABLE");
            return;
        }
        if (node->is_contact)
        {
            appendBootLog("contact exists");
            showTransientPopup("ADD CONTACT", "ALREADY EXISTS");
            return;
        }

        char nickname[13] = {};
        char fallback_nickname[13] = {};
        if (node->short_name[0] != '\0')
        {
            copyText(nickname, node->short_name);
        }
        else
        {
            std::snprintf(nickname, sizeof(nickname), "%04lX",
                          static_cast<unsigned long>(node->node_id & 0xFFFFUL));
        }
        std::snprintf(fallback_nickname, sizeof(fallback_nickname), "%04lX",
                      static_cast<unsigned long>(node->node_id & 0xFFFFUL));

        bool added = contacts->addContact(node->node_id, nickname);
        if (!added && std::strcmp(nickname, fallback_nickname) != 0)
        {
            added = contacts->addContact(node->node_id, fallback_nickname);
        }

        if (added)
        {
            appendBootLog("contact added");
            showTransientPopup("ADD CONTACT", "SUCCESS");
            rebuildNodeList();
            enterPage(Page::NodeList);
        }
        else
        {
            appendBootLog("contact failed");
            showTransientPopup("ADD CONTACT", "FAILED");
        }
        return;
    }
    case 3:
    {
        if (!contacts)
        {
            appendBootLog("ignore na");
            showTransientPopup("IGNORE NODE", "UNAVAILABLE");
            return;
        }
        const bool ignored = !node->is_ignored;
        const bool ok = contacts->setNodeIgnored(node->node_id, ignored);
        appendBootLog(ok ? (ignored ? "node ignored" : "node unignored") : "ignore failed");
        showTransientPopup(ignored ? "IGNORE NODE" : "UNIGNORE NODE", ok ? "SUCCESS" : "FAILED");
        if (ok)
        {
            rebuildNodeList();
            enterPage(Page::NodeList);
        }
        return;
    }
    case 4:
    {
        if (!mesh)
        {
            appendBootLog("trace na");
            showTransientPopup("TRACE ROUTE", "UNAVAILABLE");
            return;
        }
        meshtastic_RouteDiscovery route = meshtastic_RouteDiscovery_init_zero;
        uint8_t route_buf[96] = {};
        pb_ostream_t stream = pb_ostream_from_buffer(route_buf, sizeof(route_buf));
        if (!pb_encode(&stream, meshtastic_RouteDiscovery_fields, &route))
        {
            appendBootLog("trace encode err");
            showTransientPopup("TRACE ROUTE", "ENCODE FAILED");
            return;
        }

        const bool ok = mesh->sendAppData(chat::ChannelId::PRIMARY,
                                          meshtastic_PortNum_TRACEROUTE_APP,
                                          route_buf,
                                          stream.bytes_written,
                                          node->node_id,
                                          false,
                                          0,
                                          true);
        appendBootLog(ok ? "trace queued" : "trace failed");
        showTransientPopup("TRACE ROUTE", ok ? "QUEUED" : "FAILED");
        return;
    }
    case 5:
        if (meshtastic_mode)
        {
            requestNodePositionExchange();
            return;
        }
        showTransientPopup("OPEN COMPASS", "OPENED");
        enterPage(Page::NodeCompass);
        return;
    case 6:
        showTransientPopup("OPEN COMPASS", "OPENED");
        enterPage(Page::NodeCompass);
        return;
    default:
        return;
    }
}

void Runtime::requestNodePositionExchange()
{
    auto* mesh = app() ? app()->getMeshAdapter() : nullptr;
    const chat::contacts::NodeInfo* node = selectedNode();
    if (!node)
    {
        appendBootLog("pos req node na");
        showTransientPopup("EXCHANGE POSITION", "NO NODE");
        return;
    }
    if (!mesh)
    {
        appendBootLog("pos req na");
        showTransientPopup("EXCHANGE POSITION", "UNAVAILABLE");
        return;
    }

    const bool ok = mesh->sendAppData(chat::ChannelId::PRIMARY,
                                      meshtastic_PortNum_POSITION_APP,
                                      nullptr,
                                      0,
                                      node->node_id,
                                      false,
                                      0,
                                      true);
    appendBootLog(ok ? "pos req queued" : "pos req failed");
    showTransientPopup("EXCHANGE POSITION", ok ? "QUEUED" : "FAILED");
}

} // namespace ui::mono_128x64
