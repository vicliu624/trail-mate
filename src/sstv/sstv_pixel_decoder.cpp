#include "sstv_pixel_decoder.h"

#include <cmath>
#include <cstring>
#include <cstdlib>

#include "sstv_config.h"
#include "sstv_dsp.h"
#include "sstv_log.h"

namespace
{
enum class Phase : uint8_t
{
    Idle = 0,
    Porch1,
    Green,
    Porch2,
    Blue,
    Sync,
    Porch3,
    Red,
    RobotSync,
    RobotPorch1,
    RobotY,
    RobotSep1,
    RobotPorch2,
    RobotRY,
    RobotSep2,
    RobotPorch3,
    RobotBY,
    MartinSync,
    MartinPorch,
    MartinGreen,
    MartinSep1,
    MartinBlue,
    MartinSep2,
    MartinRed,
    MartinSep3,
    PdSync,
    PdPorch,
    PdY1,
    PdRY,
    PdBY,
    PdY2,
    PSync,
    PPorch1,
    PRed,
    PPorch2,
    PGreen,
    PPorch3,
    PBlue,
    PPorch4,
};

VisMode s_vis_mode = VisMode::Unknown;
int s_line_count = kInHeightScottie;
int s_line_index = 0;
Phase s_phase = Phase::Idle;
int s_phase_samples = 0;
int s_last_pixel = -1;
int s_pixel_pos = 0;
int s_pixel_fill = 0;
float s_audio_level = 0.0f;
bool s_frame_done = false;

uint16_t* s_frame = nullptr;
int s_last_output_y = -1;

uint32_t s_accum[3][kInWidth];
uint16_t s_count[3][kInWidth];
uint8_t s_last_ry[kInWidth];
uint8_t s_last_by[kInWidth];
bool s_has_ry = false;
bool s_has_by = false;
uint8_t s_pd_y1[kInWidth];
bool s_pd_has_y1 = false;
bool s_pd_use_y1 = false;

GoertzelBin s_pixel_bins[kPixelBinCount] = {};
int16_t s_pixel_buf[kMaxPixelSamples] = {0};
int16_t s_pixel_window[kMaxPixelSamples] = {0};

int s_porch_samples = 0;
int s_sync_samples = 0;
int s_color_samples = 0;
int s_pixel_window_samples = 0;
int s_robot_sync_samples = 0;
int s_robot_sync_porch_samples = 0;
int s_robot_sep_samples = 0;
int s_robot_porch_samples = 0;
int s_robot_y_samples = 0;
int s_robot_chroma_samples = 0;
int s_robot36_y_samples = 0;
int s_robot36_chroma_samples = 0;
int s_robot_pixel_window_y = 0;
int s_robot_pixel_window_c = 0;
int s_robot36_pixel_window_y = 0;
int s_robot36_pixel_window_c = 0;
int s_martin_sync_samples = 0;
int s_martin_porch_samples = 0;
int s_pd_sync_samples = 0;
int s_pd_porch_samples = 0;
int s_pd_scan_samples = 0;
int s_pd_pixel_window_samples = 0;
int s_p_sync_samples = 0;
int s_p_porch_samples = 0;
int s_p_color_samples = 0;
int s_p_pixel_window_samples = 0;

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

void clear_accum()
{
    memset(s_accum, 0, sizeof(s_accum));
    memset(s_count, 0, sizeof(s_count));
}

void clear_robot_chroma()
{
    memset(s_last_ry, 128, sizeof(s_last_ry));
    memset(s_last_by, 128, sizeof(s_last_by));
    s_has_ry = false;
    s_has_by = false;
}

void clear_pd_state()
{
    memset(s_pd_y1, 0, sizeof(s_pd_y1));
    s_pd_has_y1 = false;
    s_pd_use_y1 = false;
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

void render_line(int line)
{
    if (!s_frame)
    {
        return;
    }
    int out_y = (line * kOutHeight) / s_line_count;
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
        if (s_vis_mode == VisMode::Robot72 || s_vis_mode == VisMode::Robot36 ||
            s_vis_mode == VisMode::Pd50 || s_vis_mode == VisMode::Pd90 ||
            s_vis_mode == VisMode::Pd120 || s_vis_mode == VisMode::Pd160 ||
            s_vis_mode == VisMode::Pd180 || s_vis_mode == VisMode::Pd240 ||
            s_vis_mode == VisMode::Pd290)
        {
            int y = 0;
            if (s_vis_mode == VisMode::Pd50 || s_vis_mode == VisMode::Pd90 ||
                s_vis_mode == VisMode::Pd120 || s_vis_mode == VisMode::Pd160 ||
                s_vis_mode == VisMode::Pd180 || s_vis_mode == VisMode::Pd240 ||
                s_vis_mode == VisMode::Pd290)
            {
                if (s_pd_use_y1 && s_pd_has_y1)
                {
                    y = s_pd_y1[in_x];
                }
                else
                {
                    y = s_count[0][in_x]
                            ? static_cast<int>(s_accum[0][in_x] / s_count[0][in_x])
                            : 0;
                }
            }
            else
            {
                y = s_count[0][in_x] ? static_cast<int>(s_accum[0][in_x] / s_count[0][in_x])
                                     : 0;
            }
            int ry = 128;
            int by = 128;
            if (s_vis_mode == VisMode::Robot72)
            {
                ry = s_count[1][in_x] ? static_cast<int>(s_accum[1][in_x] / s_count[1][in_x])
                                      : 128;
                by = s_count[2][in_x] ? static_cast<int>(s_accum[2][in_x] / s_count[2][in_x])
                                      : 128;
            }
            else
            {
                ry = s_has_ry ? s_last_ry[in_x] : 128;
                by = s_has_by ? s_last_by[in_x] : 128;
            }
            int r_val = y + (ry - 128);
            int b_val = y + (by - 128);
            float g_val_f = (static_cast<float>(y) - 0.299f * r_val - 0.114f * b_val) / 0.587f;
            int g_val = static_cast<int>(g_val_f + 0.5f);
            r = clamp_u8(r_val);
            g = clamp_u8(g_val);
            b = clamp_u8(b_val);
        }
        else
        {
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
        }

        row[kPadX + out_x] = rgb_to_565(r, g, b);
    }
}

int calc_pixel_window_samples(int color_samples_in)
{
    int samples = color_samples_in / kInWidth;
    if (samples < 8)
    {
        samples = 8;
    }
    if (samples > kMaxPixelSamples)
    {
        samples = kMaxPixelSamples;
    }
    return samples;
}

void update_color_timing(float color_ms, int& color_samples, int& pixel_window_samples)
{
    color_samples = static_cast<int>(kSampleRate * (color_ms / 1000.0f));
    pixel_window_samples = calc_pixel_window_samples(color_samples);
}

void update_pd_timing(float scan_ms, int& pd_scan_samples, int& pd_pixel_window_samples)
{
    pd_scan_samples = static_cast<int>(kSampleRate * (scan_ms / 1000.0f));
    pd_pixel_window_samples = calc_pixel_window_samples(pd_scan_samples);
}

void update_p_timing(float sync_ms, float porch_ms, float color_ms_in,
                     int& p_sync_samples, int& p_porch_samples, int& p_color_samples,
                     int& p_pixel_window_samples)
{
    p_sync_samples = static_cast<int>(kSampleRate * (sync_ms / 1000.0f));
    p_porch_samples = static_cast<int>(kSampleRate * (porch_ms / 1000.0f));
    p_color_samples = static_cast<int>(kSampleRate * (color_ms_in / 1000.0f));
    p_pixel_window_samples = calc_pixel_window_samples(p_color_samples);
}

void reset_pixel_state()
{
    s_last_pixel = -1;
    s_pixel_pos = 0;
    s_pixel_fill = 0;
}

void mark_frame_done()
{
    s_frame_done = true;
    s_phase = Phase::Idle;
    s_phase_samples = 0;
}

struct ModeStrategy
{
    const char* name;
    void (*step)(int32_t mono);
};

void step_robot_mode(int32_t mono);
void step_martin_mode(int32_t mono);
void step_pd_mode(int32_t mono);
void step_p_mode(int32_t mono);
void step_scottie_mode(int32_t mono);

const ModeStrategy& select_mode_strategy(VisMode mode)
{
    static const ModeStrategy kRobot = {"robot", step_robot_mode};
    static const ModeStrategy kMartin = {"martin", step_martin_mode};
    static const ModeStrategy kPd = {"pd", step_pd_mode};
    static const ModeStrategy kP = {"p", step_p_mode};
    static const ModeStrategy kScottie = {"scottie", step_scottie_mode};

    switch (mode)
    {
    case VisMode::Robot72:
    case VisMode::Robot36:
        return kRobot;
    case VisMode::Martin1:
    case VisMode::Martin2:
        return kMartin;
    case VisMode::Pd50:
    case VisMode::Pd90:
    case VisMode::Pd120:
    case VisMode::Pd160:
    case VisMode::Pd180:
    case VisMode::Pd240:
    case VisMode::Pd290:
        return kPd;
    case VisMode::P3:
    case VisMode::P5:
    case VisMode::P7:
        return kP;
    default:
        return kScottie;
    }
}

void step_robot_mode(int32_t mono)
{
    const bool robot36 = (s_vis_mode == VisMode::Robot36);
    const bool even_line = (s_line_index % 2) == 0;
    if (s_phase == Phase::RobotSync)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_robot_sync_samples)
        {
            s_phase = Phase::RobotPorch1;
            s_phase_samples = 0;
        }
    }
    else if (s_phase == Phase::RobotPorch1)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_robot_sync_porch_samples)
        {
            s_phase = Phase::RobotY;
            s_phase_samples = 0;
            reset_pixel_state();
        }
    }
    else if (s_phase == Phase::RobotSep1)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_robot_sep_samples)
        {
            s_phase = Phase::RobotPorch2;
            s_phase_samples = 0;
        }
    }
    else if (s_phase == Phase::RobotPorch2)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_robot_porch_samples)
        {
            if (robot36)
            {
                s_phase = even_line ? Phase::RobotRY : Phase::RobotBY;
            }
            else
            {
                s_phase = Phase::RobotRY;
            }
            s_phase_samples = 0;
            reset_pixel_state();
        }
    }
    else if (!robot36 && s_phase == Phase::RobotSep2)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_robot_sep_samples)
        {
            s_phase = Phase::RobotPorch3;
            s_phase_samples = 0;
        }
    }
    else if (!robot36 && s_phase == Phase::RobotPorch3)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_robot_porch_samples)
        {
            s_phase = Phase::RobotBY;
            s_phase_samples = 0;
            reset_pixel_state();
        }
    }
    else
    {
        const bool y_phase = (s_phase == Phase::RobotY);
        const int active_color_samples =
            y_phase ? (robot36 ? s_robot36_y_samples : s_robot_y_samples)
                    : (robot36 ? s_robot36_chroma_samples : s_robot_chroma_samples);
        const int active_pixel_window =
            y_phase ? (robot36 ? s_robot36_pixel_window_y : s_robot_pixel_window_y)
                    : (robot36 ? s_robot36_pixel_window_c : s_robot_pixel_window_c);
        int pixel = (s_phase_samples * kInWidth) / active_color_samples;
        if (pixel >= 0 && pixel < kInWidth)
        {
            s_pixel_buf[s_pixel_pos] = static_cast<int16_t>(mono);
            s_pixel_pos++;
            if (s_pixel_pos >= active_pixel_window)
            {
                s_pixel_pos = 0;
            }
            if (s_pixel_fill < active_pixel_window)
            {
                s_pixel_fill++;
            }

            if (pixel != s_last_pixel && s_pixel_fill == active_pixel_window)
            {
                s_last_pixel = pixel;
                for (int j = 0; j < active_pixel_window; ++j)
                {
                    int idx = s_pixel_pos + j;
                    if (idx >= active_pixel_window)
                    {
                        idx -= active_pixel_window;
                    }
                    s_pixel_window[j] = s_pixel_buf[idx];
                }

                float freq = estimate_freq_from_bins(s_pixel_window, active_pixel_window,
                                                     s_pixel_bins, kPixelBinCount);
                uint8_t intensity = freq_to_intensity(freq);
                int channel = 0;
                if (s_phase == Phase::RobotY)
                {
                    channel = 0;
                }
                else if (s_phase == Phase::RobotRY)
                {
                    channel = 1;
                }
                else if (s_phase == Phase::RobotBY)
                {
                    channel = 2;
                }
                s_accum[channel][pixel] += intensity;
                s_count[channel][pixel] += 1;
            }
        }
        s_phase_samples++;
        if (s_phase_samples >= active_color_samples)
        {
            s_phase_samples = 0;
            if (s_phase == Phase::RobotY)
            {
                s_phase = Phase::RobotSep1;
            }
            else if (s_phase == Phase::RobotRY)
            {
                if (robot36)
                {
                    // Robot36: even line carries R-Y, odd line carries B-Y.
                    for (int x = 0; x < kInWidth; ++x)
                    {
                        if (s_count[1][x])
                        {
                            s_last_ry[x] = static_cast<uint8_t>(s_accum[1][x] / s_count[1][x]);
                        }
                    }
                    s_has_ry = true;
                    render_line(s_line_index);
                    s_line_index++;
                    clear_accum();
                    if (s_line_index >= s_line_count)
                    {
                        mark_frame_done();
                    }
                    else
                    {
                        s_phase = Phase::RobotSync;
                        s_phase_samples = 0;
                        reset_pixel_state();
                    }
                }
                else
                {
                    s_phase = Phase::RobotSep2;
                }
            }
            else if (s_phase == Phase::RobotBY)
            {
                if (robot36)
                {
                    for (int x = 0; x < kInWidth; ++x)
                    {
                        if (s_count[2][x])
                        {
                            s_last_by[x] = static_cast<uint8_t>(s_accum[2][x] / s_count[2][x]);
                        }
                    }
                    s_has_by = true;
                }
                render_line(s_line_index);
                s_line_index++;
                clear_accum();
                if (s_line_index >= s_line_count)
                {
                    mark_frame_done();
                }
                else
                {
                    s_phase = Phase::RobotSync;
                    s_phase_samples = 0;
                    reset_pixel_state();
                }
            }
        }
    }
}

