/**
 * @file      esp_codec.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-03-02
 *
 */
#include "esp_codec.h"

#include "_wav_header.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "driver/i2s_std.h"
#if SOC_I2S_SUPPORTS_TDM
#include "driver/i2s_tdm.h"
#endif
#if SOC_I2S_SUPPORTS_PDM_TX || SOC_I2S_SUPPORTS_PDM_RX
#include "driver/i2s_pdm.h"
#endif

#define I2S_DUPLEX_MONO_DEFAULT_CFG(_sample_rate, _mclk, _bclk, _ws, _dout, _din)                     \
    {                                                                                                 \
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(_sample_rate),                                          \
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), \
        .gpio_cfg = {                                                                                 \
            .mclk = (gpio_num_t)_mclk,                                                                \
            .bclk = (gpio_num_t)_bclk,                                                                \
            .ws = (gpio_num_t)_ws,                                                                    \
            .dout = (gpio_num_t)_dout,                                                                \
            .din = (gpio_num_t)_din,                                                                  \
            .invert_flags = {                                                                         \
                .mclk_inv = false,                                                                    \
                .bclk_inv = false,                                                                    \
                .ws_inv = false,                                                                      \
            }                                                                                         \
        }                                                                                             \
    }
#else
#include "driver/i2s.h"
#endif

EspCodec::EspCodec(uint8_t i2s_channel)
{
    _i2s_num = i2s_channel;
    _pa_num = -1;
    _mck_io_num = -1;
    _bck_io_num = -1;
    _ws_io_num = -1;
    _data_out_num = -1;
    _data_in_num = -1;
    paPinCb = nullptr;
    paPinUserData = nullptr;
}

EspCodec::~EspCodec()
{
    end();
}

void EspCodec::setPaParams(int pa_pin, float pa_voltage)
{
    _pa_num = pa_pin;
    _pa_voltage = pa_voltage;
}

void EspCodec::setPaPinCallback(EspCodecPaPinCallback_t cb, void* user_data)
{
    paPinCb = cb;
    paPinUserData = user_data;
}

void EspCodec::setPins(int mclk, int sck, int ws, int data_out, int data_in)
{
    _mck_io_num = mclk;
    _bck_io_num = sck;
    _ws_io_num = ws;
    _data_out_num = data_out;
    _data_in_num = data_in;
}

bool EspCodec::begin(TwoWire& wire, uint8_t address, EspCodecType type)
{
    wire.beginTransmission(address);
    if (wire.endTransmission() != 0)
    {
        log_e("I2C device not found at address 0x%02X", address);
        return false;
    }

    if (_i2s_init() != ESP_OK)
    {
        log_e("I2S init failed!");
        return false;
    }

    this->wire = &wire;

    gpio_if = audio_codec_new_gpio();
    if (gpio_if == NULL)
    {
        log_e("new gpio failed!");
        return false;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0,
        .addr = address,
        .bus_handle = (void*)&Wire,
    };
    i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (i2c_ctrl_if == NULL)
    {
        log_e("new i2c ctrl failed!");
        audio_codec_delete_gpio_if(gpio_if);
        return false;
    }

    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = _pa_voltage,
        .codec_dac_voltage = 3.3,
    };

    switch (type)
    {
    case CODEC_TYPE_ES8311:
#ifdef CONFIG_CODEC_ES8311_SUPPORT
    {
        es8311_codec_cfg_t es8311_cfg = {
            .ctrl_if = i2c_ctrl_if,
            .gpio_if = gpio_if,
            .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
            .pa_pin = (int16_t)_pa_num,
            .pa_reverted = false,
            .master_mode = false,
            .use_mclk = true,
            .digital_mic = false,
            .invert_mclk = false,
            .invert_sclk = false,
            .hw_gain = gain,
        };
        codec_if = es8311_codec_new(&es8311_cfg);
    }
#endif
    break;
    default:
        log_e("Error chip type");
        break;
    }

    if (codec_if == NULL)
    {
        log_e("new codec failed!");
        audio_codec_delete_gpio_if(gpio_if);
        audio_codec_delete_ctrl_if(i2c_ctrl_if);
        return false;
    }

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = i2s_data_if,
    };

    codec_dev = esp_codec_dev_new(&codec_dev_cfg);
    if (codec_dev == NULL)
    {
        log_e("new codec dev failed!");
        audio_codec_delete_gpio_if(gpio_if);
        audio_codec_delete_ctrl_if(i2c_ctrl_if);
        audio_codec_delete_codec_if(codec_if);
        return false;
    }

    if (open(16, 2, 16000) != ESP_OK)
    {
        audio_codec_delete_gpio_if(gpio_if);
        audio_codec_delete_ctrl_if(i2c_ctrl_if);
        audio_codec_delete_codec_if(codec_if);
        log_e("Codec open failed!");
        return false;
    }

    close();

    return true;
}

