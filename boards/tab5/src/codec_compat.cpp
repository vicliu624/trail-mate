#include "boards/tab5/codec_compat.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "esp_err.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "driver/i2s_std.h"
extern "C"
{
    typedef esp_err_t (*bsp_i2s_read_fn)(void* audio_buffer, size_t len, size_t* bytes_read, uint32_t timeout_ms);
    typedef esp_err_t (*bsp_i2s_write_fn)(void* audio_buffer, size_t len, size_t* bytes_written, uint32_t timeout_ms);
    typedef esp_err_t (*bsp_codec_set_in_gain_fn)(float gain);
    typedef esp_err_t (*bsp_codec_mute_fn)(bool enable);
    typedef int (*bsp_codec_volume_fn)(int volume);
    typedef esp_err_t (*bsp_codec_get_volume_fn)(void);
    typedef esp_err_t (*bsp_codec_reconfig_fn)(uint32_t rate, uint32_t bps, i2s_slot_mode_t ch);
    typedef esp_err_t (*bsp_i2s_reconfig_clk_fn)(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch);

    typedef struct
    {
        bsp_i2s_read_fn i2s_read;
        bsp_i2s_write_fn i2s_write;
        bsp_codec_mute_fn set_mute;
        bsp_codec_volume_fn set_volume;
        bsp_codec_get_volume_fn get_volume;
        bsp_codec_set_in_gain_fn set_in_gain;
        bsp_codec_reconfig_fn codec_reconfig_fn;
        bsp_i2s_reconfig_clk_fn i2s_reconfig_clk_fn;
    } bsp_codec_config_t;

    void bsp_codec_init(void);
    bsp_codec_config_t* bsp_get_codec_handle(void);
    uint8_t bsp_codec_feed_channel(void);
}
#endif

namespace boards::tab5
{
namespace
{

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
bsp_codec_config_t* active_codec()
{
    static bsp_codec_config_t* codec = nullptr;
    if (!codec)
    {
        bsp_codec_init();
        codec = bsp_get_codec_handle();
    }
    return codec;
}
#endif

} // namespace

CodecCompat::~CodecCompat()
{
    if (scratch_)
    {
        std::free(scratch_);
        scratch_ = nullptr;
        scratch_size_ = 0;
    }
}

bool CodecCompat::ensure_ready()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    bsp_codec_config_t* codec = active_codec();
    if (!codec)
    {
        ready_ = false;
        return false;
    }
    hardware_capture_channels_ = bsp_codec_feed_channel();
    if (hardware_capture_channels_ == 0)
    {
        hardware_capture_channels_ = 1;
    }
    ready_ = true;
    return true;
#else
    ready_ = false;
    return false;
#endif
}

bool CodecCompat::ensure_scratch(size_t size)
{
    if (scratch_size_ >= size)
    {
        return true;
    }

    void* resized = std::realloc(scratch_, size);
    if (!resized)
    {
        return false;
    }

    scratch_ = resized;
    scratch_size_ = size;
    return true;
}

int CodecCompat::open(uint8_t bits_per_sample, uint8_t channel, uint32_t sample_rate)
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    if (!ensure_ready())
    {
        return -1;
    }

    bsp_codec_config_t* codec = active_codec();
    if (!codec)
    {
        return -1;
    }

    requested_bits_ = bits_per_sample;
    requested_channels_ = channel == 0 ? 1 : channel;
    requested_sample_rate_ = sample_rate;

    const i2s_slot_mode_t play_mode = requested_channels_ > 1 ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
    const i2s_slot_mode_t capture_mode = static_cast<i2s_slot_mode_t>(hardware_capture_channels_);

    esp_err_t err = ESP_OK;
    if (codec->i2s_reconfig_clk_fn)
    {
        err = codec->i2s_reconfig_clk_fn(requested_sample_rate_, requested_bits_, play_mode);
    }
    if (err != ESP_OK)
    {
        return static_cast<int>(err);
    }

    if (codec->codec_reconfig_fn)
    {
        err = codec->codec_reconfig_fn(requested_sample_rate_, requested_bits_, capture_mode);
    }
    return static_cast<int>(err);
#else
    (void)bits_per_sample;
    (void)channel;
    (void)sample_rate;
    return -1;
