#include "sstv_service.h"

#include <Arduino.h>

#ifndef SSTV_DEBUG
#define SSTV_DEBUG 1
#endif

#if SSTV_DEBUG
#define SSTV_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define SSTV_LOG(...) ((void)0)
#endif

#if defined(ARDUINO_LILYGO_LORA_SX1262) && defined(USING_AUDIO_CODEC)

#include "../board/TLoRaPagerBoard.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <SD.h>
#include <cmath>
#include <cstring>
#include <ctime>

namespace
{
constexpr uint32_t kSampleRate = 44100;
constexpr uint8_t kBitsPerSample = 16;
constexpr uint8_t kChannels = 2;
constexpr float kMicGainDb = 36.0f;
constexpr uint32_t kTaskStack = 8192;
constexpr uint32_t kTaskDelayMs = 2;

constexpr float kPorchMs = 1.5f;
constexpr float kColorMs = 138.24f;
constexpr float kLeaderMs = 300.0f;
constexpr float kBreakMs = 10.0f;
constexpr float kVisBitMs = 30.0f;
constexpr uint8_t kVisScottie1 = 60;

constexpr int kInWidth = 320;
constexpr int kInHeight = 256;
constexpr int kOutWidth = 288;
constexpr int kOutHeight = 192;
constexpr int kOutImageWidth = 240;
constexpr int kPadX = (kOutWidth - kOutImageWidth) / 2;

constexpr uint32_t kPanelBg = 0xFAF0D8;

constexpr float kFreqMin = 1500.0f;
constexpr float kFreqMax = 2300.0f;
constexpr float kFreqSpan = kFreqMax - kFreqMin;

constexpr float kMinSyncGapMs = 420.0f;
constexpr float kHeaderToneDetectRatio = 1.3f;
constexpr float kHeaderToneTotalRatio = 0.45f;
constexpr float kSyncToneDetectRatio = 1.6f;
constexpr float kSyncToneTotalRatio = 0.55f;
constexpr int kHeaderWindowSamples = 256;
constexpr int kHeaderHopSamples = 128;
constexpr int kSyncWindowSamples = 400;
constexpr int kSyncHopSamples = 80;
constexpr float kPixelBinStep = 25.0f;
constexpr int kPixelBinCount = static_cast<int>((kFreqMax - kFreqMin) / kPixelBinStep) + 1;
constexpr int kMaxPixelSamples = 32;

enum class Phase : uint8_t
{
    Idle = 0,
    Porch,
    Green,
    Blue,
    Red,
};

enum class HeaderState : uint8_t
{
    SeekLeader1 = 0,
    SeekBreak,
    SeekLeader2,
    SeekVisStart,
    ReadVisBits,
};

enum class Tone : uint8_t
{
    None = 0,
    Tone1100,
    Tone1200,
    Tone1300,
    Tone1900,
};

struct GoertzelBin
{
    float freq = 0.0f;
    float coeff = 0.0f;
    float cos_w = 0.0f;
    float sin_w = 0.0f;
};

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t s_task = nullptr;
volatile bool s_stop = false;
bool s_active = false;
bool s_codec_open = false;

sstv::Status s_status;
char s_last_error[96] = {0};
char s_saved_path[64] = {0};
bool s_pending_save = false;

uint16_t* s_frame = nullptr;
int s_last_output_y = -1;

uint32_t s_accum[3][kInWidth];
uint16_t s_count[3][kInWidth];

GoertzelBin s_bin_1100 = {};
GoertzelBin s_bin_1200 = {};
GoertzelBin s_bin_1300 = {};
GoertzelBin s_bin_1900 = {};
GoertzelBin s_pixel_bins[kPixelBinCount] = {};

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

GoertzelBin make_bin(float freq)
{
    GoertzelBin bin;
    bin.freq = freq;
    float w = 2.0f * static_cast<float>(M_PI) * freq / static_cast<float>(kSampleRate);
    bin.cos_w = cosf(w);
    bin.sin_w = sinf(w);
    bin.coeff = 2.0f * bin.cos_w;
    return bin;
}

float goertzel_power(const int16_t* data, int count, const GoertzelBin& bin)
{
    float q0 = 0.0f;
    float q1 = 0.0f;
    float q2 = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        q0 = bin.coeff * q1 - q2 + static_cast<float>(data[i]);
        q2 = q1;
        q1 = q0;
    }
    float real = q1 - q2 * bin.cos_w;
    float imag = q2 * bin.sin_w;
    return real * real + imag * imag;
}