void step_martin_mode(int32_t mono)
{
    if (s_phase == Phase::MartinSync)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_martin_sync_samples)
        {
            s_phase = Phase::MartinPorch;
            s_phase_samples = 0;
        }
    }
    else if (s_phase == Phase::MartinPorch)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_martin_porch_samples)
        {
            s_phase = Phase::MartinGreen;
            s_phase_samples = 0;
            reset_pixel_state();
        }
    }
    else if (s_phase == Phase::MartinSep1)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_martin_porch_samples)
        {
            s_phase = Phase::MartinBlue;
            s_phase_samples = 0;
            reset_pixel_state();
        }
    }
    else if (s_phase == Phase::MartinSep2)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_martin_porch_samples)
        {
            s_phase = Phase::MartinRed;
            s_phase_samples = 0;
            reset_pixel_state();
        }
    }
    else if (s_phase == Phase::MartinSep3)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_martin_porch_samples)
        {
            s_phase = Phase::MartinSync;
            s_phase_samples = 0;
        }
    }
    else
    {
        int pixel = (s_phase_samples * kInWidth) / s_color_samples;
        if (pixel >= 0 && pixel < kInWidth)
        {
            s_pixel_buf[s_pixel_pos] = static_cast<int16_t>(mono);
            s_pixel_pos++;
            if (s_pixel_pos >= s_pixel_window_samples)
            {
                s_pixel_pos = 0;
            }
            if (s_pixel_fill < s_pixel_window_samples)
            {
                s_pixel_fill++;
            }

            if (pixel != s_last_pixel && s_pixel_fill == s_pixel_window_samples)
            {
                s_last_pixel = pixel;
                for (int j = 0; j < s_pixel_window_samples; ++j)
                {
                    int idx = s_pixel_pos + j;
                    if (idx >= s_pixel_window_samples)
                    {
                        idx -= s_pixel_window_samples;
                    }
                    s_pixel_window[j] = s_pixel_buf[idx];
                }

                float freq = estimate_freq_from_bins(s_pixel_window, s_pixel_window_samples,
                                                     s_pixel_bins, kPixelBinCount);
                uint8_t intensity = freq_to_intensity(freq);
                int channel = 0;
                if (s_phase == Phase::MartinGreen)
                {
                    channel = 0;
                }
                else if (s_phase == Phase::MartinBlue)
                {
                    channel = 1;
                }
                else if (s_phase == Phase::MartinRed)
                {
                    channel = 2;
                }
                s_accum[channel][pixel] += intensity;
                s_count[channel][pixel] += 1;
            }
        }
        s_phase_samples++;
        if (s_phase_samples >= s_color_samples)
        {
            s_phase_samples = 0;
            if (s_phase == Phase::MartinGreen)
            {
                s_phase = Phase::MartinSep1;
            }
            else if (s_phase == Phase::MartinBlue)
            {
                s_phase = Phase::MartinSep2;
            }
            else if (s_phase == Phase::MartinRed)
            {
                render_line(s_line_index);
                s_line_index++;
                clear_accum();
                if (s_line_index >= s_line_count)
                {
                    mark_frame_done();
                }
                else
                {
                    s_phase = Phase::MartinSep3;
                    s_phase_samples = 0;
                    reset_pixel_state();
                }
            }
        }
    }
}