#endif
}

void CodecCompat::close()
{
    setOutMute(true);
}

int CodecCompat::write(uint8_t* buffer, size_t size)
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    if (!ensure_ready())
    {
        return -1;
    }

    bsp_codec_config_t* codec = active_codec();
    if (!codec || !codec->i2s_write)
    {
        return -1;
    }

    size_t bytes_written = 0;
    return static_cast<int>(codec->i2s_write(buffer, size, &bytes_written, 200));
#else
    (void)buffer;
    (void)size;
    return -1;
#endif
}

int CodecCompat::read(uint8_t* buffer, size_t size)
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    if (!ensure_ready())
    {
        return -1;
    }

    bsp_codec_config_t* codec = active_codec();
    if (!codec || !codec->i2s_read)
    {
        return -1;
    }

    if (requested_channels_ <= 1 && hardware_capture_channels_ > 1)
    {
        const size_t scratch_bytes = size * hardware_capture_channels_;
        if (!ensure_scratch(scratch_bytes))
        {
            return -1;
        }

        size_t bytes_read = 0;
        const esp_err_t err = codec->i2s_read(scratch_, scratch_bytes, &bytes_read, 200);
        if (err != ESP_OK)
        {
            return static_cast<int>(err);
        }

        auto* out = static_cast<int16_t*>(static_cast<void*>(buffer));
        const auto* in = static_cast<const int16_t*>(scratch_);
        const size_t requested_samples = size / sizeof(int16_t);
        const size_t captured_frames = (bytes_read / sizeof(int16_t)) / hardware_capture_channels_;
        const size_t frames = std::min(requested_samples, captured_frames);

        for (size_t i = 0; i < frames; ++i)
        {
            int32_t mixed = 0;
            const size_t usable_channels = std::max<size_t>(1, hardware_capture_channels_ - 1);
            for (size_t ch = 0; ch < usable_channels; ++ch)
            {
                mixed += in[i * hardware_capture_channels_ + ch];
            }
            out[i] = static_cast<int16_t>(mixed / static_cast<int32_t>(usable_channels));
        }

        if (frames < requested_samples)
        {
            std::memset(out + frames, 0, (requested_samples - frames) * sizeof(int16_t));
        }
        return 0;
    }

    size_t bytes_read = 0;
    const esp_err_t err = codec->i2s_read(buffer, size, &bytes_read, 200);
    if (err == ESP_OK && bytes_read < size)
    {
        std::memset(buffer + bytes_read, 0, size - bytes_read);
    }
    return static_cast<int>(err);
#else
    (void)buffer;
    (void)size;
    return -1;
#endif
}

void CodecCompat::setMute(bool enable)
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    mute_ = enable;
    if (ensure_ready())
    {
        bsp_codec_config_t* codec = active_codec();
        if (codec && codec->set_mute)
        {
            codec->set_mute(enable);
        }
    }
#else
    (void)enable;
#endif
}

bool CodecCompat::getMute() const
{
    return mute_;
}

void CodecCompat::setOutMute(bool enable)
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    out_mute_ = enable;
    if (ensure_ready())
    {
        bsp_codec_config_t* codec = active_codec();
        if (codec && codec->set_mute)
        {
            codec->set_mute(enable);
        }
    }
#else
    (void)enable;
#endif
}

bool CodecCompat::getOutMute() const
{
    return out_mute_;
}

void CodecCompat::setVolume(uint8_t level)
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    volume_ = level;
    if (ensure_ready())
    {
        bsp_codec_config_t* codec = active_codec();
        if (codec && codec->set_volume)
        {
            codec->set_volume(level);
        }
    }
#else
    (void)level;
#endif
}

int CodecCompat::getVolume() const
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return volume_;
#else
    return 0;
#endif
}

void CodecCompat::setGain(float db_value)
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    gain_db_ = db_value;
    if (ensure_ready())
    {
        bsp_codec_config_t* codec = active_codec();
        if (codec && codec->set_in_gain)
        {
            codec->set_in_gain(db_value);
        }
    }
#else
    (void)db_value;
#endif
}

float CodecCompat::getGain() const
{
    return gain_db_;
}

bool CodecCompat::ready() const
{
    return ready_;
}

} // namespace boards::tab5
