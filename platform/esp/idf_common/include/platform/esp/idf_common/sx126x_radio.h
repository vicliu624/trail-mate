#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::esp::idf_common
{

class Sx126xRadio
{
  public:
    static Sx126xRadio& instance();

    bool acquire();
    void release();
    bool isOnline() const;

    bool configureLoRaReceive(float freq_mhz,
                              float bw_khz,
                              uint8_t sf,
                              uint8_t cr,
                              int8_t tx_power,
                              uint16_t preamble_len,
                              uint8_t sync_word,
                              uint8_t crc_len);

    bool configureFsk(float freq_mhz,
                      int8_t tx_power,
                      float bit_rate_kbps,
                      float freq_dev_khz,
                      float rx_bw_khz,
                      uint16_t preamble_len,
                      const uint8_t* sync_word,
                      size_t sync_word_len,
                      uint8_t crc_len);

    bool startReceive();
    void standby();
    float readRssi();

    int startTransmit(const uint8_t* data, size_t size);
    uint32_t getIrqFlags();
    void clearIrqFlags(uint32_t flags);
    int getPacketLength(bool update);
    int readPacket(uint8_t* buffer, size_t size);

    const char* lastError() const;

  private:
    Sx126xRadio() = default;

    bool init_locked();
    bool probe_locked();
    void wait_ready_locked() const;
    bool write_command_locked(uint8_t cmd, const uint8_t* data, size_t size, bool wait = true);
    bool read_command_locked(uint8_t cmd,
                             const uint8_t* prefix,
                             size_t prefix_size,
                             uint8_t* data,
                             size_t size,
                             bool wait = true);
    bool write_register_locked(uint16_t addr, const uint8_t* data, size_t size);
    bool read_register_locked(uint16_t addr, uint8_t* data, size_t size);
    bool set_packet_type_locked(uint8_t packet_type);
    bool set_rf_frequency_locked(float freq_mhz);
    bool set_tx_power_locked(int8_t tx_power);
    bool set_dio_irq_params_locked(uint16_t irq_mask, uint16_t dio1_mask);
    bool clear_irq_locked(uint16_t flags);
    bool set_buffer_base_locked(uint8_t tx_base, uint8_t rx_base);
    bool set_rx_locked(uint32_t timeout_raw);
    bool set_tx_locked(uint32_t timeout_raw);
    bool configure_lora_locked(float freq_mhz,
                               float bw_khz,
                               uint8_t sf,
                               uint8_t cr,
                               int8_t tx_power,
                               uint16_t preamble_len,
                               uint8_t sync_word,
                               uint8_t crc_len);
    bool configure_fsk_locked(float freq_mhz,
                              int8_t tx_power,
                              float bit_rate_kbps,
                              float freq_dev_khz,
                              float rx_bw_khz,
                              uint16_t preamble_len,
                              const uint8_t* sync_word,
                              size_t sync_word_len,
                              uint8_t crc_len);
    void set_error_locked(const char* error);

    void* mutex_ = nullptr;
    void* device_ = nullptr;
    bool initialized_ = false;
    bool online_ = false;
    uint8_t packet_type_ = 0xFF;
    float freq_mhz_ = 0.0f;
    uint8_t last_rx_offset_ = 0;
    uint32_t users_ = 0;
    char last_error_[96] = {0};
};

} // namespace platform::esp::idf_common