void step_pd_mode(int32_t mono)
{
    if (s_phase == Phase::PdSync)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_pd_sync_samples)
        {
            s_phase = Phase::PdPorch;
            s_phase_samples = 0;
        }
    }
    else if (s_phase == Phase::PdPorch)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_pd_porch_samples)
        {
            s_phase = Phase::PdY1;
            s_phase_samples = 0;
            reset_pixel_state();
        }
    }
    else
    {
        const bool y_phase = (s_phase == Phase::PdY1 || s_phase == Phase::PdY2);
        int pixel = (s_phase_samples * kInWidth) / s_pd_scan_samples;
        if (pixel >= 0 && pixel < kInWidth)
        {
            s_pixel_buf[s_pixel_pos] = static_cast<int16_t>(mono);
            s_pixel_pos++;
            if (s_pixel_pos >= s_pd_pixel_window_samples)
            {
                s_pixel_pos = 0;
            }
            if (s_pixel_fill < s_pd_pixel_window_samples)
            {
                s_pixel_fill++;
            }

            if (pixel != s_last_pixel && s_pixel_fill == s_pd_pixel_window_samples)
            {
                s_last_pixel = pixel;
                for (int j = 0; j < s_pd_pixel_window_samples; ++j)
                {
                    int idx = s_pixel_pos + j;
                    if (idx >= s_pd_pixel_window_samples)
                    {
                        idx -= s_pd_pixel_window_samples;
                    }
                    s_pixel_window[j] = s_pixel_buf[idx];
                }

                float freq = estimate_freq_from_bins(s_pixel_window, s_pd_pixel_window_samples,
                                                     s_pixel_bins, kPixelBinCount);
                uint8_t intensity = freq_to_intensity(freq);
                int channel = 0;
                if (y_phase)
                {
                    channel = 0;
                }
                else if (s_phase == Phase::PdRY)
                {
                    channel = 1;
                }
                else if (s_phase == Phase::PdBY)
                {
                    channel = 2;
                }
                s_accum[channel][pixel] += intensity;
                s_count[channel][pixel] += 1;
            }
        }
        s_phase_samples++;
        if (s_phase_samples >= s_pd_scan_samples)
        {
            s_phase_samples = 0;
            if (s_phase == Phase::PdY1)
            {
                for (int x = 0; x < kInWidth; ++x)
                {
                    s_pd_y1[x] = s_count[0][x]
                                     ? static_cast<uint8_t>(s_accum[0][x] / s_count[0][x])
                                     : 0;
                }
                s_pd_has_y1 = true;
                clear_accum();
                s_phase = Phase::PdRY;
                reset_pixel_state();
            }
            else if (s_phase == Phase::PdRY)
            {
                for (int x = 0; x < kInWidth; ++x)
                {
                    s_last_ry[x] = s_count[1][x]
                                       ? static_cast<uint8_t>(s_accum[1][x] / s_count[1][x])
                                       : 128;
                }
                s_has_ry = true;
                clear_accum();
                s_phase = Phase::PdBY;
                reset_pixel_state();
            }
            else if (s_phase == Phase::PdBY)
            {
                for (int x = 0; x < kInWidth; ++x)
                {
                    s_last_by[x] = s_count[2][x]
                                       ? static_cast<uint8_t>(s_accum[2][x] / s_count[2][x])
                                       : 128;
                }
                s_has_by = true;
                clear_accum();

                if (s_pd_has_y1)
                {
                    s_pd_use_y1 = true;
                    render_line(s_line_index);
                    s_line_index++;
                    if (s_line_index >= s_line_count)
                    {
                        mark_frame_done();
                        return;
                    }
                }

                s_phase = Phase::PdY2;
                reset_pixel_state();
            }
            else if (s_phase == Phase::PdY2)
            {
                s_pd_use_y1 = false;
                render_line(s_line_index);
                s_line_index++;
                clear_accum();
                s_pd_has_y1 = false;
                if (s_line_index >= s_line_count)
                {
                    mark_frame_done();
                }
                else
                {
                    s_phase = Phase::PdSync;
                    s_phase_samples = 0;
                    reset_pixel_state();
                }
            }
        }
    }
}

