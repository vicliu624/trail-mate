#include "sstv_service.h"

#include <Arduino.h>

#if defined(ARDUINO_LILYGO_LORA_SX1262) && defined(USING_AUDIO_CODEC)

#include "../board/TLoRaPagerBoard.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <SD.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <ctime>

#include "sstv_config.h"
#include "sstv_dsp.h"
#include "sstv_header_detector.h"
#include "sstv_log.h"
#include "sstv_pixel_decoder.h"
#include "sstv_sync_detector.h"
#include "sstv_types.h"
#include "sstv_vis_decoder.h"

namespace
{
constexpr uint32_t kTaskStack = 12288;
constexpr uint32_t kTaskDelayMs = 2;

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t s_task = nullptr;
volatile bool s_stop = false;
bool s_active = false;
bool s_codec_open = false;

sstv::Status s_status;
char s_last_error[96] = {0};
char s_saved_path[64] = {0};
bool s_pending_save = false;

HeaderDetector s_header = {};
VisDecoder s_vis = {};
SyncDetector s_sync = {};

// TODO: add WAV replay regression tests for header/VIS/sync gating.

int16_t s_vis_preroll[kVisPrerollSamples] = {0};
int16_t s_vis_preroll_linear[kVisPrerollSamples] = {0};
int s_vis_preroll_pos = 0;
int s_vis_preroll_fill = 0;

#if SSTV_TONE_TEST
GoertzelBin s_tone_bin_1100 = {};
GoertzelBin s_tone_bin_1200 = {};
GoertzelBin s_tone_bin_1300 = {};
GoertzelBin s_tone_bin_1500 = {};
int16_t s_tone_buf[kToneWindowSamples] = {0};
int16_t s_tone_window[kToneWindowSamples] = {0};
#endif

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
    const uint16_t* frame = pixel_decoder_framebuffer();
    if (!frame)
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