float estimate_freq_from_bins(const int16_t* data, int count, const GoertzelBin* bins, int bin_count)
{
    if (!bins || bin_count <= 0)
    {
        return kFreqMin;
    }

    float max_val = 0.0f;
    int max_idx = 0;
    float mags[kPixelBinCount];
    for (int i = 0; i < bin_count; ++i)
    {
        mags[i] = goertzel_power(data, count, bins[i]);
        if (mags[i] > max_val)
        {
            max_val = mags[i];
            max_idx = i;
        }
    }

    int left = max_idx > 0 ? max_idx - 1 : max_idx;
    int right = max_idx + 1 < bin_count ? max_idx + 1 : max_idx;
    float y1 = mags[left];
    float y2 = mags[max_idx];
    float y3 = mags[right];
    float denom = y1 + y2 + y3;
    float peak = static_cast<float>(max_idx);
    if (denom > 0.0f)
    {
        peak = peak + (y3 - y1) / denom;
    }

    float freq = kFreqMin + peak * kPixelBinStep;
    if (freq < kFreqMin)
    {
        freq = kFreqMin;
    }
    if (freq > kFreqMax)
    {
        freq = kFreqMax;
    }
    return freq;
}

Tone detect_tone(float p1100, float p1200, float p1300, float p1900)
{
    float total = p1100 + p1200 + p1300 + p1900;
    if (total <= 0.0f)
    {
        return Tone::None;
    }

    float max_val = p1100;
    Tone tone = Tone::Tone1100;
    if (p1200 > max_val)
    {
        max_val = p1200;
        tone = Tone::Tone1200;
    }
    if (p1300 > max_val)
    {
        max_val = p1300;
        tone = Tone::Tone1300;
    }
    if (p1900 > max_val)
    {
        max_val = p1900;
        tone = Tone::Tone1900;
    }

    float other_max = 0.0f;
    if (tone != Tone::Tone1100 && p1100 > other_max)
    {
        other_max = p1100;
    }
    if (tone != Tone::Tone1200 && p1200 > other_max)
    {
        other_max = p1200;
    }
    if (tone != Tone::Tone1300 && p1300 > other_max)
    {
        other_max = p1300;
    }
    if (tone != Tone::Tone1900 && p1900 > other_max)
    {
        other_max = p1900;
    }

    if (max_val > other_max * kHeaderToneDetectRatio && max_val > total * kHeaderToneTotalRatio)
    {
        return tone;
    }

    return Tone::None;
}

const char* header_state_name(HeaderState state)
{
    switch (state)
    {
    case HeaderState::SeekLeader1:
        return "Leader1";
    case HeaderState::SeekBreak:
        return "Break";
    case HeaderState::SeekLeader2:
        return "Leader2";
    case HeaderState::SeekVisStart:
        return "VisStart";
    case HeaderState::ReadVisBits:
        return "VisBits";
    default:
        return "Unknown";
    }
}

const char* tone_name(Tone tone)
{
    switch (tone)
    {
    case Tone::Tone1100:
        return "1100";
    case Tone::Tone1200:
        return "1200";
    case Tone::Tone1300:
        return "1300";
    case Tone::Tone1900:
        return "1900";
    default:
        return "None";
    }
}

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

