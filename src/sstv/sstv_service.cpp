#include "sstv_service.h"

#if defined(ARDUINO_LILYGO_LORA_SX1262) && defined(USING_AUDIO_CODEC)

#include "../board/TLoRaPagerBoard.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino.h>
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

constexpr float kSyncMinMs = 7.0f;
constexpr float kSyncMaxMs = 13.0f;
constexpr float kPorchMs = 1.5f;
constexpr float kColorMs = 138.24f;

constexpr int kInWidth = 320;
constexpr int kInHeight = 256;
constexpr int kOutWidth = 288;
constexpr int kOutHeight = 192;
constexpr int kOutImageWidth = 240;
constexpr int kPadX = (kOutWidth - kOutImageWidth) / 2;

constexpr uint32_t kPanelBg = 0xFAF0D8;

constexpr float kSyncFreqMin = 1100.0f;
constexpr float kSyncFreqMax = 1300.0f;
constexpr float kFreqMin = 1500.0f;
constexpr float kFreqMax = 2300.0f;
constexpr float kFreqSpan = kFreqMax - kFreqMin;

constexpr float kMinSyncGapMs = 100.0f;

enum class Phase : uint8_t
{
    Idle = 0,
    Porch,
    Green,
    Blue,
    Red,
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

    const int sync_min_samples = static_cast<int>(kSampleRate * (kSyncMinMs / 1000.0f));
    const int sync_max_samples = static_cast<int>(kSampleRate * (kSyncMaxMs / 1000.0f));
    const int porch_samples = static_cast<int>(kSampleRate * (kPorchMs / 1000.0f));
    const int color_samples = static_cast<int>(kSampleRate * (kColorMs / 1000.0f));
    const int min_sync_gap = static_cast<int>(kSampleRate * (kMinSyncGapMs / 1000.0f));

    int line_index = 0;
    Phase phase = Phase::Idle;
    int phase_samples = 0;

    float current_freq = 1500.0f;
    int16_t prev_sample = 0;
    int64_t last_cross = -1;
    int64_t sample_index = 0;
    int sync_samples = 0;
    bool sync_active = false;
    int64_t last_sync_index = -1000000;

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

            if (prev_sample <= 0 && mono > 0)
            {
                if (last_cross >= 0)
                {
                    int64_t period = sample_index - last_cross;
                    if (period > 0)
                    {
                        float freq = static_cast<float>(kSampleRate) / static_cast<float>(period);
                        if (freq > 800.0f && freq < 3000.0f)
                        {
                            current_freq = (current_freq * 0.7f) + (freq * 0.3f);
                        }
                    }
                }
                last_cross = sample_index;
            }
            prev_sample = static_cast<int16_t>(mono);

            bool in_sync = (current_freq >= kSyncFreqMin && current_freq <= kSyncFreqMax);
            if (in_sync)
            {
                sync_samples++;
                sync_active = true;
            }
            else if (sync_active)
            {
                if (sync_samples >= sync_min_samples && sync_samples <= sync_max_samples)
                {
                    if (sample_index - last_sync_index > min_sync_gap)
                    {
                        last_sync_index = sample_index;
                        if (s_status.state != sstv::State::Receiving)
                        {
                            line_index = 0;
                            clear_frame();
                        }
                        clear_accum();
                        clear_saved_path();
                        s_pending_save = false;
                        phase = Phase::Porch;
                        phase_samples = 0;
                        set_status(sstv::State::Receiving, static_cast<uint16_t>(line_index),
                                   static_cast<float>(line_index) / kInHeight,
                                   audio_level, true);
                    }
                }
                sync_samples = 0;
                sync_active = false;
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
                    }
                }
                else
                {
                    int pixel = (phase_samples * kInWidth) / color_samples;
                    if (pixel >= 0 && pixel < kInWidth)
                    {
                        uint8_t intensity = freq_to_intensity(current_freq);
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
                    phase_samples++;
                    if (phase_samples >= color_samples)
                    {
                        phase_samples = 0;
                        if (phase == Phase::Green)
                        {
                            phase = Phase::Blue;
                        }
                        else if (phase == Phase::Blue)
                        {
                            phase = Phase::Red;
                        }
                        else if (phase == Phase::Red)
                        {
                            render_line(line_index);
                            line_index++;
                            clear_accum();
                            if (line_index >= kInHeight)
                            {
                                set_status(sstv::State::Complete, kInHeight,
                                           1.0f, audio_level, true);
                                s_pending_save = true;
                                phase = Phase::Idle;
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
