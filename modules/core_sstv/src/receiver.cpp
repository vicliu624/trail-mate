#include "sstv/receiver.h"
#include <algorithm>
#include <cmath>

namespace sstv::core
{
namespace
{
uint16_t rgb(int r, int g, int b)
{
    return (std::clamp(r, 0, 255) >> 3) << 11 | (std::clamp(g, 0, 255) >> 2) << 5 | (std::clamp(b, 0, 255) >> 3);
}
} // namespace
Receiver::Receiver() : decoder_(sample_rate)
{
    decoder_.set_timeout_seconds(2);
    frame_.fill(rgb(250, 240, 216));
}
void Receiver::reset()
{
    decoder_ = c_sstv_decoder(sample_rate);
    decoder_.set_timeout_seconds(2);
    status_ = {};
    line_ = {};
    have_line_ = false;
    last_y_ = 0;
    frame_.fill(rgb(250, 240, 216));
}
void Receiver::feed(const int16_t* samples, std::size_t count)
{
    if (!samples || !count || status_.state == ReceiveState::Complete) return;
    int peak = 0;
    for (std::size_t index = 0; index < count && status_.state != ReceiveState::Complete; ++index)
    {
        peak = std::max(peak, std::abs(int(samples[index])));
        uint16_t y = 0, x = 0;
        uint8_t channel = 0, value = 0;
        int16_t frequency = 0;
        if (decoder_.decode_audio(samples[index], y, x, channel, value, frequency)) pixel(y, x, channel, value);
        if (have_line_ && decoder_.get_state() == detect_sync)
        {
            const auto maximum = decoder_.get_modes()[mode_].max_height;
            if (last_y_ + 1 >= maximum)
            {
                render_line(last_y_);
                status_.state = ReceiveState::Complete;
                status_.progress = 1;
                status_.line = height;
            }
            else status_.state = ReceiveState::Waiting;
            have_line_ = false;
        }
    }
    status_.audio_level = std::clamp(peak / 32768.0f, 0.0f, 1.0f);
}
void Receiver::pixel(uint16_t y, uint16_t x, uint8_t channel, uint8_t value)
{
    const auto detected = decoder_.get_mode();
    const auto& geometry = decoder_.get_modes()[detected];
    if (!have_line_ || detected != mode_ || y < last_y_)
    {
        mode_ = detected;
        frame_.fill(rgb(250, 240, 216));
        line_ = {};
        status_.line = 0;
        status_.progress = 0;
        status_.state = ReceiveState::Receiving;
        status_.has_image = true;
        have_line_ = true;
        last_y_ = y;
    }
    if (y != last_y_)
    {
        render_line(last_y_);
        for (auto& pixel : line_)
        {
            pixel[0] = 0;
            if (mode_ != robot36) pixel[1] = pixel[2] = pixel[3] = 0;
        }
        last_y_ = y;
    }
    if (y >= geometry.max_height)
    {
        status_.state = ReceiveState::Complete;
        status_.progress = 1;
        status_.line = height;
        return;
    }
    if (x < geometry.width && x < line_.size() && channel < 4) line_[x][channel] = value;
}
void Receiver::render_row(uint16_t y, uint16_t native_height, unsigned channel, bool yuv, unsigned cr, unsigned cb)
{
    const auto native_width = decoder_.get_modes()[mode_].width;
    const unsigned first = unsigned(y) * height / native_height;
    const unsigned last = std::min<unsigned>((unsigned(y) + 1) * height / native_height, height);
    for (unsigned row = first; row < last; ++row)
        for (unsigned x = 0; x < 240; ++x)
        {
            const auto& pixel = line_[std::min<unsigned>(x * native_width / 240, 319)];
            uint16_t colour;
            if (yuv)
            {
                const int red = pixel[cr] - 128, blue = pixel[cb] - 128, luminance = pixel[channel];
                colour = rgb(luminance + 45 * red / 32, luminance - (11 * blue + 23 * red) / 32, luminance + 113 * blue / 64);
            }
            else if (mode_ == bw8 || mode_ == bw12) colour = rgb(pixel[0], pixel[0], pixel[0]);
            else colour = rgb(pixel[0], pixel[1], pixel[2]);
            frame_[row * width + x + 24] = colour;
        }
}
void Receiver::render_line(uint16_t y)
{
    const auto maximum = decoder_.get_modes()[mode_].max_height;
    if (y >= maximum) return;
    if (mode_ == pd_50 || mode_ == pd_90 || mode_ == pd_120 || mode_ == pd_180)
    {
        render_row(y * 2, maximum * 2, 0, true);
        render_row(y * 2 + 1, maximum * 2, 3, true);
    }
    else if (mode_ == robot24 || mode_ == robot72) render_row(y, maximum, 0, true);
    else if (mode_ == robot36)
    {
        unsigned high = 0;
        for (unsigned x = 0; x < 40; ++x)
            if (line_[x][3] > 128) ++high;
        const bool red_first = (high < 20 && y % 2 == 0) || (high > 20 && y % 2 == 1);
        render_row(y, maximum, 0, true, red_first ? 1 : 2, red_first ? 2 : 1);
    }
    else render_row(y, maximum, 0, false);
    status_.line = std::min<unsigned>(height, (unsigned(y) + 1) * height / maximum);
    status_.progress = float(y + 1) / maximum;
}
const char* Receiver::mode_name() const
{
    static constexpr const char* names[] = {"Martin M1", "Martin M2", "Scottie S1", "Scottie S2", "Scottie DX", "PD50", "PD90", "PD120", "PD180", "SC2-60", "SC2-120", "SC2-180", "Robot24", "Robot36", "Robot72", "BW8", "BW12"};
    return status_.has_image ? names[mode_] : "Auto";
}
} // namespace sstv::core
