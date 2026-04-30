#include "platform/ui/sstv_runtime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace platform::ui::sstv
{
namespace
{

using Clock = std::chrono::steady_clock;

constexpr const char* kSdRootEnv = "TRAIL_MATE_SD_ROOT";
constexpr const char* kSettingsRootEnv = "TRAIL_MATE_SETTINGS_ROOT";
constexpr const char* kModeEnv = "TRAIL_MATE_SSTV_MODE";
constexpr const char* kOutputDirEnv = "TRAIL_MATE_SSTV_OUTPUT_DIR";

constexpr uint16_t kFrameWidth = 288;
constexpr uint16_t kFrameHeight = 192;
constexpr uint32_t kWaitingMs = 900U;
constexpr uint32_t kReceivingMs = 6200U;

std::mutex s_mutex;
bool s_active = false;
State s_state = State::Idle;
Clock::time_point s_started_at = Clock::now();
std::string s_last_error{};
std::string s_saved_path{};
std::string s_mode_name{};
std::vector<uint16_t> s_frame{};

uint8_t expand5(std::uint16_t value)
{
    return static_cast<uint8_t>((value * 255U) / 31U);
}

uint8_t expand6(std::uint16_t value)
{
    return static_cast<uint8_t>((value * 255U) / 63U);
}

uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    const uint16_t r = static_cast<uint16_t>((red >> 3U) & 0x1FU);
    const uint16_t g = static_cast<uint16_t>((green >> 2U) & 0x3FU);
    const uint16_t b = static_cast<uint16_t>((blue >> 3U) & 0x1FU);
    return static_cast<uint16_t>((r << 11U) | (g << 5U) | b);
}

std::filesystem::path default_storage_root()
{
    if (const char* configured = std::getenv(kOutputDirEnv))
    {
        if (configured[0] != '\0')
        {
            return std::filesystem::path(configured);
        }
    }

    if (const char* configured = std::getenv(kSdRootEnv))
    {
        if (configured[0] != '\0')
        {
            return std::filesystem::path(configured) / "sstv";
        }
    }

    if (const char* configured = std::getenv(kSettingsRootEnv))
    {
        if (configured[0] != '\0')
        {
            return std::filesystem::path(configured) / "sdcard" / "sstv";
        }
    }

#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"))
    {
        if (appdata[0] != '\0')
        {
            return std::filesystem::path(appdata) / "TrailMateCardputerZero" / "sdcard" / "sstv";
        }
    }
#endif

    if (const char* home = std::getenv("HOME"))
    {
        if (home[0] != '\0')
        {
            return std::filesystem::path(home) / ".trailmate_cardputer_zero" / "sdcard" / "sstv";
        }
    }

    return std::filesystem::current_path() / ".trailmate_cardputer_zero" / "sdcard" / "sstv";
}

std::string env_or_default(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return std::string(fallback);
    }
    return std::string(value);
}

void ensure_frame_locked()
{
    if (!s_frame.empty())
    {
        return;
    }

    s_frame.resize(static_cast<std::size_t>(kFrameWidth) * static_cast<std::size_t>(kFrameHeight));
    for (uint16_t y = 0; y < kFrameHeight; ++y)
    {
        for (uint16_t x = 0; x < kFrameWidth; ++x)
        {
            const float fx = static_cast<float>(x) / static_cast<float>(kFrameWidth - 1U);
            const float fy = static_cast<float>(y) / static_cast<float>(kFrameHeight - 1U);
            const uint8_t red = static_cast<uint8_t>(std::lround(210.0f * fx + 30.0f));
            const uint8_t green = static_cast<uint8_t>(std::lround(180.0f * (1.0f - fy) + 35.0f));
            const uint8_t blue = static_cast<uint8_t>(std::lround(120.0f + 100.0f * std::sin((fx + fy) * 4.4f)));

            uint16_t pixel = rgb565(red, green, blue);
            if ((x / 24U) % 2U == 0U)
            {
                pixel = rgb565(std::min<uint8_t>(255U, red + 18U), green, blue);
            }
            if ((y / 16U) % 2U == 1U)
            {
                pixel = rgb565(red, std::min<uint8_t>(255U, green + 22U), blue);
            }

            if (x > 56U && x < 232U && y > 48U && y < 144U)
            {
                const bool border = x < 60U || x > 228U || y < 52U || y > 140U;
                pixel = border ? rgb565(250U, 244U, 220U) : rgb565(242U, 164U, 57U);
            }

            s_frame[static_cast<std::size_t>(y) * static_cast<std::size_t>(kFrameWidth) + x] = pixel;
        }
    }
}