void step_p_mode(int32_t mono)
{
    if (s_phase == Phase::PSync)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_p_sync_samples)
        {
            s_phase = Phase::PPorch1;
            s_phase_samples = 0;
        }
    }
    else if (s_phase == Phase::PPorch1)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_p_porch_samples)
        {
            s_phase = Phase::PRed;
            s_phase_samples = 0;
            reset_pixel_state();
        }
    }
    else if (s_phase == Phase::PPorch2)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_p_porch_samples)
        {
            s_phase = Phase::PGreen;
            s_phase_samples = 0;
            reset_pixel_state();
        }
    }
    else if (s_phase == Phase::PPorch3)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_p_porch_samples)
        {
            s_phase = Phase::PBlue;
            s_phase_samples = 0;
            reset_pixel_state();
        }
    }
    else if (s_phase == Phase::PPorch4)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_p_porch_samples)
        {
            render_line(s_line_index);
            s_line_index++;
            clear_accum();
            if (s_line_index >= s_line_count)
            {
                mark_frame_done();
            }
            else
            {
                s_phase = Phase::PSync;
                s_phase_samples = 0;
                reset_pixel_state();
            }
        }
    }
    else
    {
        int pixel = (s_phase_samples * kInWidth) / s_p_color_samples;
        if (pixel >= 0 && pixel < kInWidth)
        {
            s_pixel_buf[s_pixel_pos] = static_cast<int16_t>(mono);
            s_pixel_pos++;
            if (s_pixel_pos >= s_p_pixel_window_samples)
            {
                s_pixel_pos = 0;
            }
            if (s_pixel_fill < s_p_pixel_window_samples)
            {
                s_pixel_fill++;
            }

            if (pixel != s_last_pixel && s_pixel_fill == s_p_pixel_window_samples)
            {
                s_last_pixel = pixel;
                for (int j = 0; j < s_p_pixel_window_samples; ++j)
                {
                    int idx = s_pixel_pos + j;
                    if (idx >= s_p_pixel_window_samples)
                    {
                        idx -= s_p_pixel_window_samples;
                    }
                    s_pixel_window[j] = s_pixel_buf[idx];
                }

                float freq = estimate_freq_from_bins(s_pixel_window, s_p_pixel_window_samples,
                                                     s_pixel_bins, kPixelBinCount);
                uint8_t intensity = freq_to_intensity(freq);
                int channel = 0;
                if (s_phase == Phase::PRed)
                {
                    channel = 2;
                }
                else if (s_phase == Phase::PGreen)
                {
                    channel = 0;
                }
                else if (s_phase == Phase::PBlue)
                {
                    channel = 1;
                }
                s_accum[channel][pixel] += intensity;
                s_count[channel][pixel] += 1;
            }
        }
        s_phase_samples++;
        if (s_phase_samples >= s_p_color_samples)
        {
            s_phase_samples = 0;
            if (s_phase == Phase::PRed)
            {
                s_phase = Phase::PPorch2;
            }
            else if (s_phase == Phase::PGreen)
            {
                s_phase = Phase::PPorch3;
            }
            else if (s_phase == Phase::PBlue)
            {
                s_phase = Phase::PPorch4;
            }
        }
    }
}

