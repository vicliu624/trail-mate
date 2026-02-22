#include "sstv_service.h"

#include <Arduino.h>

#if defined(ARDUINO_LILYGO_LORA_SX1262) && defined(USING_AUDIO_CODEC)

#include "../board/TLoRaPagerBoard.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <SD.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <esp_heap_caps.h>

#include "decode_sstv.h"
#include "display/DisplayInterface.h"
#include "sstv_config.h"
#include "sstv_log.h"

namespace
{
constexpr uint32_t kTaskStack = 12288;
constexpr uint32_t kTaskDelayMs = 2;
constexpr uint32_t kCompleteHoldMs = 2000;
constexpr uint8_t kDecodeTimeoutSec = 40;
constexpr bool kEnableSlantCorrection = true;
constexpr bool kStretch = true;
constexpr int kSamplesPerBlock = 1024;
constexpr int kResampleMaxOut = 4096;

struct LinearResampler
{
    double step = 1.0;
    double pos = 0.0;
    int16_t prev = 0;
    bool has_prev = false;
    bool passthrough = true;

    LinearResampler(uint32_t src_rate, uint32_t dst_rate)
    {
        passthrough = (src_rate == dst_rate);
        if (!passthrough && dst_rate > 0 && src_rate > 0)
        {
            step = static_cast<double>(src_rate) / static_cast<double>(dst_rate);
        }
    }