bool save_frame_locked()
{
    ensure_frame_locked();

    std::error_code ec;
    const std::filesystem::path root = default_storage_root();
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
        s_last_error = "Failed to create SSTV output directory";
        return false;
    }

    const std::filesystem::path output = root / "last.ppm";
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        s_last_error = "Failed to save SSTV image";
        return false;
    }

    stream << "P6\n"
           << kFrameWidth << " " << kFrameHeight << "\n255\n";
    for (uint16_t pixel : s_frame)
    {
        const uint8_t red = expand5(static_cast<uint16_t>((pixel >> 11U) & 0x1FU));
        const uint8_t green = expand6(static_cast<uint16_t>((pixel >> 5U) & 0x3FU));
        const uint8_t blue = expand5(static_cast<uint16_t>(pixel & 0x1FU));
        stream.put(static_cast<char>(red));
        stream.put(static_cast<char>(green));
        stream.put(static_cast<char>(blue));
    }
    stream.close();

    if (!stream)
    {
        s_last_error = "Failed to write SSTV image";
        return false;
    }

    s_saved_path = output.string();
    return true;
}

float audio_wave(float seconds)
{
    const float wave = 0.5f + 0.5f * std::sin(seconds * 8.0f);
    return std::clamp(0.18f + wave * 0.72f, 0.0f, 1.0f);
}

Status current_status_locked()
{
    Status status{};
    status.state = s_state;
    status.has_image = !s_frame.empty() && (s_state == State::Receiving || s_state == State::Complete);

    if (!s_active)
    {
        if (s_state == State::Complete)
        {
            status.line = kFrameHeight;
            status.progress = 1.0f;
            status.audio_level = 0.08f;
            status.has_image = true;
        }
        return status;
    }

    const uint32_t elapsed_ms = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - s_started_at).count());
    const float seconds = static_cast<float>(elapsed_ms) / 1000.0f;

    if (elapsed_ms < kWaitingMs)
    {
        s_state = State::Waiting;
        status.state = State::Waiting;
        status.audio_level = audio_wave(seconds * 0.7f) * 0.5f;
        return status;
    }

    const uint32_t rx_elapsed_ms = elapsed_ms - kWaitingMs;
    if (rx_elapsed_ms < kReceivingMs)
    {
        ensure_frame_locked();
        const float progress = static_cast<float>(rx_elapsed_ms) / static_cast<float>(kReceivingMs);
        s_state = State::Receiving;
        status.state = State::Receiving;
        status.progress = std::clamp(progress, 0.0f, 1.0f);
        status.line = static_cast<uint16_t>(std::min<uint32_t>(
            kFrameHeight,
            static_cast<uint32_t>(std::lround(status.progress * static_cast<float>(kFrameHeight)))));
        status.audio_level = audio_wave(seconds);
        status.has_image = true;
        return status;
    }

    if (s_state != State::Complete)
    {
        if (!save_frame_locked())
        {
            s_state = State::Error;
            s_active = false;
            status.state = State::Error;
            status.audio_level = 0.0f;
            status.has_image = !s_frame.empty();
            return status;
        }
        s_state = State::Complete;
        s_active = false;
    }

    status.state = State::Complete;
    status.line = kFrameHeight;
    status.progress = 1.0f;
    status.audio_level = 0.1f;
    status.has_image = !s_frame.empty();
    return status;
}

} // namespace

bool is_supported()
{
    return true;
}

bool start()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_last_error.clear();
    s_saved_path.clear();
    s_mode_name = env_or_default(kModeEnv, "Scottie 1");
    ensure_frame_locked();
    s_active = true;
    s_state = State::Waiting;
    s_started_at = Clock::now();
    return true;
}

void stop()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_active = false;
    if (s_state != State::Complete && s_state != State::Error)
    {
        s_state = State::Idle;
    }
}

bool is_active()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_active;
}

Status get_status()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return current_status_locked();
}

const char* last_error()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_last_error.empty() ? "" : s_last_error.c_str();
}

const char* last_saved_path()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_saved_path.empty() ? "" : s_saved_path.c_str();
}

const char* mode_name()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_mode_name.empty() ? "Scottie 1" : s_mode_name.c_str();
}

const uint16_t* framebuffer()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    ensure_frame_locked();
    return s_frame.empty() ? nullptr : s_frame.data();
}

uint16_t frame_width()
{
    return kFrameWidth;
}

uint16_t frame_height()
{
    return kFrameHeight;
}

} // namespace platform::ui::sstv