bool save_frame_to_sd()
{
    if (!s_frame)
    {
        set_error("No frame");
        return false;
    }
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

void clear_accum()
{
    memset(s_accum, 0, sizeof(s_accum));
    memset(s_count, 0, sizeof(s_count));
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

uint8_t clamp_u8(int v)
{
    if (v < 0)
    {
        return 0;
    }
    if (v > 255)
    {
        return 255;
    }
    return static_cast<uint8_t>(v);
}

uint8_t freq_to_intensity(float freq)
{
    if (freq < kFreqMin)
    {
        freq = kFreqMin;
    }
    if (freq > kFreqMax)
    {
        freq = kFreqMax;
    }
    float ratio = (freq - kFreqMin) / kFreqSpan;
    int value = static_cast<int>(ratio * 255.0f + 0.5f);
    return clamp_u8(value);
}

void render_line(int line)
{
    if (!s_frame)
    {
        return;
    }
    int out_y = (line * kOutHeight) / kInHeight;
    if (out_y == s_last_output_y || out_y < 0 || out_y >= kOutHeight)
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
        int in_x = (out_x * kInWidth) / kOutImageWidth;
        if (in_x < 0)
        {
            in_x = 0;
        }
        if (in_x >= kInWidth)
        {
            in_x = kInWidth - 1;
        }

        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t r = 0;
        if (s_count[0][in_x])
        {
            g = static_cast<uint8_t>(s_accum[0][in_x] / s_count[0][in_x]);
        }
        if (s_count[1][in_x])
        {
            b = static_cast<uint8_t>(s_accum[1][in_x] / s_count[1][in_x]);
        }
        if (s_count[2][in_x])
        {
            r = static_cast<uint8_t>(s_accum[2][in_x] / s_count[2][in_x]);
        }

        row[kPadX + out_x] = rgb_to_565(r, g, b);
    }
}

void sstv_task(void*)
{
    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    if (!board)
    {
        set_error("Board not ready");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    if (!(board->getDevicesProbe() & HW_CODEC_ONLINE))
    {
        set_error("Audio codec not ready");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    if (board->codec.open(kBitsPerSample, kChannels, kSampleRate) != 0)
    {
        set_error("Codec open failed");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    s_codec_open = true;
    board->codec.setGain(kMicGainDb);
    board->codec.setMute(false);
    SSTV_LOG("[SSTV] codec open ok (rate=%lu, bits=%u, ch=%u)\n",
             static_cast<unsigned long>(kSampleRate), kBitsPerSample, kChannels);

    const int samples_per_block = 512;
    const int samples_per_read = samples_per_block * kChannels;
    int16_t* buffer = static_cast<int16_t*>(malloc(samples_per_read * sizeof(int16_t)));
    if (!buffer)
    {
        set_error("No audio buffer");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_codec_open = false;
        board->codec.close();
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    s_bin_1100 = make_bin(1100.0f);
    s_bin_1200 = make_bin(1200.0f);
    s_bin_1300 = make_bin(1300.0f);
    s_bin_1900 = make_bin(1900.0f);
    for (int i = 0; i < kPixelBinCount; ++i)
    {
        float freq = kFreqMin + static_cast<float>(i) * kPixelBinStep;
        s_pixel_bins[i] = make_bin(freq);
    }

    const int porch_samples = static_cast<int>(kSampleRate * (kPorchMs / 1000.0f));
    const int color_samples = static_cast<int>(kSampleRate * (kColorMs / 1000.0f));
    const int min_sync_gap = static_cast<int>(kSampleRate * (kMinSyncGapMs / 1000.0f));
    const float header_window_ms =
        1000.0f * static_cast<float>(kHeaderHopSamples) / static_cast<float>(kSampleRate);
    int leader_windows = static_cast<int>(kLeaderMs / header_window_ms + 0.5f);
    if (leader_windows < 1)
    {
        leader_windows = 1;
    }
    int break_windows = static_cast<int>(kBreakMs / header_window_ms + 0.5f);
    if (break_windows < 1)
    {
        break_windows = 1;
    }
    int vis_windows_per_bit = static_cast<int>(kVisBitMs / header_window_ms + 0.5f);
    if (vis_windows_per_bit < 1)
    {
        vis_windows_per_bit = 1;
    }
    int pixel_window_samples = color_samples / kInWidth;
    if (pixel_window_samples < 8)
    {
        pixel_window_samples = 8;
    }
    if (pixel_window_samples > kMaxPixelSamples)
    {
        pixel_window_samples = kMaxPixelSamples;
    }

    int line_index = 0;
    Phase phase = Phase::Idle;
    int phase_samples = 0;
    int last_pixel = -1;
    int pixel_pos = 0;
    int pixel_fill = 0;

    HeaderState header_state = HeaderState::SeekLeader1;
    int header_count = 0;
    bool header_ok = false;
    int vis_bit_index = 0;
    uint8_t vis_value = 0;
    int vis_ones = 0;
    int vis_window_count = 0;
    int vis_1100_count = 0;
    int vis_1300_count = 0;
    int vis_skip_windows = 0;
    int header_log_tick = 0;
    const int header_log_every = 10;
    int header_stat_tick = 0;
    const int header_stat_every = 50;
    int break_window_count = 0;
    int break_hit_count = 0;
    double break_ratio_total_sum = 0.0;
    double break_ratio_max_sum = 0.0;
    int leader2_window_count = 0;
    int leader2_hit_count = 0;
    double leader2_ratio_total_sum = 0.0;
    double leader2_ratio_max_sum = 0.0;
    int vis_stat_window_count = 0;
    int vis_hit_count = 0;
    double vis_ratio_total_sum = 0.0;
    double vis_ratio_max_sum = 0.0;

    int16_t header_buf[kHeaderWindowSamples] = {0};
    int header_pos = 0;
    int header_fill = 0;
    int header_hop = 0;
    int16_t sync_buf[kSyncWindowSamples] = {0};
    int sync_pos = 0;
    int sync_fill = 0;
    int sync_hop = 0;
    int16_t pixel_buf[kMaxPixelSamples] = {0};
    int16_t header_window[kHeaderWindowSamples];
    int16_t sync_window[kSyncWindowSamples];
    int16_t pixel_window[kMaxPixelSamples];

    int64_t sample_index = 0;
    int64_t last_sync_index = -1000000;

    auto reset_header_state = [&]()
    {
        header_state = HeaderState::SeekLeader1;
        header_count = 0;
        vis_bit_index = 0;
        vis_value = 0;
        vis_ones = 0;
        vis_window_count = 0;
        vis_1100_count = 0;
        vis_1300_count = 0;
        vis_skip_windows = 0;
    };

    auto reset_pixel_state = [&]()
    {
        last_pixel = -1;
        pixel_pos = 0;
        pixel_fill = 0;
    };

    float audio_level = 0.0f;
    clear_frame();
    clear_accum();
    set_status(sstv::State::Waiting, 0, 0.0f, 0.0f, false);

    while (!s_stop)
    {
        int read_state = board->codec.read(reinterpret_cast<uint8_t*>(buffer),
                                           samples_per_read * sizeof(int16_t));
        if (read_state != 0)
        {
            vTaskDelay(pdMS_TO_TICKS(kTaskDelayMs));
            continue;
        }

        int16_t block_peak = 0;
        for (int i = 0; i < samples_per_block; ++i)
        {
            int32_t l = buffer[i * 2];
            int32_t r = buffer[i * 2 + 1];
            int32_t mono = (l + r) / 2;
            if (mono > block_peak)
            {
                block_peak = mono;
            }
            if (mono < -block_peak)
            {
                block_peak = static_cast<int16_t>(-mono);
            }

            header_buf[header_pos] = static_cast<int16_t>(mono);
            header_pos++;
            if (header_pos >= kHeaderWindowSamples)
            {
                header_pos = 0;
            }
            if (header_fill < kHeaderWindowSamples)
            {
                header_fill++;
            }

            sync_buf[sync_pos] = static_cast<int16_t>(mono);
            sync_pos++;
            if (sync_pos >= kSyncWindowSamples)
            {
                sync_pos = 0;
            }
            if (sync_fill < kSyncWindowSamples)
            {
                sync_fill++;
            }

            if (!header_ok && header_fill == kHeaderWindowSamples)
            {
                header_hop++;
                if (header_hop >= kHeaderHopSamples)
                {
                    header_hop = 0;
                    for (int j = 0; j < kHeaderWindowSamples; ++j)
                    {
                        int idx = header_pos + j;
                        if (idx >= kHeaderWindowSamples)
                        {
                            idx -= kHeaderWindowSamples;
                        }
                        header_window[j] = header_buf[idx];
                    }

                    float p1100 = goertzel_power(header_window, kHeaderWindowSamples, s_bin_1100);
                    float p1200 = goertzel_power(header_window, kHeaderWindowSamples, s_bin_1200);
                    float p1300 = goertzel_power(header_window, kHeaderWindowSamples, s_bin_1300);
                    float p1900 = goertzel_power(header_window, kHeaderWindowSamples, s_bin_1900);
                    Tone tone = detect_tone(p1100, p1200, p1300, p1900);

                    if (header_state == HeaderState::ReadVisBits)
                    {
                        if (vis_skip_windows > 0)
                        {
                            vis_skip_windows--;
                        }
                        else
                        {
                            if (tone == Tone::Tone1100)
                            {
                                vis_1100_count++;
                            }
                            else if (tone == Tone::Tone1300)
                            {
                                vis_1300_count++;
                            }
                            vis_window_count++;

                            if (vis_window_count >= vis_windows_per_bit)
                            {
                                int bit = -1;
                                if (vis_1100_count > vis_1300_count)
                                {
                                    bit = 1;
                                }
                                else if (vis_1300_count > vis_1100_count)
                                {
                                    bit = 0;
                                }

                                if (bit < 0)
                                {
                                    reset_header_state();
                                }
                                else
                                {
                                    if (vis_bit_index < 7)
                                    {
                                        if (bit)
                                        {
                                            vis_value |= static_cast<uint8_t>(1u << vis_bit_index);
                                            vis_ones++;
                                        }
                                        SSTV_LOG("[SSTV] VIS bit %d=%d\n", vis_bit_index, bit);
                                        vis_bit_index++;
                                    }
                                    else
                                    {
                                        int total_ones = vis_ones + (bit ? 1 : 0);
                                        bool parity_ok = (total_ones % 2) == 0;
                                        SSTV_LOG("[SSTV] VIS value=%u parity=%d\n",
                                                 static_cast<unsigned>(vis_value), parity_ok ? 1 : 0);
                                        if (parity_ok && vis_value == kVisScottie1)
                                        {
                                            header_ok = true;
                                            SSTV_LOG("[SSTV] VIS ok: Scottie 1\n");
                                        }
                                        reset_header_state();
                                    }
                                }

                                vis_window_count = 0;
                                vis_1100_count = 0;
                                vis_1300_count = 0;
                            }
                        }
                    }
                    else
                    {
                        if ((header_state == HeaderState::SeekBreak ||
                             header_state == HeaderState::SeekLeader2 ||
                             header_state == HeaderState::SeekVisStart) &&
                            (header_log_tick++ % header_log_every == 0))
                        {
                            SSTV_LOG("[SSTV] hdr state=%s tone=%s p1100=%.0f p1200=%.0f p1300=%.0f p1900=%.0f\n",
                                     header_state_name(header_state), tone_name(tone),
                                     static_cast<double>(p1100), static_cast<double>(p1200),
                                     static_cast<double>(p1300), static_cast<double>(p1900));
                        }

                        switch (header_state)
                        {
                        case HeaderState::SeekLeader1:
                            if (tone == Tone::Tone1900)
                            {
                                header_count++;
                                if (header_count >= leader_windows)
                                {
                                    header_state = HeaderState::SeekBreak;
                                    header_count = 0;
                                    SSTV_LOG("[SSTV] header leader1 ok\n");
                                }
                            }
                            else
                            {
                                header_count = 0;
                            }
                            break;
                        case HeaderState::SeekBreak:
                        {
                            bool break_hit = (tone == Tone::Tone1200);
                            float total = p1100 + p1200 + p1300 + p1900;
                            float max_other = p1300 > p1900 ? p1300 : p1900;
                            float ratio_total = total > 0.0f ? p1200 / total : 0.0f;
                            float ratio_max = max_other > 0.0f ? p1200 / max_other : 0.0f;
                            break_window_count++;
                            break_ratio_total_sum += ratio_total;
                            break_ratio_max_sum += ratio_max;
                            if (!break_hit)
                            {
                                break_hit = (total > 0.0f &&
                                             p1200 > total * 0.05f &&
                                             p1200 > max_other * 0.06f);
                                if (break_hit)
                                {
                                    SSTV_LOG("[SSTV] break fallback p1200=%.0f p1300=%.0f p1900=%.0f\n",
                                             static_cast<double>(p1200),
                                             static_cast<double>(p1300),
                                             static_cast<double>(p1900));
                                }
                                else if (header_log_tick % header_log_every == 0)
                                {
                                    SSTV_LOG("[SSTV] break miss r_total=%.3f r_max=%.3f\n",
                                             static_cast<double>(ratio_total),
                                             static_cast<double>(ratio_max));
                                }
                            }
                            if (break_hit)
                            {
                                break_hit_count++;
                                header_count++;
                                if (header_count >= break_windows)
                                {
                                    header_state = HeaderState::SeekLeader2;
                                    header_count = 0;
                                    SSTV_LOG("[SSTV] header break ok\n");
                                }
                            }
                            else if (tone == Tone::Tone1900)
                            {
                                header_state = HeaderState::SeekLeader1;
                                header_count = 1;
                            }
                            else
                            {
                                header_count = 0;
                            }
                            break;
                        }
                        case HeaderState::SeekLeader2:
                        {
                            float total = p1100 + p1200 + p1300 + p1900;
                            float max_other = p1100;
                            if (p1200 > max_other)
                            {
                                max_other = p1200;
                            }
                            if (p1300 > max_other)
                            {
                                max_other = p1300;
                            }
                            float ratio_total = total > 0.0f ? p1900 / total : 0.0f;
                            float ratio_max = max_other > 0.0f ? p1900 / max_other : 0.0f;
                            leader2_window_count++;
                            leader2_ratio_total_sum += ratio_total;
                            leader2_ratio_max_sum += ratio_max;
                            bool leader2_hit = (tone == Tone::Tone1900);
                            if (!leader2_hit)
                            {
                                leader2_hit = (total > 0.0f &&
                                               p1900 > total * 0.05f &&
                                               p1900 > max_other * 0.08f);
                                if (leader2_hit)
                                {
                                    SSTV_LOG("[SSTV] leader2 fallback p1900=%.0f p1200=%.0f p1300=%.0f\n",
                                             static_cast<double>(p1900),
                                             static_cast<double>(p1200),
                                             static_cast<double>(p1300));
                                }
                            }
                            if (leader2_hit)
                            {
                                header_count++;
                                leader2_hit_count++;
                                if (header_count >= leader_windows)
                                {
                                    header_state = HeaderState::SeekVisStart;
                                    header_count = 0;
                                    SSTV_LOG("[SSTV] header leader2 ok\n");
#if SSTV_DEBUG
                                    header_ok = true;
                                    SSTV_LOG("[SSTV] debug: force header ok after leader2\n");
#endif
                                }
                            }
                            else
                            {
                                header_state = HeaderState::SeekLeader1;
                                header_count = 0;
                            }
                            if (!leader2_hit && header_log_tick % header_log_every == 0)
                            {
                                SSTV_LOG("[SSTV] leader2 miss r_total=%.3f r_max=%.3f\n",
                                         static_cast<double>(ratio_total),
                                         static_cast<double>(ratio_max));
                            }
                            break;
                        }
                        case HeaderState::SeekVisStart:
                        {
                            bool vis_start = (tone == Tone::Tone1200);
                            float total = p1100 + p1200 + p1300 + p1900;
                            float max_other = p1300 > p1900 ? p1300 : p1900;
                            float ratio_total = total > 0.0f ? p1200 / total : 0.0f;
                            float ratio_max = max_other > 0.0f ? p1200 / max_other : 0.0f;
                            vis_stat_window_count++;
                            vis_ratio_total_sum += ratio_total;
                            vis_ratio_max_sum += ratio_max;
                            if (!vis_start)
                            {
                                vis_start = (total > 0.0f &&
                                             p1200 > total * 0.05f &&
                                             p1200 > max_other * 0.08f);
                                if (vis_start)
                                {
                                    SSTV_LOG("[SSTV] visstart fallback p1200=%.0f p1300=%.0f p1900=%.0f\n",
                                             static_cast<double>(p1200),
                                             static_cast<double>(p1300),
                                             static_cast<double>(p1900));
                                }
                                else if (header_log_tick % header_log_every == 0)
                                {
                                    SSTV_LOG("[SSTV] visstart miss r_total=%.3f r_max=%.3f\n",
                                             static_cast<double>(ratio_total),
                                             static_cast<double>(ratio_max));
                                }
                            }
                            if (vis_start)
                            {
                                vis_hit_count++;
                                header_state = HeaderState::ReadVisBits;
                                vis_skip_windows = vis_windows_per_bit;
                                vis_bit_index = 0;
                                vis_value = 0;
                                vis_ones = 0;
                                vis_window_count = 0;
                                vis_1100_count = 0;
                                vis_1300_count = 0;
                                SSTV_LOG("[SSTV] header VIS start\n");
                            }
                            else if (tone == Tone::Tone1900)
                            {
                                header_state = HeaderState::SeekLeader2;
                                header_count = 1;
                            }
                            else
                            {
                                header_state = HeaderState::SeekLeader1;
                                header_count = 0;
                            }
                            break;
                        }
                        case HeaderState::ReadVisBits:
                            break;
                        }

                        if (header_stat_tick++ % header_stat_every == 0)
                        {
                            double break_avg_total =
                                break_window_count > 0 ? break_ratio_total_sum / break_window_count : 0.0;
                            double break_avg_max =
                                break_window_count > 0 ? break_ratio_max_sum / break_window_count : 0.0;
                            double leader2_avg_total = leader2_window_count > 0
                                                           ? leader2_ratio_total_sum / leader2_window_count
                                                           : 0.0;
                            double leader2_avg_max = leader2_window_count > 0
                                                         ? leader2_ratio_max_sum / leader2_window_count
                                                         : 0.0;
                            double vis_avg_total = vis_stat_window_count > 0
                                                       ? vis_ratio_total_sum / vis_stat_window_count
                                                       : 0.0;
                            double vis_avg_max = vis_stat_window_count > 0
                                                     ? vis_ratio_max_sum / vis_stat_window_count
                                                     : 0.0;
                            SSTV_LOG("[SSTV] stat break=%d/%d avg(%.3f,%.3f) leader2=%d/%d avg(%.3f,%.3f) vis=%d/%d avg(%.3f,%.3f)\n",
                                     break_hit_count, break_window_count, break_avg_total,
                                     break_avg_max, leader2_hit_count, leader2_window_count,
                                     leader2_avg_total, leader2_avg_max, vis_hit_count,
                                     vis_stat_window_count, vis_avg_total, vis_avg_max);
                        }
                    }
                }
            }

            bool can_sync = header_ok || s_status.state == sstv::State::Receiving;
            if (can_sync && sync_fill == kSyncWindowSamples)
            {
                sync_hop++;
                if (sync_hop >= kSyncHopSamples)
                {
                    sync_hop = 0;
                    for (int j = 0; j < kSyncWindowSamples; ++j)
                    {
                        int idx = sync_pos + j;
                        if (idx >= kSyncWindowSamples)
                        {
                            idx -= kSyncWindowSamples;
                        }
                        sync_window[j] = sync_buf[idx];
                    }

                    float p1100 = goertzel_power(sync_window, kSyncWindowSamples, s_bin_1100);
                    float p1200 = goertzel_power(sync_window, kSyncWindowSamples, s_bin_1200);
                    float p1300 = goertzel_power(sync_window, kSyncWindowSamples, s_bin_1300);
                    float total = p1100 + p1200 + p1300;
                    float other_max = p1100 > p1300 ? p1100 : p1300;
                    bool sync_hit = (p1200 > other_max * kSyncToneDetectRatio &&
                                     p1200 > total * kSyncToneTotalRatio);

                    if (sync_hit && sample_index - last_sync_index > min_sync_gap)
                    {
                        last_sync_index = sample_index;
                        SSTV_LOG("[SSTV] sync hit @%lld line=%d state=%d\n",
                                 static_cast<long long>(sample_index), line_index,
                                 static_cast<int>(s_status.state));
                        if (s_status.state != sstv::State::Receiving)
                        {
                            line_index = 0;
                            clear_frame();
                            clear_saved_path();
                            s_pending_save = false;
                        }
                        clear_accum();
                        phase = Phase::Porch;
                        phase_samples = 0;
                        reset_pixel_state();
                        set_status(sstv::State::Receiving, static_cast<uint16_t>(line_index),
                                   static_cast<float>(line_index) / kInHeight,
                                   audio_level, true);
                    }
                }
            }

            if (s_status.state == sstv::State::Receiving && phase != Phase::Idle)
            {
                if (phase == Phase::Porch)
                {
                    phase_samples++;
                    if (phase_samples >= porch_samples)
                    {
                        phase = Phase::Green;
                        phase_samples = 0;
                        reset_pixel_state();
                    }
                }
                else
                {
                    int pixel = (phase_samples * kInWidth) / color_samples;
                    if (pixel >= 0 && pixel < kInWidth)
                    {
                        pixel_buf[pixel_pos] = static_cast<int16_t>(mono);
                        pixel_pos++;
                        if (pixel_pos >= pixel_window_samples)
                        {
                            pixel_pos = 0;
                        }
                        if (pixel_fill < pixel_window_samples)
                        {
                            pixel_fill++;
                        }

                        if (pixel != last_pixel && pixel_fill == pixel_window_samples)
                        {
                            last_pixel = pixel;
                            for (int j = 0; j < pixel_window_samples; ++j)
                            {
                                int idx = pixel_pos + j;
                                if (idx >= pixel_window_samples)
                                {
                                    idx -= pixel_window_samples;
                                }
                                pixel_window[j] = pixel_buf[idx];
                            }

                            float freq = estimate_freq_from_bins(pixel_window, pixel_window_samples,
                                                                 s_pixel_bins, kPixelBinCount);
                            uint8_t intensity = freq_to_intensity(freq);
                            int channel = 0;
                            if (phase == Phase::Green)
                            {
                                channel = 0;
                            }
                            else if (phase == Phase::Blue)
                            {
                                channel = 1;
                            }
                            else if (phase == Phase::Red)
                            {
                                channel = 2;
                            }
                            s_accum[channel][pixel] += intensity;
                            s_count[channel][pixel] += 1;
                        }
                    }
                    phase_samples++;
                    if (phase_samples >= color_samples)
                    {
                        phase_samples = 0;
                        if (phase == Phase::Green)
                        {
                            phase = Phase::Blue;
                            reset_pixel_state();
                        }
                        else if (phase == Phase::Blue)
                        {
                            phase = Phase::Red;
                            reset_pixel_state();
                        }
                        else if (phase == Phase::Red)
                        {
                            render_line(line_index);
                            SSTV_LOG("[SSTV] line %d done\n", line_index);
                            line_index++;
                            clear_accum();
                            if (line_index >= kInHeight)
                            {
                                set_status(sstv::State::Complete, kInHeight,
                                           1.0f, audio_level, true);
                                s_pending_save = true;
                                phase = Phase::Idle;
                                header_ok = false;
                                reset_header_state();
                                SSTV_LOG("[SSTV] complete\n");
                            }
                            else
                            {
                                set_status(sstv::State::Receiving,
                                           static_cast<uint16_t>(line_index),
                                           static_cast<float>(line_index) / kInHeight,
                                           audio_level, true);
                                phase = Phase::Idle;
                            }
                        }
                    }
                }
            }

            sample_index++;
        }

        audio_level = (audio_level * 0.8f) +
                      (static_cast<float>(block_peak) / 32767.0f) * 0.2f;
        if (audio_level < 0.0f)
        {
            audio_level = 0.0f;
        }
        if (audio_level > 1.0f)
        {
            audio_level = 1.0f;
        }
        if (s_pending_save)
        {
            save_frame_to_sd();
            s_pending_save = false;
        }

        if (s_status.state == sstv::State::Waiting)
        {
            set_status(sstv::State::Waiting, 0, 0.0f, audio_level, s_frame != nullptr);
        }
        else if (s_status.state == sstv::State::Receiving)
        {
            set_status(sstv::State::Receiving, static_cast<uint16_t>(line_index),
                       static_cast<float>(line_index) / kInHeight, audio_level, true);
        }
        else if (s_status.state == sstv::State::Complete)
        {
            set_status(sstv::State::Complete, kInHeight, 1.0f, audio_level, true);
        }
    }

    free(buffer);
    s_task = nullptr;
    if (s_codec_open)
    {
        board->codec.close();
        s_codec_open = false;
    }
    set_status(sstv::State::Idle, 0, 0.0f, 0.0f, s_frame != nullptr);
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

    if (!s_frame)
    {
        s_frame = static_cast<uint16_t*>(malloc(kOutWidth * kOutHeight * sizeof(uint16_t)));
        if (!s_frame)
        {
            set_error("No framebuffer");
            set_status(State::Error, 0, 0.0f, 0.0f, false);
            return false;
        }
    }
    clear_frame();
    clear_saved_path();
    s_pending_save = false;

    s_stop = false;
    s_active = true;
    set_status(State::Waiting, 0, 0.0f, 0.0f, true);

    BaseType_t ok = xTaskCreate(
        sstv_task,
        "sstv_rx",
        kTaskStack,
        nullptr,
        6,
        &s_task);
    if (ok != pdPASS)
    {
        s_task = nullptr;
        s_active = false;
        set_error("Task create failed");
        set_status(State::Error, 0, 0.0f, 0.0f, false);
        return false;
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
    for (int i = 0; i < 20; ++i)
    {
        if (!s_task)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_active = false;
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

void stop()
{
}

bool is_active()
{
    return false;
}

Status get_status()
{
    return {};
}

const char* get_last_error()
{
    return "SSTV not supported";
}

const char* get_last_saved_path()
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