void EspCodec::end()
{
    esp_codec_dev_delete(codec_dev);
    audio_codec_delete_gpio_if(gpio_if);
    audio_codec_delete_ctrl_if(i2c_ctrl_if);
    audio_codec_delete_codec_if(codec_if);
    _i2s_deinit();
}

void EspCodec::setMute(bool enable)
{
    esp_codec_dev_set_in_mute(codec_dev, enable);
}

bool EspCodec::getMute()
{
    bool isMute = false;
    esp_codec_dev_get_in_mute(codec_dev, &isMute);
    return isMute;
}

void EspCodec::setOutMute(bool enable)
{
    esp_codec_dev_set_out_mute(codec_dev, enable);
}

bool EspCodec::getOutMute()
{
    bool isMute = false;
    esp_codec_dev_get_out_mute(codec_dev, &isMute);
    return isMute;
}

void EspCodec::setVolume(uint8_t level)
{
    esp_codec_dev_set_out_vol(codec_dev, level);
}

int EspCodec::getVolume()
{
    int level = 0;
    esp_codec_dev_get_out_vol(codec_dev, &level);
    return level;
}

void EspCodec::setGain(float db_value)
{
    esp_codec_dev_set_in_gain(codec_dev, db_value);
}

float EspCodec::getGain()
{
    float db_value = 0;
    esp_codec_dev_get_in_gain(codec_dev, &db_value);
    return db_value;
}

int EspCodec::open(uint8_t bits_per_sample, uint8_t channel, uint32_t sample_rate)
{
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = bits_per_sample,
        .channel = channel,
        .channel_mask = 0,
        .sample_rate = sample_rate};
    int rlst = esp_codec_dev_open(codec_dev, &fs);
    if (rlst == 0 && paPinCb)
    {
        paPinCb(true, paPinUserData);
    }
    return rlst;
}

void EspCodec::close()
{
    esp_codec_dev_close(codec_dev);
    if (paPinCb)
    {
        paPinCb(false, paPinUserData);
    }
}

int EspCodec::write(uint8_t* buffer, size_t size)
{
    return esp_codec_dev_write(codec_dev, buffer, size);
}

int EspCodec::read(uint8_t* buffer, size_t size)
{
    return esp_codec_dev_read(codec_dev, buffer, size);
}

esp_err_t EspCodec::_i2s_init()
{
    log_d("I2S pins: MCLK=%d, BCK=%d, WS=%d, DOUT=%d, DIN=%d", _mck_io_num, _bck_io_num, _ws_io_num, _data_out_num, _data_in_num);
    log_d("I2S port=%d initialized", _i2s_num);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    static i2s_chan_handle_t tx_channel;
    static i2s_chan_handle_t rx_channel;

    /* Setup I2S peripheral */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)_i2s_num, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // Auto clear the legacy data in the DMA buffer
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_channel, &rx_channel));

    /* Setup I2S channels */
    const i2s_std_config_t std_cfg_default = I2S_DUPLEX_MONO_DEFAULT_CFG(16000, _mck_io_num, _bck_io_num, _ws_io_num, _data_out_num, _data_in_num);

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_channel, &std_cfg_default));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_channel));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_channel, &std_cfg_default));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_channel));

#else

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_TX | I2S_MODE_RX | I2S_MODE_MASTER),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_IRAM,
        .dma_buf_count = 2,
        .dma_buf_len = 128,
        .use_apll = true,
        .tx_desc_auto_clear = true,
    };
    esp_err_t ret = i2s_driver_install((i2s_port_t)_i2s_num, &i2s_config, 0, NULL);
    ESP_ERROR_CHECK(ret);

    i2s_pin_config_t i2s_pin_cfg = {
        .mck_io_num = _mck_io_num,
        .bck_io_num = _bck_io_num,
        .ws_io_num = _ws_io_num,
        .data_out_num = _data_out_num,
        .data_in_num = _data_in_num};
    ret = i2s_set_pin((i2s_port_t)_i2s_num, &i2s_pin_cfg);
    ESP_ERROR_CHECK(ret);