void step_scottie_mode(int32_t mono)
{
    if (s_phase == Phase::Porch1)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_porch_samples)
        {
            s_phase = Phase::Green;
            s_phase_samples = 0;
            reset_pixel_state();
        }
    }
    else if (s_phase == Phase::Porch2)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_porch_samples)
        {
            s_phase = Phase::Blue;
            s_phase_samples = 0;
            reset_pixel_state();
        }
    }
    else if (s_phase == Phase::Sync)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_sync_samples)
        {
            s_phase = Phase::Porch3;
            s_phase_samples = 0;
        }
    }
    else if (s_phase == Phase::Porch3)
    {
        s_phase_samples++;
        if (s_phase_samples >= s_porch_samples)
        {
            s_phase = Phase::Red;
            s_phase_samples = 0;
            reset_pixel_state();
        }
    }
    else
    {
        int pixel = (s_phase_samples * kInWidth) / s_color_samples;
        if (pixel >= 0 && pixel < kInWidth)
        {
            s_pixel_buf[s_pixel_pos] = static_cast<int16_t>(mono);
            s_pixel_pos++;
            if (s_pixel_pos >= s_pixel_window_samples)
            {
                s_pixel_pos = 0;
            }
            if (s_pixel_fill < s_pixel_window_samples)
            {
                s_pixel_fill++;
            }

            if (pixel != s_last_pixel && s_pixel_fill == s_pixel_window_samples)
            {
                s_last_pixel = pixel;
                for (int j = 0; j < s_pixel_window_samples; ++j)
                {
                    int idx = s_pixel_pos + j;
                    if (idx >= s_pixel_window_samples)
                    {
                        idx -= s_pixel_window_samples;
                    }
                    s_pixel_window[j] = s_pixel_buf[idx];
                }

                float freq = estimate_freq_from_bins(s_pixel_window, s_pixel_window_samples,
                                                     s_pixel_bins, kPixelBinCount);
                uint8_t intensity = freq_to_intensity(freq);
                int channel = 0;
                if (s_phase == Phase::Green)
                {
                    channel = 0;
                }
                else if (s_phase == Phase::Blue)
                {
                    channel = 1;
                }
                else if (s_phase == Phase::Red)
                {
                    channel = 2;
                }
                s_accum[channel][pixel] += intensity;
                s_count[channel][pixel] += 1;
            }
        }
        s_phase_samples++;
        if (s_phase_samples >= s_color_samples)
        {
            s_phase_samples = 0;
            if (s_phase == Phase::Green)
            {
                s_phase = Phase::Porch2;
            }
            else if (s_phase == Phase::Blue)
            {
                s_phase = Phase::Sync;
            }
            else if (s_phase == Phase::Red)
            {
                render_line(s_line_index);
                s_line_index++;
                clear_accum();
                if (s_line_index >= s_line_count)
                {
                    mark_frame_done();
                }
                else
                {
                    s_phase = Phase::Porch1;
                    s_phase_samples = 0;
                    reset_pixel_state();
                }
            }
        }
    }
}
} // namespace

