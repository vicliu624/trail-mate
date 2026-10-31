#include "bsp/trail_mate_t_display_p4_runtime.h"

#include "boards/t_display_p4/t_display_p4_board.h"
#include "platform/audio/audio_focus.h"

#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{

constexpr char kTag[] = "p4_audio";
constexpr i2s_port_t kI2sPort = I2S_NUM_0;
constexpr uint32_t kInitialSampleRateHz = 16000;
constexpr int kBitsPerSample = 16;
constexpr int kPhysicalChannels = 2;
constexpr int kMclkMultiple = 384;
constexpr float kMicrophoneGainDb = 36.0f;
constexpr std::size_t kScratchFrames = 320;

struct AudioRuntime
{
    StaticSemaphore_t mutex_storage{};
    i2s_chan_handle_t tx = nullptr;
    i2s_chan_handle_t rx = nullptr;
    const audio_codec_data_if_t* data_if = nullptr;
    const audio_codec_ctrl_if_t* ctrl_if = nullptr;
    const audio_codec_if_t* codec_if = nullptr;
    esp_codec_dev_handle_t codec = nullptr;
    trail_mate_t_display_p4_audio_owner_t owner =
        TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_NONE;
    uint32_t sample_rate_hz = 0;
    uint8_t volume_percent = 45;
    int64_t next_init_us = 0;
    bool ready = false;
    bool cleanup_required = false;
    std::array<int16_t, kScratchFrames * kPhysicalChannels> stereo_scratch{};
};

AudioRuntime s_audio{};
platform::audio::FocusToken s_audio_focus;
struct FocusReservation
{
    platform::audio::FocusToken token;
    bool allowed = false;
    explicit FocusReservation(trail_mate_t_display_p4_audio_owner_t owner)
    {
        if (owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_MUSIC)
        {
            allowed = !platform::audio::music_blocked();
            return;
        }
        using platform::audio::FocusOwner;
        const auto kind = owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_WALKIE ? FocusOwner::Walkie : owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_RETICULUM_CALL ? FocusOwner::Call
                                                                                                 : owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_SSTV             ? FocusOwner::Sstv
                                                                                                 : owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_FM               ? FocusOwner::FmRadio
                                                                                                                                                                 : FocusOwner::Notification;
        token = platform::audio::request_focus(kind);
        allowed = bool(token);
    }
    ~FocusReservation() { platform::audio::release_focus(token); }
};

// A failed open/configuration may have partially activated the codec. Retain
// the caller's reservation if rollback cannot prove that it closed physically.
void rollback_session_locked(trail_mate_t_display_p4_audio_owner_t owner,
                             uint32_t sample_rate_hz, FocusReservation& focus)
{
    (void)esp_codec_dev_set_out_mute(s_audio.codec, true);
    if (esp_codec_dev_close(s_audio.codec) == ESP_CODEC_DEV_OK) return;
    s_audio.owner = owner;
    s_audio.sample_rate_hz = sample_rate_hz;
    s_audio.cleanup_required = true;
    s_audio_focus = focus.token;
    focus.token = {};
}

SemaphoreHandle_t audio_mutex()
{
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(&s_audio.mutex_storage);
    return mutex;
}

class AudioLock
{
  public:
    AudioLock()
    {
        SemaphoreHandle_t mutex = audio_mutex();
        locked_ = mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
    }

    ~AudioLock()
    {
        if (locked_)
        {
            xSemaphoreGive(audio_mutex());
        }
    }

    explicit operator bool() const { return locked_; }

  private:
    bool locked_ = false;
};

void release_audio_resources_locked()
{
    if (s_audio.codec) esp_codec_dev_delete(s_audio.codec);
    if (s_audio.codec_if) audio_codec_delete_codec_if(s_audio.codec_if);
    if (s_audio.ctrl_if) audio_codec_delete_ctrl_if(s_audio.ctrl_if);
    if (s_audio.data_if) audio_codec_delete_data_if(s_audio.data_if);
    if (s_audio.tx) i2s_del_channel(s_audio.tx);
    if (s_audio.rx) i2s_del_channel(s_audio.rx);
    s_audio.codec = nullptr;
    s_audio.codec_if = nullptr;
    s_audio.ctrl_if = nullptr;
    s_audio.data_if = nullptr;
    s_audio.tx = s_audio.rx = nullptr;
    s_audio.ready = false;
}