#endif

    audio_codec_i2s_cfg_t i2s_cfg = {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .port = _i2s_num,
        .rx_handle = rx_channel,
        .tx_handle = tx_channel
#else
        .port = _i2s_num,
        .rx_handle = NULL,
        .tx_handle = NULL
#endif
    };

    i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    return i2s_data_if != NULL ? ESP_OK : ESP_FAIL;
}

void EspCodec::_i2s_deinit()
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // TODO: Deinitialize I2S for IDF 5.x
#else

    i2s_driver_uninstall((i2s_port_t)_i2s_num);

    pinMode(_mck_io_num, INPUT);
    pinMode(_bck_io_num, INPUT);
    pinMode(_ws_io_num, INPUT);
    pinMode(_data_out_num, INPUT);
    pinMode(_data_in_num, INPUT);
#endif
}

const int WAVE_HEADER_SIZE = PCM_WAV_HEADER_SIZE;

bool EspCodec::playWAV(uint8_t* data, size_t len)
{
    pcm_wav_header_t* header = (pcm_wav_header_t*)data;
    if (header->fmt_chunk.audio_format != 1)
    {
        log_e("Audio format is not PCM!");
        return false;
    }
    wav_data_chunk_t* data_chunk = &header->data_chunk;
    size_t data_offset = 0;
    while (memcmp(data_chunk->subchunk_id, "data", 4) != 0)
    {
        log_d(
            "Skip chunk: %c%c%c%c, len: %lu", data_chunk->subchunk_id[0], data_chunk->subchunk_id[1], data_chunk->subchunk_id[2], data_chunk->subchunk_id[3],
            data_chunk->subchunk_size + 8);
        data_offset += data_chunk->subchunk_size + 8;
        data_chunk = (wav_data_chunk_t*)(data + WAVE_HEADER_SIZE + data_offset - 8);
    }
    log_d(
        "Play WAV: rate:%lu, bits:%d, channels:%d, size:%lu", header->fmt_chunk.sample_rate, header->fmt_chunk.bits_per_sample, header->fmt_chunk.num_of_channels,
        data_chunk->subchunk_size);

    int ret = open(header->fmt_chunk.bits_per_sample, header->fmt_chunk.num_of_channels, header->fmt_chunk.sample_rate);
    if (ret < 0)
    {
        log_e("Open audio device failed");
        return false;
    }
    write(data + WAVE_HEADER_SIZE + data_offset, data_chunk->subchunk_size);
    close();
    return true;
}

bool EspCodec::recordWAV(size_t rec_seconds, uint8_t** output, size_t* out_size, uint16_t sample_rate, uint8_t num_channels)
{
    uint16_t sample_width = 16;
    size_t rec_size = rec_seconds * ((sample_rate * (sample_width / 8)) * num_channels);
    const pcm_wav_header_t wav_header = PCM_WAV_HEADER_DEFAULT(rec_size, sample_width, sample_rate, num_channels);
    *out_size = 0;

    log_d("Record WAV: rate:%lu, bits:%u, channels:%u, size:%lu", sample_rate, sample_width, num_channels, rec_size);

    uint8_t* wav_buf = (uint8_t*)malloc(rec_size + PCM_WAV_HEADER_SIZE);
    if (wav_buf == NULL)
    {
        log_e("Failed to allocate WAV buffer with size %u", rec_size + PCM_WAV_HEADER_SIZE);
        return false;
    }
    memcpy(wav_buf, &wav_header, PCM_WAV_HEADER_SIZE);

    int rlst = this->open(sample_width, num_channels, sample_rate);
    if (rlst != ESP_CODEC_DEV_OK)
    {
        free(wav_buf);
        return false;
    }
    rlst = this->read(wav_buf + PCM_WAV_HEADER_SIZE, rec_size);
    if (rlst != ESP_CODEC_DEV_OK)
    {
        log_e("Recorded failed,error code : %d", rlst);
        free(wav_buf);
        this->close();
        return false;
    }
    *out_size = rec_size + PCM_WAV_HEADER_SIZE;
    this->close();
    *output = wav_buf;
    return true;
}
