#pragma once
#include "platform/linux/fsk_settings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace platform::linux_runtime
{

struct Sx126xRadioConfig
{
    std::string spi_device = "/dev/spidev1.0";
    std::string gpiochip = "/dev/gpiochip0";
    int power_gpio = 16;
    int reset_gpio = 25;
    int busy_gpio = 24;
    int irq_gpio = 26;
    std::uint32_t spi_speed_hz = 2000000;
    bool dio2_as_rf_switch = true;
    bool dio3_tcxo_1v8 = true;
};

struct Sx126xLoRaConfig
{
    float freq_mhz = 433.175f;
    float bw_khz = 250.0f;
    std::uint8_t sf = 8;
    std::uint8_t cr = 5;
    std::int8_t tx_power_dbm = 17;
    std::uint16_t preamble_len = 16;
    std::uint8_t sync_word = 0x12;
    std::uint8_t crc_len = 2;
};

struct Sx126xPacket
{
    std::array<std::uint8_t, 255> data{};
    std::size_t size = 0;
    float rssi_dbm = 0.0f;
    float snr_db = 0.0f;
    std::uint32_t freq_hz = 0;
    std::uint32_t bw_hz = 0;
    std::uint8_t sf = 0;
    std::uint8_t cr = 0;
};

struct Sx126xRadioStats
{
    bool online = false;
    std::uint32_t rx_packets = 0;
    std::uint32_t tx_packets = 0;
    std::uint32_t rx_crc_errors = 0;
    std::uint32_t rx_header_errors = 0;
    std::uint32_t rx_timeouts = 0;
    std::uint32_t rx_invalid_lengths = 0;
    std::uint32_t rx_read_errors = 0;
    std::uint32_t last_irq_flags = 0;
    Sx126xLoRaConfig lora_config{};
};

class Sx126xRadio final
{
  public:
    static Sx126xRadio& instance();

    [[nodiscard]] static bool hardwareCandidatePresent();
    [[nodiscard]] static Sx126xRadioConfig defaultConfigFromEnvironment();
    [[nodiscard]] static Sx126xLoRaConfig defaultLoRaConfigFromEnvironment();

    bool acquire(const Sx126xRadioConfig& config = defaultConfigFromEnvironment());
    void release();

    bool configureLoRa(const Sx126xLoRaConfig& config);
    bool configureFsk(const FskSettings& config);
    bool startReceive();
    bool transmit(const std::uint8_t* data, std::size_t size);
    bool pollReceive(Sx126xPacket* out);
    float readRssi();

    [[nodiscard]] bool isOnline() const;
    [[nodiscard]] const char* lastError() const;
    [[nodiscard]] Sx126xLoRaConfig appliedLoRaConfig() const;
    [[nodiscard]] Sx126xRadioStats stats() const;

  private:
    Sx126xRadio() = default;
    ~Sx126xRadio();

    Sx126xRadio(const Sx126xRadio&) = delete;
    Sx126xRadio& operator=(const Sx126xRadio&) = delete;

    bool initLocked(const Sx126xRadioConfig& config);
    bool openSpiLocked();
    bool openGpioLocked();
    void closeLocked();

    bool waitReadyLocked();
    bool transferLocked(const std::uint8_t* tx,
                        std::uint8_t* rx,
                        std::size_t size);
    bool writeCommandLocked(std::uint8_t cmd,
                            const std::uint8_t* data,
                            std::size_t size,
                            bool wait);
    bool readCommandLocked(std::uint8_t cmd,
                           const std::uint8_t* prefix,
                           std::size_t prefix_size,
                           std::uint8_t* data,
                           std::size_t size,
                           bool wait);
    bool writeRegisterLocked(std::uint16_t addr,
                             const std::uint8_t* data,
                             std::size_t size);
    bool readRegisterLocked(std::uint16_t addr,
                            std::uint8_t* data,
                            std::size_t size);

    bool prepareRadioHardwareLocked();
    bool probeLocked();
    bool readStatusLocked(std::uint8_t* out_status);
    bool setPacketTypeLocked(std::uint8_t packet_type);
    bool setRfFrequencyLocked(float freq_mhz);
    bool setTxPowerLocked(std::int8_t tx_power);
    bool setDioIrqParamsLocked(std::uint16_t irq_mask,
                               std::uint16_t dio1_mask);
    bool clearIrqLocked(std::uint16_t flags);
    bool setBufferBaseLocked(std::uint8_t tx_base, std::uint8_t rx_base);
    bool setRxLocked(std::uint32_t timeout_raw);
    bool setTxLocked(std::uint32_t timeout_raw);
    bool configureLoRaLocked(const Sx126xLoRaConfig& config);
    std::uint32_t getIrqFlagsLocked();
    int getPacketLengthLocked(std::uint8_t* out_offset);
    int readPacketLocked(std::uint8_t offset,
                         std::uint8_t* buffer,
                         std::size_t size);
    bool readPacketStatusLocked(float* out_rssi_dbm, float* out_snr_db);

    void setErrorLocked(const char* error);
    void setErrorStringLocked(const std::string& error);
    void updateLastIrqLocked(std::uint32_t irq);

    mutable std::mutex mutex_{};
    Sx126xRadioConfig config_{};
    Sx126xLoRaConfig lora_config_{};
    FskSettings fsk_config_{};
    int spi_fd_ = -1;
    int chip_fd_ = -1;
    int power_fd_ = -1;
    int reset_fd_ = -1;
    int busy_fd_ = -1;
    int irq_fd_ = -1;
    int users_ = 0;
    std::uint8_t packet_type_ = 0xFF;
    float freq_mhz_ = 0.0f;
    std::array<uint8_t, 2> image_calibration_{};
    bool initialized_ = false;
    bool online_ = false;
    bool transport_failed_ = false;
    std::uint32_t rx_packets_ = 0;
    std::uint32_t tx_packets_ = 0;
    std::uint32_t rx_crc_errors_ = 0;
    std::uint32_t rx_header_errors_ = 0;
    std::uint32_t rx_timeouts_ = 0;
    std::uint32_t rx_invalid_lengths_ = 0;
    std::uint32_t rx_read_errors_ = 0;
    std::uint32_t last_irq_flags_ = 0;
    char last_error_[512] = {};
};

} // namespace platform::linux_runtime