bool initialize_audio_locked()
{
    if (s_audio.ready)
    {
        return true;
    }
    if (esp_timer_get_time() < s_audio.next_init_us)
    {
        return false;
    }
    s_audio.next_init_us = esp_timer_get_time() + 2000000;
    // Every partial setup must unwind before a later retry can reserve I2S0.
    struct SetupCleanup
    {
        ~SetupCleanup()
        {
            if (!s_audio.ready) release_audio_resources_locked();
        }
    } cleanup;

    auto& board = ::boards::t_display_p4::TDisplayP4Board::instance();
    if (!board.ensureExternal3v3Power() || !board.externalI2cHandle())
    {
        ESP_LOGE(kTag, "external 3V3 or I2C bus unavailable");
        return false;
    }

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;
    if (i2s_new_channel(&channel_config, &s_audio.tx, &s_audio.rx) != ESP_OK)
    {
        ESP_LOGE(kTag, "I2S channel creation failed");
        return false;
    }

    const auto& pins = board.profile().audio_i2s;
    i2s_std_config_t i2s_config{};
    i2s_config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kInitialSampleRateHz);
    i2s_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
    i2s_config.slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                            I2S_SLOT_MODE_STEREO);
    i2s_config.gpio_cfg.mclk = static_cast<gpio_num_t>(pins.mclk);
    i2s_config.gpio_cfg.bclk = static_cast<gpio_num_t>(pins.bclk);
    i2s_config.gpio_cfg.ws = static_cast<gpio_num_t>(pins.ws);
    i2s_config.gpio_cfg.dout = static_cast<gpio_num_t>(pins.dout);
    i2s_config.gpio_cfg.din = static_cast<gpio_num_t>(pins.din);
    i2s_config.gpio_cfg.invert_flags = {};

    if (i2s_channel_init_std_mode(s_audio.tx, &i2s_config) != ESP_OK ||
        i2s_channel_init_std_mode(s_audio.rx, &i2s_config) != ESP_OK)
    {
        ESP_LOGE(kTag, "I2S standard mode initialization failed");
        return false;
    }

    audio_codec_i2s_cfg_t data_config{};
    data_config.port = static_cast<uint8_t>(kI2sPort);
    data_config.rx_handle = s_audio.rx;
    data_config.tx_handle = s_audio.tx;
    s_audio.data_if = audio_codec_new_i2s_data(&data_config);
    if (!s_audio.data_if)
    {
        ESP_LOGE(kTag, "codec I2S data interface unavailable");
        return false;
    }

    audio_codec_i2c_cfg_t control_config{};
    control_config.port = static_cast<uint8_t>(board.externalI2c().port);
    control_config.addr = ES8311_CODEC_DEFAULT_ADDR;
    control_config.bus_handle = board.externalI2cHandle();
    s_audio.ctrl_if = audio_codec_new_i2c_ctrl(&control_config);
    if (!s_audio.ctrl_if)
    {
        ESP_LOGE(kTag, "ES8311 I2C control interface unavailable");
        return false;
    }

    es8311_codec_cfg_t codec_config{};
    codec_config.ctrl_if = s_audio.ctrl_if;
    codec_config.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    codec_config.pa_pin = -1;
    codec_config.master_mode = false;
    codec_config.use_mclk = true;
    codec_config.digital_mic = false;
    codec_config.no_dac_ref = true;
    codec_config.mclk_div = kMclkMultiple;
    s_audio.codec_if = es8311_codec_new(&codec_config);
    if (!s_audio.codec_if)
    {
        ESP_LOGE(kTag, "ES8311 codec interface unavailable");
        return false;
    }

    esp_codec_dev_cfg_t device_config{};
    device_config.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT;
    device_config.codec_if = s_audio.codec_if;
    device_config.data_if = s_audio.data_if;
    s_audio.codec = esp_codec_dev_new(&device_config);
    if (!s_audio.codec)
    {
        ESP_LOGE(kTag, "ES8311 codec device unavailable");
        return false;
    }

    s_audio.ready = true;
    ESP_LOGI(kTag,
             "ES8311 ready i2c=%d pins(mclk=%d bclk=%d ws=%d dout=%d din=%d)",
             board.externalI2c().port,
             pins.mclk,
             pins.bclk,
             pins.ws,
             pins.dout,
             pins.din);
    return true;
}

