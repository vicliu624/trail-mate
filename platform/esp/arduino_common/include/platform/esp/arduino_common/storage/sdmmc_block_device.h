#pragma once

#if defined(TRAIL_MATE_SDFAT_SDMMC)
#include "platform/esp/arduino_common/storage/sd_transport_config.h"
#include <common/FsBlockDeviceInterface.h>
#include <sdmmc_cmd.h>

namespace platform::esp::arduino_common::storage
{
// The enclosing SD runtime mutex owns all calls, including scratch-buffer use.
class ArduinoSdmmcBlockDevice final : public FsBlockDeviceInterface
{
  public:
    ArduinoSdmmcBlockDevice() = default;
    ArduinoSdmmcBlockDevice(const ArduinoSdmmcBlockDevice&) = delete;
    ArduinoSdmmcBlockDevice& operator=(const ArduinoSdmmcBlockDevice&) = delete;
    ~ArduinoSdmmcBlockDevice() override { end(); }
    bool begin(int clock, int command, int data0);
    bool begin(const SdmmcSdConfig& config);
    void end() override;
    bool isBusy() override { return false; }
    bool readSector(Sector_t sector, uint8_t* destination) override;
    bool readSectors(Sector_t sector, uint8_t* destination, size_t count) override;
    bool writeSector(Sector_t sector, const uint8_t* source) override;
    bool writeSectors(Sector_t sector, const uint8_t* source, size_t count) override;
    Sector_t sectorCount() override { return ready_ ? card_.csd.capacity : 0; }
    bool syncDevice() override { return ready_; }
    bool highCapacity() const { return (card_.ocr & (1UL << 30)) != 0; }
    // Sticky within an operation, including a failed batch followed by a
    // successful metadata read. Access only under the enclosing FS mutex.
    void clearIoError() { io_error_ = ESP_OK; }
    esp_err_t ioError() const { return io_error_; }
    struct ReadMetrics
    {
        uint32_t calls = 0;
        uint32_t sectors = 0;
        uint32_t max_sectors = 0;
        uint32_t elapsed_us = 0;
    };
    void resetReadMetrics() { read_metrics_ = {}; }
    const ReadMetrics& readMetrics() const { return read_metrics_; }

  private:
    bool failIo(esp_err_t error)
    {
        if (io_error_ == ESP_OK) io_error_ = error;
        return false;
    }
    esp_err_t io_error_ = ESP_OK;
    ReadMetrics read_metrics_{};
    sdmmc_card_t card_{};
    bool host_ready_ = false;
    bool ready_ = false;
    uint8_t* dma_buffer_ = nullptr;
};
} // namespace platform::esp::arduino_common::storage
#endif