    const uint32_t w = pixel_decoder_frame_width();
    const uint32_t h = pixel_decoder_frame_height();
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
        const uint16_t* row = frame + (h - 1 - y) * w;
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

void sstv_task(void*)
{
    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    if (!board)
    {
        set_error("Board not ready");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_active = false;
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    if (!(board->getDevicesProbe() & HW_CODEC_ONLINE))
    {
        set_error("Audio codec not ready");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_active = false;
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    if (board->codec.open(kBitsPerSample, kChannels, kSampleRate) != 0)
    {
        set_error("Codec open failed");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_active = false;
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
        s_active = false;
        s_codec_open = false;
        board->codec.close();
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    pixel_decoder_init();
    pixel_decoder_reset();
    pixel_decoder_clear_frame();
    if (!pixel_decoder_framebuffer())
    {
        set_error("No framebuffer");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_active = false;
        free(buffer);
        s_codec_open = false;
        board->codec.close();
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    header_detector_init(s_header);
    sync_detector_init(s_sync);

    const float header_window_ms =
        1000.0f * static_cast<float>(kHeaderHopSamples) / static_cast<float>(kSampleRate);
    int vis_windows_per_bit = static_cast<int>(kVisBitMs / header_window_ms + 0.5f);
    if (vis_windows_per_bit < 1)
    {
        vis_windows_per_bit = 1;
    }
    vis_decoder_init(s_vis, vis_windows_per_bit);

    header_detector_reset(s_header);
    sync_detector_reset(s_sync);
    vis_decoder_reset(s_vis);

    bool header_ok = false;
    s_vis_preroll_pos = 0;
    s_vis_preroll_fill = 0;
    const int64_t min_sync_gap =
        static_cast<int64_t>(kSampleRate * (kMinSyncGapMs / 1000.0f));
    int64_t last_sync_index = -min_sync_gap;
    int64_t sample_index = 0;
    int64_t low_audio_samples = 0;
    float audio_level = 0.0f;

#if SSTV_TONE_TEST
    s_tone_bin_1100 = make_bin(1100.0f);
    s_tone_bin_1200 = make_bin(1200.0f);
    s_tone_bin_1300 = make_bin(1300.0f);
    s_tone_bin_1500 = make_bin(1500.0f);
    int tone_pos = 0;
    int tone_fill = 0;
    int tone_blocks = 0;
    int tone_peak_max = 0;
    double tone_peak_sum = 0.0;
    double tone_p1100_sum = 0.0;
    double tone_p1200_sum = 0.0;
    double tone_p1300_sum = 0.0;
    double tone_p1500_sum = 0.0;
    double tone_p1200_max = 0.0;
    double tone_p1500_max = 0.0;
    int tone_sample_accum = 0;
#endif

    set_status(sstv::State::Waiting, 0, 0.0f, 0.0f, pixel_decoder_framebuffer() != nullptr);

    while (!s_stop)
    {
        int read_state = board->codec.read(reinterpret_cast<uint8_t*>(buffer),
                                           samples_per_read * sizeof(int16_t));
        if (read_state != 0)
        {
            vTaskDelay(pdMS_TO_TICKS(kTaskDelayMs));
            continue;
        }

        int block_peak = 0;

        for (int i = 0; i < samples_per_read; ++i)
        {
            int16_t mono = buffer[i];
            int sample_mag = mono < 0 ? -mono : mono;
            if (sample_mag > block_peak)
            {
                block_peak = sample_mag;
            }

            s_vis_preroll[s_vis_preroll_pos] = mono;
            s_vis_preroll_pos++;
            if (s_vis_preroll_pos >= kVisPrerollSamples)
            {
                s_vis_preroll_pos = 0;
            }
            if (s_vis_preroll_fill < kVisPrerollSamples)
            {
                s_vis_preroll_fill++;
            }

#if SSTV_TONE_TEST
            s_tone_buf[tone_pos] = mono;
            tone_pos++;
            if (tone_pos >= kToneWindowSamples)
            {
                tone_pos = 0;
            }
            if (tone_fill < kToneWindowSamples)
            {
                tone_fill++;
            }
#endif

            // Header/VIS gate: only scan when no mode locked yet.
            if (!header_ok)
            {
                HeaderResult header_result;
                bool hop_ready = header_detector_push_sample(s_header, mono, header_result);
                if (hop_ready && header_result.vis_start)
                {
                    for (int j = 0; j < s_vis_preroll_fill; ++j)
                    {
                        int idx = s_vis_preroll_pos + j;
                        if (idx >= kVisPrerollSamples)
                        {
                            idx -= kVisPrerollSamples;
                        }
                        s_vis_preroll_linear[j] = s_vis_preroll[idx];
                    }
                    vis_decoder_start_raw(s_vis, s_vis_preroll_linear, s_vis_preroll_fill,
                                          kVisStartHoldSamples);
                    SSTV_LOG_VIS("[SSTV] VIS raw collect=%d samples (bit=%.2fms)\n",
                                 s_vis.raw_needed,
                                 static_cast<double>(kVisBitSamples) * 1000.0 / kSampleRate);
                }

                if (vis_decoder_is_collecting(s_vis))
                {
                    VisDecodeResult vis_result;
                    if (vis_decoder_push_raw(s_vis, mono, vis_result) && vis_result.done)
                    {
                        if (vis_result.accepted && vis_result.info.mode != VisMode::Unknown)
                        {
                            pixel_decoder_set_mode(vis_result.info);
                            header_ok = true;
                            SSTV_LOG("[SSTV] VIS ok: %s (%s, color=%.3f ms)\n",
                                     vis_mode_name(vis_result.info.mode),
                                     vis_result.label ? vis_result.label : "?",
                                     static_cast<double>(vis_result.info.color_ms));
                        }
                        else
                        {
                            SSTV_LOG("[SSTV] VIS reject (raw) value=%u parity=%d avg=%.2f min=%.2f\n",
                                     static_cast<unsigned>(vis_result.value),
                                     vis_result.parity_ok ? 1 : 0,
                                     vis_result.valid_avg,
                                     vis_result.valid_min);
                        }

                        header_detector_reset(s_header);
                        vis_decoder_reset(s_vis);
                    }
                }
            }

            bool can_sync = header_ok || s_status.state == sstv::State::Receiving;
            if (sync_detector_push_sample(s_sync, mono, can_sync, sample_index, min_sync_gap,
                                          last_sync_index))
            {
                bool started = pixel_decoder_on_sync(s_status.state == sstv::State::Receiving);
                if (started)
                {
                    clear_saved_path();
                    s_pending_save = false;
                    const int line_index = pixel_decoder_line_index();
                    const int line_count = pixel_decoder_line_count();
                    set_status(sstv::State::Receiving, static_cast<uint16_t>(line_index),
                               line_count > 0 ? static_cast<float>(line_index) / line_count : 0.0f,
                               audio_level, true);
                }
            }

            if (s_status.state == sstv::State::Receiving)
            {
                pixel_decoder_step(mono);
                if (pixel_decoder_take_frame_done())
                {
                    const int line_count = pixel_decoder_line_count();
                    set_status(sstv::State::Complete, static_cast<uint16_t>(line_count), 1.0f,
                               audio_level, true);
                    s_pending_save = true;
                    header_ok = false;
                    header_detector_reset(s_header);
                    vis_decoder_reset(s_vis);
                }
            }

            sample_index++;
        }

#if SSTV_TONE_TEST
        if (tone_fill >= kToneWindowSamples)
        {
            tone_blocks++;
            for (int j = 0; j < kToneWindowSamples; ++j)
            {
                int idx = tone_pos + j;
                if (idx >= kToneWindowSamples)
                {
                    idx -= kToneWindowSamples;
                }
                s_tone_window[j] = s_tone_buf[idx];
            }
            double p1100 = goertzel_power(s_tone_window, kToneWindowSamples, s_tone_bin_1100);
            double p1200 = goertzel_power(s_tone_window, kToneWindowSamples, s_tone_bin_1200);
            double p1300 = goertzel_power(s_tone_window, kToneWindowSamples, s_tone_bin_1300);
            double p1500 = goertzel_power(s_tone_window, kToneWindowSamples, s_tone_bin_1500);
            tone_p1100_sum += p1100;
            tone_p1200_sum += p1200;
            tone_p1300_sum += p1300;
            tone_p1500_sum += p1500;
            if (p1200 > tone_p1200_max)
            {
                tone_p1200_max = p1200;
            }
            if (p1500 > tone_p1500_max)
            {
                tone_p1500_max = p1500;
            }
            tone_peak_sum += static_cast<double>(block_peak);
            if (block_peak > tone_peak_max)
            {
                tone_peak_max = block_peak;
            }
            tone_sample_accum += samples_per_block;
            if (tone_sample_accum >= static_cast<int>(kSampleRate))
            {
                const double peak_avg = tone_blocks > 0 ? tone_peak_sum / tone_blocks : 0.0;
                const double p1100_avg = tone_blocks > 0 ? tone_p1100_sum / tone_blocks : 0.0;
                const double p1200_avg = tone_blocks > 0 ? tone_p1200_sum / tone_blocks : 0.0;
                const double p1300_avg = tone_blocks > 0 ? tone_p1300_sum / tone_blocks : 0.0;
                const double p1500_avg = tone_blocks > 0 ? tone_p1500_sum / tone_blocks : 0.0;
                SSTV_LOG_TONE("[SSTV] tone_test p1200(cur=%.0f avg=%.0f max=%.0f) "
                              "p1500(cur=%.0f avg=%.0f max=%.0f) "
                              "p1100(avg=%.0f) p1300(avg=%.0f) "
                              "peak(cur=%d avg=%.0f max=%d)\n",
                              p1200, p1200_avg, tone_p1200_max,
                              p1500, p1500_avg, tone_p1500_max,
                              p1100_avg, p1300_avg,
                              static_cast<int>(block_peak), peak_avg, tone_peak_max);
                tone_sample_accum = 0;
                tone_blocks = 0;
                tone_peak_sum = 0.0;
                tone_peak_max = 0;
                tone_p1200_sum = 0.0;
                tone_p1200_max = 0.0;
                tone_p1500_sum = 0.0;
                tone_p1500_max = 0.0;
                tone_p1100_sum = 0.0;
                tone_p1300_sum = 0.0;
            }
        }
#endif

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
        pixel_decoder_set_audio_level(audio_level);

        if (s_pending_save)
        {
            save_frame_to_sd();
            s_pending_save = false;
        }

        if (audio_level < kAudioResetLevel)
        {
            low_audio_samples += samples_per_block;
        }
        else
        {
            low_audio_samples = 0;
        }

        if (s_status.state == sstv::State::Receiving)
        {
            const int64_t sync_timeout_samples =
                static_cast<int64_t>(kSampleRate * (kSyncTimeoutMs / 1000.0f));
            const int64_t audio_timeout_samples =
                static_cast<int64_t>(kSampleRate * (kAudioResetHoldMs / 1000.0f));
            bool should_reset = false;
            if (sample_index - last_sync_index > sync_timeout_samples)
            {
                SSTV_LOG("[SSTV] reset: sync timeout %.0f ms\n",
                         static_cast<double>((sample_index - last_sync_index) * 1000.0 /
                                             static_cast<double>(kSampleRate)));
                should_reset = true;
            }
            else if (low_audio_samples > audio_timeout_samples)
            {
                SSTV_LOG("[SSTV] reset: low audio %.0f ms (level=%.3f)\n",
                         static_cast<double>(low_audio_samples * 1000.0 /
                                             static_cast<double>(kSampleRate)),
                         static_cast<double>(audio_level));
                should_reset = true;
            }

            if (should_reset)
            {
                header_ok = false;
                header_detector_reset(s_header);
                vis_decoder_reset(s_vis);
                pixel_decoder_reset();
                s_pending_save = false;
                set_status(sstv::State::Waiting, 0, 0.0f, audio_level,
                           pixel_decoder_framebuffer() != nullptr);
            }
        }

        if (s_status.state == sstv::State::Waiting)
        {
            set_status(sstv::State::Waiting, 0, 0.0f, audio_level,
                       pixel_decoder_framebuffer() != nullptr);
        }
        else if (s_status.state == sstv::State::Receiving)
        {
            const int line_index = pixel_decoder_line_index();
            const int line_count = pixel_decoder_line_count();
            set_status(sstv::State::Receiving, static_cast<uint16_t>(line_index),
                       line_count > 0 ? static_cast<float>(line_index) / line_count : 0.0f,
                       audio_level, true);
        }
        else if (s_status.state == sstv::State::Complete)
        {
            const int line_count = pixel_decoder_line_count();
            set_status(sstv::State::Complete, static_cast<uint16_t>(line_count), 1.0f,
                       audio_level, true);
        }
    }

    free(buffer);
    s_task = nullptr;
    if (s_codec_open)
    {
        board->codec.close();
        s_codec_open = false;
    }
    s_active = false;
    set_status(sstv::State::Idle, 0, 0.0f, 0.0f, pixel_decoder_framebuffer() != nullptr);
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

    pixel_decoder_init();
    if (!pixel_decoder_framebuffer())
    {
        set_error("No framebuffer");
        set_status(State::Error, 0, 0.0f, 0.0f, false);
        return false;
    }

    pixel_decoder_reset();
    pixel_decoder_clear_frame();
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
    return vis_mode_name(pixel_decoder_mode());
}

const uint16_t* get_framebuffer()
{
    return pixel_decoder_framebuffer();
}

uint16_t frame_width()
{
    return pixel_decoder_frame_width();
}

uint16_t frame_height()
{
    return pixel_decoder_frame_height();
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

const char* get_mode_name()
{
    return "Unknown";
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
