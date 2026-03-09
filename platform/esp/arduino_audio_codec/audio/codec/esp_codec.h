/**
 * @file      esp_codec.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-03-02
 *
 */
#pragma once

#ifdef ARDUINO
#include "esp_codec_config.h"
#include "include/esp_codec_dev.h"
#include "include/esp_codec_dev_defaults.h"
#ifdef CONFIG_CODEC_ES8311_SUPPORT
#include "device/include/es8311_codec.h"
#endif
#include <Wire.h>

/**
 * @enum EspCodecType
 * @brief Enumeration of supported audio codec types.
 * @details This enum lists different audio codec models supported by the EspCodec class.
 *          Each value corresponds to a specific codec chip model.
 */
typedef enum
{
    CODEC_TYPE_ES8311, /**< ES8311 audio codec chip */
} EspCodecType;

/**
 * @typedef EspCodecPaPinCallback_t
 * @brief Callback function type for power amplifier (PA) pin control events.
 * @param enable True to enable the PA pin, false to disable it.
 * @param user_data User-provided data pointer passed to the callback.
 */
using EspCodecPaPinCallback_t = void (*)(bool enable, void* user_data);

/**
 * @class EspCodec
 * @brief Base class for controlling audio codecs on ESP32 devices.
 * @details This class provides an interface to initialize, configure, and operate audio codecs
 *          via I2C and I2S interfaces. It supports common audio operations like playback, recording,
 *          volume control, and codec-specific parameter settings.
 */
class EspCodec
{
  public:
    /**
     * @brief Set power amplifier (PA) parameters.
     * @param pa_pin GPIO pin number for the PA control.
     * @param pa_voltage Voltage level to set for the PA (if supported by the codec).
     */
    void setPaParams(int pa_pin, float pa_voltage);

    /**
     * @brief Register a callback function for PA pin control events.
     * @param cb Callback function to be called when PA state changes.
     * @param user_data User-specific data to pass to the callback.
     */
    void setPaPinCallback(EspCodecPaPinCallback_t cb, void* user_data);

    /**
     * @brief Configure I2S interface pins.
     * @param mclk Master clock (MCLK) pin number.
     * @param sck Serial clock (SCK/BCK) pin number.
     * @param ws Word select (WS) pin number.
     * @param data_out Data output (DOUT) pin number.
     * @param data_in Data input (DIN) pin number.
     */
    void setPins(int mclk, int sck, int ws, int data_out, int data_in);

    /**
     * @brief Initialize the codec over I2C.
     * @param wire Reference to the TwoWire object (I2C interface).
     * @param address I2C slave address of the codec.
     * @param type    Codec chip type.
     * @return True if initialization succeeds, false otherwise.
     */
    bool begin(TwoWire& wire, uint8_t address, EspCodecType type);

    /**
     * @brief Deinitialize the codec and release resources.
     */
    void end();

    /**
     * @brief Open an audio stream with specified parameters.
     * @param bits_per_sample Number of bits per audio sample (e.g., 16, 24).
     * @param channel Number of audio channels (1 for mono, 2 for stereo).
     * @param sample_rate Sampling rate in Hz (e.g., 44100, 48000).
     * @return 0 on success, negative error code on failure.
     */
    int open(uint8_t bits_per_sample, uint8_t channel, uint32_t sample_rate);

    /**
     * @brief Close the current audio stream.
     */
    void close();

    /**
     * @brief Write audio data to the codec for playback.
     * @param buffer Pointer to the audio data buffer.
     * @param size Size of the data buffer in bytes.
     * @return Number of bytes written, or negative error code on failure.
     */
    int write(uint8_t* buffer, size_t size);

    /**
     * @brief Read audio data from the codec during recording.
     * @param buffer Pointer to the buffer to store recorded data.
     * @param size Maximum number of bytes to read.
     * @return Number of bytes read, or negative error code on failure.
     */
    int read(uint8_t* buffer, size_t size);