void pixel_decoder_init()
{
    if (!s_frame)
    {
        s_frame =
            static_cast<uint16_t*>(malloc(kOutWidth * kOutHeight * sizeof(uint16_t)));
    }

    for (int i = 0; i < kPixelBinCount; ++i)
    {
        float freq = kFreqMin + static_cast<float>(i) * kPixelBinStep;
        s_pixel_bins[i] = make_bin(freq);
    }

    s_porch_samples = static_cast<int>(kSampleRate * (kPorchMs / 1000.0f));
    s_sync_samples = static_cast<int>(kSampleRate * (kSyncPulseMs / 1000.0f));
    s_robot_sync_samples = static_cast<int>(kSampleRate * (kRobotSyncPulseMs / 1000.0f));
    s_robot_sync_porch_samples =
        static_cast<int>(kSampleRate * (kRobotSyncPorchMs / 1000.0f));
    s_robot_sep_samples = static_cast<int>(kSampleRate * (kRobotSepMs / 1000.0f));
    s_robot_porch_samples = static_cast<int>(kSampleRate * (kRobotPorchMs / 1000.0f));
    s_martin_sync_samples = static_cast<int>(kSampleRate * (kMartinSyncMs / 1000.0f));
    s_martin_porch_samples = static_cast<int>(kSampleRate * (kMartinPorchMs / 1000.0f));
    s_pd_sync_samples = static_cast<int>(kSampleRate * (kPdSyncMs / 1000.0f));
    s_pd_porch_samples = static_cast<int>(kSampleRate * (kPdPorchMs / 1000.0f));
    s_robot_y_samples = static_cast<int>(kSampleRate * (kRobotYMs / 1000.0f));
    s_robot_chroma_samples = static_cast<int>(kSampleRate * (kRobotChromaMs / 1000.0f));
    s_robot_pixel_window_y = calc_pixel_window_samples(s_robot_y_samples);
    s_robot_pixel_window_c = calc_pixel_window_samples(s_robot_chroma_samples);
    s_robot36_y_samples = static_cast<int>(kSampleRate * (kRobot36YMs / 1000.0f));
    s_robot36_chroma_samples = static_cast<int>(kSampleRate * (kRobot36ChromaMs / 1000.0f));
    s_robot36_pixel_window_y = calc_pixel_window_samples(s_robot36_y_samples);
    s_robot36_pixel_window_c = calc_pixel_window_samples(s_robot36_chroma_samples);

    update_color_timing(kColorMsScottie1, s_color_samples, s_pixel_window_samples);
    update_pd_timing(kPd120ScanMs, s_pd_scan_samples, s_pd_pixel_window_samples);
    update_p_timing(kP3SyncMs, kP3PorchMs, kP3ColorMs, s_p_sync_samples, s_p_porch_samples,
                    s_p_color_samples, s_p_pixel_window_samples);

    pixel_decoder_reset();
}