bool owner_active_locked(trail_mate_t_display_p4_audio_owner_t owner)
{
    return owner != TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_NONE &&
           s_audio.owner == owner && s_audio.codec && !s_audio.cleanup_required;
}

} // namespace

extern "C" bool trail_mate_t_display_p4_audio_is_ready(void)
{
    AudioLock lock;
    return lock && initialize_audio_locked();
}

extern "C" bool trail_mate_t_display_p4_audio_begin(
    trail_mate_t_display_p4_audio_owner_t owner,
    uint32_t sample_rate_hz)
{
    if (owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_NONE || sample_rate_hz == 0)
    {
        return false;
    }

    {
        AudioLock lock;
        if (!lock) return false;
        if (s_audio.owner == owner) return !s_audio.cleanup_required && s_audio.sample_rate_hz == sample_rate_hz;
        if (s_audio.owner != TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_NONE && s_audio.owner != TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_MUSIC) return false;
    }
    // Suspend the music worker before taking the codec lock it needs to close its session.
    FocusReservation focus(owner);
    if (!focus.allowed) return false;
    AudioLock lock;
    if (!lock || !initialize_audio_locked())
    {
        return false;
    }
    if (s_audio.owner != TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_NONE)
    {
        return s_audio.owner == owner && !s_audio.cleanup_required && s_audio.sample_rate_hz == sample_rate_hz;
    }

    esp_codec_dev_sample_info_t sample_info{};
    sample_info.bits_per_sample = kBitsPerSample;
    sample_info.channel = kPhysicalChannels;
    sample_info.sample_rate = sample_rate_hz;
    sample_info.mclk_multiple = kMclkMultiple;
    const int open_result = esp_codec_dev_open(s_audio.codec, &sample_info);
    if (open_result != ESP_CODEC_DEV_OK)
    {
        ESP_LOGE(kTag,
                 "open failed owner=%u rate=%lu rc=%d",
                 static_cast<unsigned>(owner),
                 static_cast<unsigned long>(sample_rate_hz),
                 open_result);
        rollback_session_locked(owner, sample_rate_hz, focus);
        return false;
    }

    // Music and FM apply their own PCM gain; don't apply a second unrelated
    // notification-volume attenuation at the codec.
    const uint8_t hardware_volume = (owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_MUSIC || owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_FM) ? 100 : s_audio.volume_percent;
    if (esp_codec_dev_set_in_gain(s_audio.codec, kMicrophoneGainDb) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_vol(s_audio.codec, hardware_volume) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_mute(s_audio.codec, hardware_volume == 0) != ESP_CODEC_DEV_OK)
    {
        rollback_session_locked(owner, sample_rate_hz, focus);
        return false;
    }
    s_audio.owner = owner;
    s_audio.sample_rate_hz = sample_rate_hz;
    s_audio.cleanup_required = false;
    s_audio_focus = focus.token;
    focus.token = {};
    ESP_LOGI(kTag,
             "session begin owner=%u rate=%lu volume=%u",
             static_cast<unsigned>(owner),
             static_cast<unsigned long>(sample_rate_hz),
             static_cast<unsigned>(s_audio.volume_percent));
    return true;
}

extern "C" bool trail_mate_t_display_p4_audio_set_input_gain(
    trail_mate_t_display_p4_audio_owner_t owner, float gain_db)
{
    if (!std::isfinite(gain_db)) return false;
    AudioLock lock;
    return lock && owner_active_locked(owner) &&
           esp_codec_dev_set_in_gain(s_audio.codec, std::clamp(gain_db, 0.0f, 42.0f)) == ESP_CODEC_DEV_OK;
}

extern "C" void trail_mate_t_display_p4_audio_end(
    trail_mate_t_display_p4_audio_owner_t owner)
{
    (void)trail_mate_t_display_p4_audio_end_checked(owner);
}