    /**
     * @brief Set the audio volume level.
     * @param level Volume level (0-100, depending on codec capabilities).
     */
    void setVolume(uint8_t level);

    /**
     * @brief Get the current audio volume level.
     * @return Current volume level (0-100), or -1 on error.
     */
    int getVolume();

    /**
     * @brief Enable or disable audio mute.
     * @param enable True to mute, false to unmute.
     */
    void setMute(bool enable);

    /**
     * @brief Check if audio is muted.
     * @return True if muted, false otherwise.
     */
    bool getMute();

    /**
     * @brief Enable or disable output mute.
     * @param enable True to mute output, false to unmute.
     */
    void setOutMute(bool enable);

    /**
     * @brief Check if output is muted.
     * @return True if muted, false otherwise.
     */
    bool getOutMute();

    /**
     * @brief Set the audio gain in decibels (dB).
     * @param db_value Gain value in dB (e.g., 6.0, -3.0).
     */
    void setGain(float db_value);

    /**
     * @brief Get the current audio gain in decibels (dB).
     * @return Current gain value in dB, or 0.0 on error.
     */
    float getGain();

    /**
     * @brief Record audio to a WAV file.
     * @note  This is a blocking recording function that will not exit until the recording is complete.
     * @param rec_seconds Duration of recording in seconds.
     * @param output Pointer to receive the allocated WAV data buffer.
     * @param out_size Pointer to receive the size of the recorded data.
     * @return True if recording succeeds, false otherwise.
     */
    bool recordWAV(size_t rec_seconds, uint8_t** output, size_t* out_size, uint16_t sample_rate = 16000, uint8_t num_channels = 1);

    /**
     * @brief Play a WAV audio file from buffer.
     * @note  This is a blocking playback function that will loop until the audio playback is complete.
     * @param data Pointer to the WAV audio data buffer.
     * @param len Length of the WAV data buffer in bytes.
     * @return True if playback starts successfully, false otherwise.
     */
    bool playWAV(uint8_t* data, size_t len);

    /**
     * @brief Constructor for EspCodec.
     * @param i2s_channel I2S channel number (0 or 1, default: 0).
     */
    EspCodec(uint8_t i2s_channel = 0);

    /**
     * @brief Destructor for EspCodec.
     * @details Cleans up resources and deinitializes the codec.
     */
    ~EspCodec();

  private:
    /**
     * @brief Internal function to initialize the I2S peripheral.
     * @return ESP_OK on success, or other ESP error codes on failure.
     */
    esp_err_t _i2s_init();

    /**
     * @brief Internal function to deinitialize the I2S peripheral.
     */
    void _i2s_deinit();

    int _mck_io_num;                          /**< Master clock (MCK) pin number (limited to GPIO0/GPIO1/GPIO3 on ESP32) */
    int _bck_io_num;                          /**< Bit clock (BCK) pin number */
    int _ws_io_num;                           /**< Word select (WS) pin number */
    int _data_out_num;                        /**< Data output (DOUT) pin number */
    int _data_in_num;                         /**< Data input (DIN) pin number */
    int _pa_num;                              /**< Power amplifier (PA) control pin number */
    float _pa_voltage;                        /**< PA voltage setting */
    uint8_t _i2s_num;                         /**< I2S peripheral number (0 or 1) */
    const audio_codec_gpio_if_t* gpio_if;     /**< GPIO interface for codec control */
    const audio_codec_ctrl_if_t* i2c_ctrl_if; /**< I2C control interface for codec */
    const audio_codec_if_t* codec_if;         /**< Core codec interface */
    const audio_codec_data_if_t* i2s_data_if; /**< I2S data transfer interface */
    esp_codec_dev_handle_t codec_dev;         /**< Handle to the codec device */
    TwoWire* wire;                            /**< Pointer to the I2C interface object */
    EspCodecPaPinCallback_t paPinCb;          /**< Callback function for PA pin control */
    void* paPinUserData;                      /**< User data for PA pin callback */
};

#endif