void pixel_decoder_reset()
{
    s_vis_mode = VisMode::Unknown;
    s_line_count = kInHeightScottie;
    s_line_index = 0;
    s_phase = Phase::Idle;
    s_phase_samples = 0;
    reset_pixel_state();
    clear_accum();
    clear_robot_chroma();
    clear_pd_state();
    s_frame_done = false;
}

void pixel_decoder_set_mode(const VisModeInfo& info)
{
    s_vis_mode = info.mode;
    if (info.line_count > 0)
    {
        s_line_count = info.line_count;
    }
    if (info.color_ms > 0.0f)
    {
        update_color_timing(info.color_ms, s_color_samples, s_pixel_window_samples);
    }
    if (info.pd_scan_ms > 0.0f)
    {
        update_pd_timing(info.pd_scan_ms, s_pd_scan_samples, s_pd_pixel_window_samples);
    }
    if (info.p_sync_ms > 0.0f)
    {
        update_p_timing(info.p_sync_ms, info.p_porch_ms, info.p_color_ms, s_p_sync_samples,
                        s_p_porch_samples, s_p_color_samples, s_p_pixel_window_samples);
    }
}

void pixel_decoder_set_audio_level(float level)
{
    s_audio_level = level;
}

float pixel_decoder_audio_level()
{
    return s_audio_level;
}