extern "C" bool trail_mate_t_display_p4_audio_end_checked(
    trail_mate_t_display_p4_audio_owner_t owner)
{
    if (owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_NONE) return false;
    platform::audio::FocusToken focus;
    {
        AudioLock lock;
        if (!lock) return false;
        // The postcondition is that this owner holds no session. Another
        // owner's session already satisfies it and must remain untouched.
        if (s_audio.owner != owner) return true;
        if (!s_audio.codec) return false;

        (void)esp_codec_dev_set_out_mute(s_audio.codec, true);
        if (esp_codec_dev_close(s_audio.codec) != ESP_CODEC_DEV_OK)
        {
            s_audio.cleanup_required = true;
            return false;
        }
        s_audio.owner = TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_NONE;
        s_audio.sample_rate_hz = 0;
        s_audio.cleanup_required = false;
        focus = s_audio_focus;
        s_audio_focus = {};
        ESP_LOGI(kTag, "session end owner=%u", static_cast<unsigned>(owner));
    }
    platform::audio::release_focus(focus);
    return true;
}

extern "C" bool trail_mate_t_display_p4_audio_read_mono(
    trail_mate_t_display_p4_audio_owner_t owner,
    int16_t* pcm,
    size_t sample_count)
{
    if (!pcm || sample_count == 0)
    {
        return false;
    }

    AudioLock lock;
    if (!lock || !owner_active_locked(owner))
    {
        return false;
    }

    std::size_t offset = 0;
    while (offset < sample_count)
    {
        const std::size_t frames = std::min(kScratchFrames, sample_count - offset);
        const int result = esp_codec_dev_read(s_audio.codec,
                                              s_audio.stereo_scratch.data(),
                                              static_cast<int>(frames * kPhysicalChannels *
                                                               sizeof(int16_t)));
        if (result != ESP_CODEC_DEV_OK)
        {
            return false;
        }
        for (std::size_t index = 0; index < frames; ++index)
        {
            pcm[offset + index] = s_audio.stereo_scratch[index * kPhysicalChannels];
        }
        offset += frames;
    }
    return true;
}

extern "C" bool trail_mate_t_display_p4_audio_write_mono(
    trail_mate_t_display_p4_audio_owner_t owner,
    const int16_t* pcm,
    size_t sample_count)
{
    if (!pcm || sample_count == 0)
    {
        return false;
    }

    AudioLock lock;
    if (!lock || !owner_active_locked(owner))
    {
        return false;
    }

    std::size_t offset = 0;
    while (offset < sample_count)
    {
        const std::size_t frames = std::min(kScratchFrames, sample_count - offset);
        for (std::size_t index = 0; index < frames; ++index)
        {
            const int16_t sample = pcm[offset + index];
            s_audio.stereo_scratch[index * kPhysicalChannels] = sample;
            s_audio.stereo_scratch[(index * kPhysicalChannels) + 1] = sample;
        }
        const int result = esp_codec_dev_write(s_audio.codec,
                                               s_audio.stereo_scratch.data(),
                                               static_cast<int>(frames * kPhysicalChannels *
                                                                sizeof(int16_t)));
        if (result != ESP_CODEC_DEV_OK)
        {
            return false;
        }
        offset += frames;
    }
    return true;
}

extern "C" void trail_mate_t_display_p4_audio_set_volume_percent(uint8_t volume_percent)
{
    AudioLock lock;
    if (!lock)
    {
        return;
    }

    s_audio.volume_percent = volume_percent > 100U ? 100U : volume_percent;
    if (s_audio.ready && s_audio.owner != TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_NONE && s_audio.owner != TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_MUSIC && s_audio.owner != TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_RETICULUM_CALL && s_audio.owner != TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_FM)
    {
        (void)esp_codec_dev_set_out_vol(s_audio.codec, s_audio.volume_percent);
        (void)esp_codec_dev_set_out_mute(s_audio.codec, s_audio.volume_percent == 0);
    }
}

extern "C" bool trail_mate_t_display_p4_audio_set_session_volume(trail_mate_t_display_p4_audio_owner_t owner, uint8_t volume_percent)
{
    AudioLock lock;
    if (!lock || !s_audio.ready || owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_NONE || s_audio.owner != owner) return false;
    const auto volume = std::min<uint8_t>(volume_percent, 100);
    return esp_codec_dev_set_out_vol(s_audio.codec, volume) == ESP_CODEC_DEV_OK && esp_codec_dev_set_out_mute(s_audio.codec, volume == 0) == ESP_CODEC_DEV_OK;
}