    int process(const int16_t* in, int n, int16_t* out, int out_cap)
    {
        if (!in || n <= 0 || !out || out_cap <= 0)
        {
            return 0;
        }
        if (passthrough)
        {
            if (n > out_cap)
            {
                n = out_cap;
            }
            memcpy(out, in, static_cast<size_t>(n) * sizeof(int16_t));
            return n;
        }

        int out_len = 0;
        for (int i = 0; i < n; ++i)
        {
            int16_t s = in[i];
            if (!has_prev)
            {
                prev = s;
                has_prev = true;
                continue;
            }
            while (pos < 1.0)
            {
                double sample = static_cast<double>(prev) + (static_cast<double>(s - prev) * pos);
                int32_t v = static_cast<int32_t>(llround(sample));
                if (v > 32767)
                {
                    v = 32767;
                }
                if (v < -32768)
                {
                    v = -32768;
                }
                if (out_len < out_cap)
                {
                    out[out_len++] = static_cast<int16_t>(v);
                }
                else
                {
                    return out_len;
                }
                pos += step;
            }
            pos -= 1.0;
            prev = s;
        }
        return out_len;
    }
};

struct GoertzelState
{
    double coeff = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;
};

void goertzel_init(GoertzelState& st, double target_hz, double sample_rate)
{
    constexpr double kPi = 3.14159265358979323846;
    double w = 2.0 * kPi * target_hz / sample_rate;
    st.coeff = 2.0 * cos(w);
    st.s1 = 0.0;
    st.s2 = 0.0;
}

void goertzel_step(GoertzelState& st, int16_t sample)
{
    double s0 = static_cast<double>(sample) + st.coeff * st.s1 - st.s2;
    st.s2 = st.s1;
    st.s1 = s0;
}

double goertzel_power(const GoertzelState& st)
{
    return (st.s1 * st.s1) + (st.s2 * st.s2) - (st.coeff * st.s1 * st.s2);
}

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t s_task = nullptr;
volatile bool s_stop = false;
bool s_active = false;
bool s_codec_open = false;

sstv::Status s_status;
char s_last_error[96] = {0};
char s_saved_path[64] = {0};
bool s_pending_save = false;
char s_mode_name[24] = "Auto";

uint16_t* s_frame = nullptr;
int s_last_output_y = -1;
bool s_has_image = false;
uint16_t s_last_line = 0;
float s_last_progress = 0.0f;

int64_t s_complete_hold_samples = 0;
bool s_image_in_progress = false;
int64_t s_no_pixel_samples = 0;
int64_t s_log_samples = 0;
bool s_last_in_progress = false;

struct SdSpiGuard
{
    bool locked = false;
    explicit SdSpiGuard(bool enable = true, TickType_t wait = portMAX_DELAY)
    {
        if (enable)
        {
            locked = display_spi_lock(wait);
        }
    }
    ~SdSpiGuard()
    {
        if (locked)
        {
            display_spi_unlock();
        }
    }
};

void set_error(const char* msg)
{
    if (!msg)
    {
        s_last_error[0] = '\0';
        return;
    }
    snprintf(s_last_error, sizeof(s_last_error), "%s", msg);
}

void clear_saved_path()
{
    s_saved_path[0] = '\0';
}

const char* get_saved_path()
{
    return s_saved_path;
}

bool ensure_sstv_dir()
{
    if (!SD.exists("/sstv"))
    {
        if (!SD.mkdir("/sstv"))
        {
            return false;
        }
    }
    return true;
}

bool build_save_path(char* out_path, size_t out_len)
{
    if (!out_path || out_len == 0)
    {
        return false;
    }
    time_t now = time(nullptr);
    struct tm* info = nullptr;
    if (now > 0)
    {
        info = gmtime(&now);
    }

    char date_buf[16] = {0};
    if (info)
    {
        strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", info);
    }

    for (int i = 1; i <= 999; ++i)
    {
        if (info)
        {
            snprintf(out_path, out_len, "/sstv/%s_%03d.bmp", date_buf, i);
        }
        else
        {
            snprintf(out_path, out_len, "/sstv/%lu_%03d.bmp",
                     static_cast<unsigned long>(millis()), i);
        }
        if (!SD.exists(out_path))
        {
            return true;
        }
    }

    return false;
}

uint16_t rgb_to_565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t r5 = static_cast<uint16_t>(r >> 3);
    uint16_t g6 = static_cast<uint16_t>(g >> 2);
    uint16_t b5 = static_cast<uint16_t>(b >> 3);
    return static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

uint16_t panel_rgb565()
{
    uint8_t r = static_cast<uint8_t>((kPanelBg >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((kPanelBg >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(kPanelBg & 0xFF);
    return rgb_to_565(r, g, b);
}

void clear_frame()
{
    if (!s_frame)
    {
        return;
    }
    uint16_t bg = panel_rgb565();
    for (int y = 0; y < kOutHeight; ++y)
    {
        uint16_t* row = s_frame + (y * kOutWidth);
        for (int x = 0; x < kOutWidth; ++x)
        {
            row[x] = bg;
        }
    }
    s_last_output_y = -1;
}

void render_line(const uint16_t* line, uint16_t y, uint16_t width, uint16_t height)
{
    if (!s_frame || !line || width == 0 || height == 0)
    {
        return;
    }

    uint16_t out_y = static_cast<uint16_t>((static_cast<uint32_t>(y) * kOutHeight) / height);
    if (out_y >= kOutHeight)
    {
        return;
    }
    if (out_y == s_last_output_y)
    {
        return;
    }
    s_last_output_y = out_y;

    uint16_t bg = panel_rgb565();
    uint16_t* row = s_frame + (out_y * kOutWidth);
    for (int x = 0; x < kOutWidth; ++x)
    {
        row[x] = bg;
    }

    for (int out_x = 0; out_x < kOutImageWidth; ++out_x)
    {
        uint32_t in_x = static_cast<uint32_t>(out_x) * width / kOutImageWidth;
        if (in_x >= width)
        {
            in_x = width - 1;
        }
        row[kPadX + out_x] = line[in_x];
    }
}

bool save_frame_to_sd()
{
    if (!s_frame)
    {
        set_error("No frame");
        return false;
    }
    SdSpiGuard guard;
    if (SD.cardType() == CARD_NONE)
    {
        set_error("SD not ready");
        return false;
    }
    if (!ensure_sstv_dir())
    {
        set_error("SD mkdir failed");
        return false;
    }

    char path[64];
    if (!build_save_path(path, sizeof(path)))
    {
        set_error("SD path failed");
        return false;
    }

    const uint32_t w = kOutWidth;
    const uint32_t h = kOutHeight;
    const uint32_t row24 = (w * 3 + 3) & ~3u;
    const uint32_t pixel_bytes = row24 * h;
    const uint32_t file_size = 14 + 40 + pixel_bytes;
    const uint32_t data_offset = 14 + 40;

    File f = SD.open(path, FILE_WRITE);
    if (!f)
    {
        set_error("SD open failed");
        return false;
    }

    uint8_t file_hdr[14] = {
        'B', 'M',
        static_cast<uint8_t>(file_size & 0xFF),
        static_cast<uint8_t>((file_size >> 8) & 0xFF),
        static_cast<uint8_t>((file_size >> 16) & 0xFF),
        static_cast<uint8_t>((file_size >> 24) & 0xFF),
        0, 0, 0, 0,
        static_cast<uint8_t>(data_offset & 0xFF),
        static_cast<uint8_t>((data_offset >> 8) & 0xFF),
        static_cast<uint8_t>((data_offset >> 16) & 0xFF),
        static_cast<uint8_t>((data_offset >> 24) & 0xFF)};
    f.write(file_hdr, sizeof(file_hdr));

    uint8_t info_hdr[40] = {0};
    info_hdr[0] = 40;
    info_hdr[4] = static_cast<uint8_t>(w & 0xFF);
    info_hdr[5] = static_cast<uint8_t>((w >> 8) & 0xFF);
    info_hdr[6] = static_cast<uint8_t>((w >> 16) & 0xFF);
    info_hdr[7] = static_cast<uint8_t>((w >> 24) & 0xFF);
    info_hdr[8] = static_cast<uint8_t>(h & 0xFF);
    info_hdr[9] = static_cast<uint8_t>((h >> 8) & 0xFF);
    info_hdr[10] = static_cast<uint8_t>((h >> 16) & 0xFF);
    info_hdr[11] = static_cast<uint8_t>((h >> 24) & 0xFF);
    info_hdr[12] = 1;
    info_hdr[14] = 24;
    info_hdr[16] = 0;
    info_hdr[20] = static_cast<uint8_t>(pixel_bytes & 0xFF);
    info_hdr[21] = static_cast<uint8_t>((pixel_bytes >> 8) & 0xFF);
    info_hdr[22] = static_cast<uint8_t>((pixel_bytes >> 16) & 0xFF);
    info_hdr[23] = static_cast<uint8_t>((pixel_bytes >> 24) & 0xFF);
    f.write(info_hdr, sizeof(info_hdr));

    uint8_t* rowbuf = static_cast<uint8_t*>(malloc(row24));
    if (!rowbuf)
    {
        f.close();
        set_error("SD buffer fail");
        return false;
    }
    memset(rowbuf, 0, row24);

    for (uint32_t y = 0; y < h; ++y)
    {
        const uint16_t* row = s_frame + (h - 1 - y) * w;
        uint32_t idx = 0;
        for (uint32_t x = 0; x < w; ++x)
        {
            uint16_t px = row[x];
            uint8_t r5 = (px >> 11) & 0x1F;
            uint8_t g6 = (px >> 5) & 0x3F;
            uint8_t b5 = px & 0x1F;
            uint8_t r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
            uint8_t g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
            uint8_t b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
            rowbuf[idx++] = b;
            rowbuf[idx++] = g;
            rowbuf[idx++] = r;
        }
        if (row24 > idx)
        {
            memset(rowbuf + idx, 0, row24 - idx);
        }
        f.write(rowbuf, row24);
    }

    f.flush();
    f.close();
    free(rowbuf);

    snprintf(s_saved_path, sizeof(s_saved_path), "%s", path);
    return true;
}

void set_status(sstv::State state, uint16_t line, float progress, float audio_level, bool has_image)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.line = line;
    s_status.progress = progress;
    s_status.audio_level = audio_level;
    s_status.has_image = has_image;
    portEXIT_CRITICAL(&s_lock);
}

uint8_t clamp_u8(int16_t value)
{
    if (value < 0)
    {
        return 0;
    }
    if (value > 255)
    {
        return 255;
    }
    return static_cast<uint8_t>(value);
}

uint16_t ycbcr_to_565(int16_t y, int16_t cr, int16_t cb)
{
    cr = static_cast<int16_t>(cr - 128);
    cb = static_cast<int16_t>(cb - 128);
    int16_t r = static_cast<int16_t>(y + (45 * cr) / 32);
    int16_t g = static_cast<int16_t>(y - (11 * cb + 23 * cr) / 32);
    int16_t b = static_cast<int16_t>(y + (113 * cb) / 64);
    return rgb_to_565(clamp_u8(r), clamp_u8(g), clamp_u8(b));
}

void update_mode_name(e_mode mode, const s_sstv_mode* modes, uint16_t line_y)
{
    if (!modes)
    {
        snprintf(s_mode_name, sizeof(s_mode_name), "Auto");
        return;
    }
    const uint16_t width = modes[mode].width;
    const uint16_t height = static_cast<uint16_t>(line_y + 1);
    switch (mode)
    {
    case martin_m1:
        snprintf(s_mode_name, sizeof(s_mode_name), "Martin M1: %ux%u", width, height);
        break;
    case martin_m2:
        snprintf(s_mode_name, sizeof(s_mode_name), "Martin M2: %ux%u", width, height);
        break;
    case scottie_s1:
        snprintf(s_mode_name, sizeof(s_mode_name), "Scottie S1: %ux%u", width, height);
        break;
    case scottie_s2:
        snprintf(s_mode_name, sizeof(s_mode_name), "Scottie S2: %ux%u", width, height);
        break;
    case scottie_dx:
        snprintf(s_mode_name, sizeof(s_mode_name), "Scottie DX: %ux%u", width, height);
        break;
    case sc2_60:
        snprintf(s_mode_name, sizeof(s_mode_name), "SC2 60: %ux%u", width, height);
        break;
    case sc2_120:
        snprintf(s_mode_name, sizeof(s_mode_name), "SC2 120: %ux%u", width, height);
        break;
    case sc2_180:
        snprintf(s_mode_name, sizeof(s_mode_name), "SC2 180: %ux%u", width, height);
        break;
    case pd_50:
        snprintf(s_mode_name, sizeof(s_mode_name), "PD 50: %ux%u", width, height);
        break;
    case pd_90:
        snprintf(s_mode_name, sizeof(s_mode_name), "PD 90: %ux%u", width, height);
        break;
    case pd_120:
        snprintf(s_mode_name, sizeof(s_mode_name), "PD 120: %ux%u", width, height);
        break;
    case pd_180:
        snprintf(s_mode_name, sizeof(s_mode_name), "PD 180: %ux%u", width, height);
        break;
    case robot24:
        snprintf(s_mode_name, sizeof(s_mode_name), "Robot24: %ux%u", width, height);
        break;
    case robot36:
        snprintf(s_mode_name, sizeof(s_mode_name), "Robot36: %ux%u", width, height);
        break;
    case robot72:
        snprintf(s_mode_name, sizeof(s_mode_name), "Robot72: %ux%u", width, height);
        break;
    case bw8:
        snprintf(s_mode_name, sizeof(s_mode_name), "bw8: %ux%u", width, height);
        break;
    case bw12:
        snprintf(s_mode_name, sizeof(s_mode_name), "bw12: %ux%u", width, height);
        break;
    default:
        snprintf(s_mode_name, sizeof(s_mode_name), "Auto");
        break;
    }
}

void clear_line_rgb(uint8_t line_rgb[320][4], e_mode mode)
{
    for (int x = 0; x < 320; ++x)
    {
        line_rgb[x][0] = 0;
        if (mode != robot36)
        {
            line_rgb[x][1] = 0;
            line_rgb[x][2] = 0;
        }
    }
}

void render_line_from_buffer(uint8_t line_rgb[320][4], e_mode mode, uint16_t line_y, const s_sstv_mode* modes)
{
    if (!modes)
    {
        return;
    }

    uint16_t line_rgb565[320];

    if (mode == pd_50 || mode == pd_90 || mode == pd_120 || mode == pd_180)
    {
        uint16_t scaled_pixel_y = line_y;
        uint16_t render_height = static_cast<uint16_t>(modes[mode].max_height * 2);
        if (mode == pd_120 || mode == pd_180)
        {
            scaled_pixel_y = static_cast<uint16_t>((static_cast<uint32_t>(line_y) * 240u) / 496u);
            render_height = 240;
        }

        for (uint16_t x = 0; x < 320; ++x)
        {
            line_rgb565[x] = ycbcr_to_565(line_rgb[x][0], line_rgb[x][1], line_rgb[x][2]);
        }
        render_line(line_rgb565, static_cast<uint16_t>(scaled_pixel_y * 2u), 320, render_height);

        for (uint16_t x = 0; x < 320; ++x)
        {
            line_rgb565[x] = ycbcr_to_565(line_rgb[x][3], line_rgb[x][1], line_rgb[x][2]);
        }
        render_line(line_rgb565, static_cast<uint16_t>(scaled_pixel_y * 2u + 1u), 320, render_height);
    }
    else if (mode == bw8 || mode == bw12)
    {
        const uint16_t render_height = static_cast<uint16_t>(modes[mode].max_height * 2);
        for (uint16_t x = 0; x < 320; ++x)
        {
            uint8_t gray = line_rgb[x][0];
            line_rgb565[x] = rgb_to_565(gray, gray, gray);
        }
        render_line(line_rgb565, static_cast<uint16_t>(line_y * 2u), 320, render_height);
        render_line(line_rgb565, static_cast<uint16_t>(line_y * 2u + 1u), 320, render_height);
    }
    else if (mode == robot24 || mode == robot72)
    {
        for (uint16_t x = 0; x < 320; ++x)
        {
            line_rgb565[x] = ycbcr_to_565(line_rgb[x][0], line_rgb[x][1], line_rgb[x][2]);
        }
        if (mode == robot24)
        {
            const uint16_t render_height = static_cast<uint16_t>(modes[mode].max_height * 2);
            render_line(line_rgb565, static_cast<uint16_t>(line_y * 2u), 320, render_height);
            render_line(line_rgb565, static_cast<uint16_t>(line_y * 2u + 1u), 320, render_height);
        }
        else
        {
            render_line(line_rgb565, line_y, 320, modes[mode].max_height);
        }
    }
    else if (mode == robot36)
    {
        uint8_t count = 0;
        for (uint16_t x = 0; x < 40; ++x)
        {
            if (line_rgb[x][3] > 128)
            {
                ++count;
            }
        }

        uint8_t crc = 2;
        uint8_t cbc = 1;
        if ((count < 20 && (line_y % 2 == 0)) || (count > 20 && (line_y % 2 == 1)))
        {
            crc = 1;
            cbc = 2;
        }

        for (uint16_t x = 0; x < 320; ++x)
        {
            line_rgb565[x] = ycbcr_to_565(line_rgb[x][0], line_rgb[x][crc], line_rgb[x][cbc]);
        }
        render_line(line_rgb565, line_y, 320, modes[mode].max_height);
    }
    else
    {
        for (uint16_t x = 0; x < 320; ++x)
        {
            line_rgb565[x] = rgb_to_565(line_rgb[x][0], line_rgb[x][1], line_rgb[x][2]);
        }
        render_line(line_rgb565, line_y, 320, modes[mode].max_height);
    }

    update_mode_name(mode, modes, line_y);
    s_last_line = static_cast<uint16_t>(line_y + 1);
    s_last_progress = (modes[mode].max_height > 0)
                          ? (static_cast<float>(line_y + 1) / modes[mode].max_height)
                          : 0.0f;
    s_has_image = true;
}

void sstv_task(void*)
{
    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    int prev_volume = -1;
    bool prev_out_mute = false;
    bool restore_amp = false;
    bool prev_amp = true;
    if (!board)
    {
        set_error("Board not ready");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_active = false;
        vTaskDelete(nullptr);
        return;
    }

    if (board->codec.open(kBitsPerSample, kChannels, kCodecSampleRate) != 0)
    {
        set_error("Codec open failed");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_active = false;
        vTaskDelete(nullptr);
        return;
    }

    s_codec_open = true;
    board->codec.setGain(kMicGainDb);
    board->codec.setMute(false);
    prev_volume = board->codec.getVolume();
    prev_out_mute = board->codec.getOutMute();
    board->codec.setOutMute(true);
    board->codec.setVolume(0);
#ifdef USING_XL9555_EXPANDS
    prev_amp = board->io.digitalRead(EXPANDS_AMP_EN);
    restore_amp = true;
#endif
    // Disable speaker amplifier during RX to avoid feedback/noise.
    board->powerControl(POWER_SPEAK, false);
    SSTV_LOG("[SSTV] codec open ok (rate=%lu, decode=%lu, bits=%u, ch=%u)\n",
             static_cast<unsigned long>(kCodecSampleRate),
             static_cast<unsigned long>(kSampleRate),
             static_cast<unsigned>(kBitsPerSample),
             static_cast<unsigned>(kChannels));

    const int samples_per_read = kSamplesPerBlock * kChannels;
    const int frames_per_read = kSamplesPerBlock;
    LinearResampler resampler(kCodecSampleRate, kSampleRate);
    int16_t* buffer = static_cast<int16_t*>(malloc(samples_per_read * sizeof(int16_t)));
    if (!buffer)
    {
        set_error("No audio buffer");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_codec_open = false;
        board->codec.close();
        s_active = false;
        vTaskDelete(nullptr);
        return;
    }
    auto* mono_buf = static_cast<int16_t*>(malloc(kSamplesPerBlock * sizeof(int16_t)));
    auto* resampled = static_cast<int16_t*>(malloc(kResampleMaxOut * sizeof(int16_t)));
    auto* line_rgb = static_cast<uint8_t (*)[4]>(malloc(320 * 4));
    if (!mono_buf || !resampled || !line_rgb)
    {
        free(line_rgb);
        free(resampled);
        free(mono_buf);
        free(buffer);
        set_error("No decode buffers");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_codec_open = false;
        board->codec.close();
        s_active = false;
        vTaskDelete(nullptr);
        return;
    }

    if (!s_frame)
    {
        const size_t bytes = static_cast<size_t>(kOutWidth) * kOutHeight * sizeof(uint16_t);
        s_frame = static_cast<uint16_t*>(
            heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!s_frame)
        {
            s_frame = static_cast<uint16_t*>(malloc(bytes));
        }
    }

    if (!s_frame)
    {
        free(buffer);
        set_error("No framebuffer");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_codec_open = false;
        board->codec.close();
        s_active = false;
        vTaskDelete(nullptr);
        return;
    }

    clear_frame();
    s_has_image = false;
    s_last_line = 0;
    s_last_progress = 0.0f;
    s_complete_hold_samples = 0;
    s_image_in_progress = false;
    s_no_pixel_samples = 0;
    s_log_samples = 0;
    s_last_in_progress = false;
    snprintf(s_mode_name, sizeof(s_mode_name), "Auto");

    c_sstv_decoder decoder(static_cast<float>(kSampleRate));
    decoder.set_auto_slant_correction(kEnableSlantCorrection);
    decoder.set_timeout_seconds(kDecodeTimeoutSec);
    s_sstv_mode* modes = decoder.get_modes();

    memset(line_rgb, 0, 320 * 4);
    uint16_t last_pixel_y = 0;
    bool image_complete_reported = false;
    uint32_t pixel_count = 0;
    uint32_t line_advances = 0;
    uint32_t line_resets = 0;
    int64_t freq_sum = 0;
    int32_t freq_min = 32767;
    int32_t freq_max = -32768;
    int64_t freq_count = 0;
    e_mode last_mode = martin_m1;
    uint32_t freq_samples = 0;
    uint32_t freq_low = 0;
    uint32_t freq_1200 = 0;
    uint32_t freq_1900 = 0;
    uint32_t low_run = 0;
    uint32_t low_run_max = 0;
    uint32_t sync_samples = 0;
    uint32_t sync_count = 0;
    uint32_t sync_low_count = 0;
    uint32_t last_sync_sample = 0;
    uint32_t line_len_min = 0;
    uint32_t line_len_max = 0;
    uint64_t line_len_sum = 0;
    int16_t last_freq_sample = 0;
    bool sync_confirm = false;
    uint32_t clip_samples = 0;

    float audio_level = 0.0f;
    int16_t dc = 0;
    GoertzelState g1200;
    GoertzelState g1900;
    goertzel_init(g1200, 1200.0, static_cast<double>(kSampleRate));
    goertzel_init(g1900, 1900.0, static_cast<double>(kSampleRate));
    double energy_sum = 0.0;

    while (!s_stop)
    {
        int read_state = board->codec.read(reinterpret_cast<uint8_t*>(buffer),
                                           samples_per_read * sizeof(int16_t));
        if (read_state != 0)
        {
            set_error("Audio read failed");
            set_status(sstv::State::Error, 0, 0.0f, 0.0f, s_has_image);
            break;
        }

        int block_peak = 0;
        bool had_pixel = false;
        for (int i = 0; i < frames_per_read; ++i)
        {
            int16_t mono = 0;
            if (kChannels == 1)
            {
                mono = buffer[i];
            }
            else
            {
                const int idx = i * kChannels;
                int32_t l = buffer[idx];
                int32_t r = buffer[idx + 1];
                mono = static_cast<int16_t>((l + r) / 2);
            }
            dc = static_cast<int16_t>(dc + (mono - dc) / 2);
            int32_t scaled = static_cast<int32_t>(static_cast<float>(mono - dc) * kInputPcmGain);
            if (scaled > 32767)
            {
                scaled = 32767;
            }
            if (scaled < -32768)
            {
                scaled = -32768;
            }
            int16_t sample = static_cast<int16_t>(scaled);
            int sample_mag = sample < 0 ? -sample : sample;
            if (sample_mag > block_peak)
            {
                block_peak = sample_mag;
            }
            if (sample_mag >= 32000)
            {
                clip_samples += 1;
            }
            mono_buf[i] = sample;
        }

        int resampled_count =
            resampler.process(mono_buf, frames_per_read, resampled, kResampleMaxOut);
        int decode_samples = resampled_count;
        for (int i = 0; i < resampled_count; ++i)
        {
            int16_t sample = resampled[i];
            uint16_t pixel_y = 0;
            uint16_t pixel_x = 0;
            uint8_t pixel_colour = 0;
            uint8_t pixel = 0;
            int16_t frequency = 0;
            bool new_pixel =
                decoder.decode_audio(sample, pixel_y, pixel_x, pixel_colour, pixel, frequency);
            goertzel_step(g1200, sample);
            goertzel_step(g1900, sample);
            energy_sum += static_cast<double>(sample) * static_cast<double>(sample);
            freq_sum += frequency;
            freq_count += 1;
            sync_samples += 1;
            if (!sync_confirm)
            {
                if (frequency < 1300 && last_freq_sample >= 1300)
                {
                    sync_confirm = true;
                    sync_low_count = 0;
                }
            }
            else
            {
                if (frequency < 1300)
                {
                    sync_low_count += 1;
                }
                else if (sync_low_count > 0)
                {
                    sync_low_count -= 1;
                }

                if (sync_low_count >= 10)
                {
                    uint32_t line_len = sync_samples - last_sync_sample;
                    last_sync_sample = sync_samples;
                    sync_count += 1;
                    if (line_len_min == 0 || line_len < line_len_min)
                    {
                        line_len_min = line_len;
                    }
                    if (line_len > line_len_max)
                    {
                        line_len_max = line_len;
                    }
                    line_len_sum += line_len;
                    sync_confirm = false;
                }
            }
            last_freq_sample = frequency;
            if (frequency < freq_min)
            {
                freq_min = frequency;
            }
            if (frequency > freq_max)
            {
                freq_max = frequency;
            }
            freq_samples += 1;
            if (frequency < 1300)
            {
                freq_low += 1;
                low_run += 1;
            }
            else
            {
                if (low_run > low_run_max)
                {
                    low_run_max = low_run;
                }
                low_run = 0;
            }
            if (frequency >= 1150 && frequency <= 1250)
            {
                freq_1200 += 1;
            }
            if (frequency >= 1850 && frequency <= 1950)
            {
                freq_1900 += 1;
            }
            if (new_pixel)
            {
                had_pixel = true;
                e_mode mode = decoder.get_mode();
                last_mode = mode;
                pixel_count += 1;
                if (pixel_y < last_pixel_y)
                {
                    last_pixel_y = 0;
                    image_complete_reported = false;
                    line_resets += 1;
                }
                if (pixel_y > last_pixel_y)
                {
                    line_advances += 1;
                    render_line_from_buffer(line_rgb, mode, last_pixel_y, modes);
                    if (!image_complete_reported && modes &&
                        (last_pixel_y + 1 >= modes[mode].max_height))
                    {
                        SSTV_LOG("[SSTV] image complete (line=%u, mode=%s)\n",
                                 static_cast<unsigned>(s_last_line), s_mode_name);
                        s_pending_save = true;
                        s_complete_hold_samples = static_cast<int64_t>(
                            kCompleteHoldMs * (static_cast<float>(kSampleRate) / 1000.0f));
                        image_complete_reported = true;
                    }
                    clear_line_rgb(line_rgb, mode);
                }

                last_pixel_y = pixel_y;
                if (pixel_x < 320 && pixel_y < 256 && pixel_colour < 4)
                {
                    if (kStretch && modes && modes[mode].width == 160)
                    {
                        if (pixel_x < 160)
                        {
                            uint16_t idx = static_cast<uint16_t>(pixel_x * 2);
                            line_rgb[idx][pixel_colour] = pixel;
                            line_rgb[idx + 1][pixel_colour] = pixel;
                        }
                    }
                    else
                    {
                        line_rgb[pixel_x][pixel_colour] = pixel;
                    }
                }
            }
        }

        audio_level = (audio_level * 0.8f) + (0.2f * (block_peak / 32768.0f));
        if (audio_level < 0.0f)
        {
            audio_level = 0.0f;
        }
        if (audio_level > 1.0f)
        {
            audio_level = 1.0f;
        }

        if (had_pixel)
        {
            s_image_in_progress = true;
            s_no_pixel_samples = 0;
        }
        else
        {
            s_no_pixel_samples += decode_samples;
            const int64_t no_pixel_timeout =
                static_cast<int64_t>(kDecodeTimeoutSec) * kSampleRate;
            if (s_no_pixel_samples > no_pixel_timeout)
            {
                s_image_in_progress = false;
            }
        }

        if (s_image_in_progress != s_last_in_progress)
        {
            SSTV_LOG("[SSTV] decode %s (line=%u, mode=%s)\n",
                     s_image_in_progress ? "started" : "stopped",
                     static_cast<unsigned>(s_last_line), s_mode_name);
            s_last_in_progress = s_image_in_progress;
        }

        if (s_complete_hold_samples > 0)
        {
            s_complete_hold_samples -= decode_samples;
            if (s_complete_hold_samples < 0)
            {
                s_complete_hold_samples = 0;
            }
        }

        sstv::State state = sstv::State::Waiting;
        if (s_image_in_progress)
        {
            state = sstv::State::Receiving;
        }
        else if (s_complete_hold_samples > 0)
        {
            state = sstv::State::Complete;
        }

        set_status(state, s_last_line, s_last_progress, audio_level, s_has_image);

        s_log_samples += decode_samples;
        if (s_log_samples >= static_cast<int64_t>(kSampleRate))
        {
            s_log_samples = 0;
            SSTV_LOG("[SSTV] status level=%.2f peak=%d in_progress=%d line=%u prog=%.2f mode=%s\n",
                     static_cast<double>(audio_level), block_peak,
                     s_image_in_progress ? 1 : 0,
                     static_cast<unsigned>(s_last_line),
                     static_cast<double>(s_last_progress),
                     s_mode_name);
            long freq_avg = 0;
            if (freq_count > 0)
            {
                freq_avg = static_cast<long>(freq_sum / freq_count);
            }
            if (low_run > low_run_max)
            {
                low_run_max = low_run;
            }
            int low_pct = 0;
            int tone1200_pct = 0;
            int tone1900_pct = 0;
            if (freq_samples > 0)
            {
                low_pct = static_cast<int>((freq_low * 100u) / freq_samples);
                tone1200_pct = static_cast<int>((freq_1200 * 100u) / freq_samples);
                tone1900_pct = static_cast<int>((freq_1900 * 100u) / freq_samples);
            }
            double p1200 = goertzel_power(g1200);
            double p1900 = goertzel_power(g1900);
            double tone_ratio_1200 = (energy_sum > 0.0) ? (p1200 / energy_sum) : 0.0;
            double tone_ratio_1900 = (energy_sum > 0.0) ? (p1900 / energy_sum) : 0.0;
            uint32_t line_len_avg = 0;
            if (sync_count > 0)
            {
                line_len_avg = static_cast<uint32_t>(line_len_sum / sync_count);
            }
            int clip_pct = 0;
            if (freq_samples > 0)
            {
                clip_pct = static_cast<int>((clip_samples * 100u) / freq_samples);
            }
            SSTV_LOG("[SSTV] debug px=%lu lines=%lu resets=%lu last_y=%u mode=%d f[%ld/%ld/%ld] low<1300=%d%% 1200=%d%% 1900=%d%% low_run_max=%lu clip=%d%%\n",
                     static_cast<unsigned long>(pixel_count),
                     static_cast<unsigned long>(line_advances),
                     static_cast<unsigned long>(line_resets),
                     static_cast<unsigned>(last_pixel_y),
                     static_cast<int>(last_mode),
                     static_cast<long>(freq_min),
                     static_cast<long>(freq_avg),
                     static_cast<long>(freq_max),
                     low_pct,
                     tone1200_pct,
                     tone1900_pct,
                     static_cast<unsigned long>(low_run_max),
                     clip_pct);
            SSTV_LOG("[SSTV] tone p1200=%.0f p1900=%.0f ratio1200=%.6f ratio1900=%.6f sync=%lu line_len=%lu(min=%lu max=%lu)\n",
                     p1200,
                     p1900,
                     tone_ratio_1200,
                     tone_ratio_1900,
                     static_cast<unsigned long>(sync_count),
                     static_cast<unsigned long>(line_len_avg),
                     static_cast<unsigned long>(line_len_min),
                     static_cast<unsigned long>(line_len_max));
            pixel_count = 0;
            line_advances = 0;
            line_resets = 0;
            freq_sum = 0;
            freq_count = 0;
            freq_min = 32767;
            freq_max = -32768;
            freq_samples = 0;
            freq_low = 0;
            freq_1200 = 0;
            freq_1900 = 0;
            low_run = 0;
            low_run_max = 0;
            sync_samples = 0;
            sync_count = 0;
            sync_low_count = 0;
            last_sync_sample = 0;
            line_len_min = 0;
            line_len_max = 0;
            line_len_sum = 0;
            last_freq_sample = 0;
            sync_confirm = false;
            clip_samples = 0;
            goertzel_init(g1200, 1200.0, static_cast<double>(kSampleRate));
            goertzel_init(g1900, 1900.0, static_cast<double>(kSampleRate));
            energy_sum = 0.0;
        }

        if (s_pending_save)
        {
            if (save_frame_to_sd())
            {
                s_pending_save = false;
            }
            else
            {
                s_pending_save = false;
            }
        }

        vTaskDelay(kTaskDelayMs);
    }

    free(line_rgb);
    free(resampled);
    free(mono_buf);
    free(buffer);

    if (s_codec_open)
    {
        if (prev_volume >= 0)
        {
            board->codec.setVolume(static_cast<uint8_t>(prev_volume));
        }
        board->codec.setOutMute(prev_out_mute);
        if (restore_amp)
        {
            board->powerControl(POWER_SPEAK, prev_amp);
        }
        board->codec.close();
        s_codec_open = false;
    }

    s_active = false;
    set_status(sstv::State::Idle, 0, 0.0f, 0.0f, s_has_image);
    vTaskDelete(nullptr);
}

} // namespace

namespace sstv
{

bool start()
{
    if (s_active)
    {
        return true;
    }

    set_error(nullptr);
    clear_saved_path();
    s_pending_save = false;

    s_stop = false;
    s_active = true;
    set_status(State::Waiting, 0, 0.0f, 0.0f, s_frame != nullptr);

    BaseType_t ok = xTaskCreatePinnedToCore(
        sstv_task,
        "sstv_rx",
        kTaskStack,
        nullptr,
        4,
        &s_task,
        0);
    if (ok != pdPASS)
    {
        ok = xTaskCreate(
            sstv_task,
            "sstv_rx",
            kTaskStack,
            nullptr,
            4,
            &s_task);
        if (ok != pdPASS)
        {
            s_task = nullptr;
            s_active = false;
            set_error("Task create failed");
            set_status(State::Error, 0, 0.0f, 0.0f, false);
            return false;
        }
    }

    return true;
}

void stop()
{
    if (!s_active)
    {
        return;
    }
    s_stop = true;
}

bool is_active()
{
    return s_active;
}

Status get_status()
{
    Status out;
    portENTER_CRITICAL(&s_lock);
    out = s_status;
    portEXIT_CRITICAL(&s_lock);
    return out;
}

const char* get_last_error()
{
    return s_last_error;
}

const char* get_last_saved_path()
{
    return get_saved_path();
}

const char* get_mode_name()
{
    return s_mode_name;
}

const uint16_t* get_framebuffer()
{
    return s_frame;
}

uint16_t frame_width()
{
    return kOutWidth;
}

uint16_t frame_height()
{
    return kOutHeight;
}

} // namespace sstv

#else

namespace sstv
{

bool start()
{
    return false;
}

void stop() {}

bool is_active()
{
    return false;
}

Status get_status()
{
    return Status{};
}

const char* get_last_error()
{
    return "SSTV unsupported";
}

const char* get_last_saved_path()
{
    return "";
}

const char* get_mode_name()
{
    return "";
}

const uint16_t* get_framebuffer()
{
    return nullptr;
}

uint16_t frame_width()
{
    return 0;
}

uint16_t frame_height()
{
    return 0;
}

} // namespace sstv

#endif