void pixel_decoder_clear_frame()
{
    clear_frame();
}

void pixel_decoder_clear_accum()
{
    clear_accum();
}

void pixel_decoder_clear_pd_state()
{
    clear_pd_state();
}

void pixel_decoder_clear_robot_chroma()
{
    clear_robot_chroma();
}

void pixel_decoder_start_frame()
{
    s_line_index = 0;
    s_phase = Phase::Idle;
    s_phase_samples = 0;
    reset_pixel_state();
    clear_accum();
    clear_pd_state();
    clear_robot_chroma();
    s_frame_done = false;
    clear_frame();
}

bool pixel_decoder_on_sync(bool was_receiving)
{
    const bool robot_mode = (s_vis_mode == VisMode::Robot72 || s_vis_mode == VisMode::Robot36);
    const bool martin_mode = (s_vis_mode == VisMode::Martin1 || s_vis_mode == VisMode::Martin2);
    const bool pd_mode = (s_vis_mode == VisMode::Pd50 || s_vis_mode == VisMode::Pd90 ||
                          s_vis_mode == VisMode::Pd120 || s_vis_mode == VisMode::Pd160 ||
                          s_vis_mode == VisMode::Pd180 || s_vis_mode == VisMode::Pd240 ||
                          s_vis_mode == VisMode::Pd290);
    const bool p_mode = (s_vis_mode == VisMode::P3 || s_vis_mode == VisMode::P5 ||
                         s_vis_mode == VisMode::P7);

    if (!was_receiving)
    {
        pixel_decoder_start_frame();
        if (robot_mode)
        {
            s_phase = Phase::RobotSync;
        }
        else if (martin_mode)
        {
            s_phase = Phase::MartinSync;
        }
        else if (pd_mode)
        {
            s_phase = Phase::PdSync;
        }
        else if (p_mode)
        {
            s_phase = Phase::PSync;
        }
        else
        {
            s_phase = Phase::Porch1;
        }
        s_phase_samples = 0;
        reset_pixel_state();
        return true;
    }

    if (martin_mode)
    {
        clear_accum();
        s_phase = Phase::MartinSync;
        s_phase_samples = 0;
        reset_pixel_state();
    }
    else if (p_mode)
    {
        clear_accum();
        s_phase = Phase::PSync;
        s_phase_samples = 0;
        reset_pixel_state();
    }
    else if (!robot_mode && s_phase == Phase::Blue)
    {
        // Scottie sync is between Blue and Red.
        s_phase = Phase::Sync;
        s_phase_samples = 0;
    }

    return false;
}

void pixel_decoder_step(int32_t mono)
{
    if (s_phase == Phase::Idle)
    {
        return;
    }
    const ModeStrategy& strategy = select_mode_strategy(s_vis_mode);
    strategy.step(mono);
}

VisMode pixel_decoder_mode()
{
    return s_vis_mode;
}

int pixel_decoder_line_index()
{
    return s_line_index;
}

int pixel_decoder_line_count()
{
    return s_line_count;
}

const uint16_t* pixel_decoder_framebuffer()
{
    return s_frame;
}

uint16_t pixel_decoder_frame_width()
{
    return kOutWidth;
}

uint16_t pixel_decoder_frame_height()
{
    return kOutHeight;
}

bool pixel_decoder_take_frame_done()
{
    if (!s_frame_done)
    {
        return false;
    }
    s_frame_done = false;
    return true;
}

bool pixel_decoder_is_idle()
{
    return s_phase == Phase::Idle;
}
