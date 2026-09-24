#if defined(TRAIL_MATE_SDFAT_SDMMC)
#include "platform/esp/arduino_common/storage/sdmmc_block_device.h"
#include "platform/esp/arduino_common/storage/sd_transfer_policy.h"

#include <algorithm>
#include <cstring>
#include <driver/sdmmc_host.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <limits>
#include <soc/soc_memory_types.h>

namespace platform::esp::arduino_common::storage
{
namespace
{
using Transfer = SdmmcSdTransferPolicy;

bool direct_dma_buffer(const void* buffer, size_t bytes)
{
    const auto address = reinterpret_cast<uintptr_t>(buffer);
    if (bytes == 0 || address % 4 != 0 || bytes - 1 > UINTPTR_MAX - address) return false;
    // Conservative ESP32 internal-DMA path: PSRAM and unaligned buffers use
    // the owned bounce buffer, even on targets with additional DMA features.
    return esp_ptr_dma_capable(buffer) &&
           esp_ptr_dma_capable(reinterpret_cast<const void*>(address + bytes - 1));
}
} // namespace

bool ArduinoSdmmcBlockDevice::begin(int clock, int command, int data0)
{
    SdmmcSdConfig config;
    config.clock = clock;
    config.command = command;
    config.data0 = data0;
    return begin(config);
}

bool ArduinoSdmmcBlockDevice::begin(const SdmmcSdConfig& config)
{
    if (ready_) return true;
    if (config.clock < 0 || config.command < 0 || config.data0 < 0 ||
        (config.width != 1 && config.width != 4) || config.slot < 0 || config.slot > 1 ||
        config.max_frequency_khz == 0 || config.max_frequency_khz > 40000 ||
        (config.width == 4 && (config.data1 < 0 || config.data2 < 0 || config.data3 < 0))) return false;
    end();
    dma_buffer_ = static_cast<uint8_t*>(heap_caps_malloc(
        Transfer::dma_buffer_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (!dma_buffer_) return false;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = config.width == 4 ? SDMMC_HOST_FLAG_4BIT : SDMMC_HOST_FLAG_1BIT;
    host.slot = config.slot;
    host.max_freq_khz = static_cast<int>(config.max_frequency_khz);
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = config.width;
    slot.clk = static_cast<gpio_num_t>(config.clock);
    slot.cmd = static_cast<gpio_num_t>(config.command);
    slot.d0 = static_cast<gpio_num_t>(config.data0);
    if (config.width == 4)
    {
        slot.d1 = static_cast<gpio_num_t>(config.data1);
        slot.d2 = static_cast<gpio_num_t>(config.data2);
        slot.d3 = static_cast<gpio_num_t>(config.data3);
    }
    if (config.internal_pullups) slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t result = sdmmc_host_init();
    if (result == ESP_OK)
    {
        host_ready_ = true;
        result = sdmmc_host_init_slot(host.slot, &slot);
    }
    if (result == ESP_OK) result = sdmmc_card_init(&host, &card_);
    if (result != ESP_OK)
    {
        ESP_LOGW("sdmmc", "card initialization failed: %s", esp_err_to_name(result));
        end();
        return false;
    }
    ready_ = card_.csd.sector_size == Transfer::sector_bytes && card_.csd.capacity > 0;
    if (!ready_) end();
    return ready_;
}

void ArduinoSdmmcBlockDevice::end()
{
    ready_ = false;
    if (host_ready_)
    {
        sdmmc_host_deinit();
        host_ready_ = false;
    }
    heap_caps_free(dma_buffer_);
    dma_buffer_ = nullptr;
}

bool ArduinoSdmmcBlockDevice::readSector(Sector_t sector, uint8_t* destination)
{
    return readSectors(sector, destination, 1);
}

bool ArduinoSdmmcBlockDevice::readSectors(Sector_t sector, uint8_t* destination, size_t count)
{
    if (!ready_ || (!destination && count) || sector > sectorCount() || count > sectorCount() - sector ||
        count > std::numeric_limits<size_t>::max() / Transfer::sector_bytes) return failIo(ESP_ERR_INVALID_ARG);
    if (count == 0) return true;
    const bool direct = direct_dma_buffer(destination, count * Transfer::sector_bytes);
    while (count != 0)
    {
        const size_t batch = std::min(count, direct ? Transfer::max_direct_sectors
                                                    : Transfer::dma_buffer_bytes / Transfer::sector_bytes);
        uint8_t* target = direct ? destination : dma_buffer_;
        const auto begin_us = esp_timer_get_time();
        const esp_err_t error = sdmmc_read_sectors(&card_, target, sector, batch);
        read_metrics_.elapsed_us += static_cast<uint32_t>(esp_timer_get_time() - begin_us);
        ++read_metrics_.calls;
        read_metrics_.sectors += static_cast<uint32_t>(batch);
        read_metrics_.max_sectors = std::max(read_metrics_.max_sectors, static_cast<uint32_t>(batch));
        if (error != ESP_OK) return failIo(error);
        const size_t bytes = batch * Transfer::sector_bytes;
        if (!direct) std::memcpy(destination, target, bytes);
        destination += bytes;
        sector += batch;
        count -= batch;
    }
    return true;
}

bool ArduinoSdmmcBlockDevice::writeSector(Sector_t sector, const uint8_t* source)
{
    return writeSectors(sector, source, 1);
}

bool ArduinoSdmmcBlockDevice::writeSectors(Sector_t sector, const uint8_t* source, size_t count)
{
    if (!ready_ || (!source && count) || sector > sectorCount() || count > sectorCount() - sector ||
        count > std::numeric_limits<size_t>::max() / Transfer::sector_bytes) return failIo(ESP_ERR_INVALID_ARG);
    if (count == 0) return true;
    const bool direct = direct_dma_buffer(source, count * Transfer::sector_bytes);
    while (count != 0)
    {
        const size_t batch = std::min(count, direct ? Transfer::max_direct_sectors
                                                    : Transfer::dma_buffer_bytes / Transfer::sector_bytes);
        const size_t bytes = batch * Transfer::sector_bytes;
        if (!direct) std::memcpy(dma_buffer_, source, bytes);
        const esp_err_t error = sdmmc_write_sectors(&card_, direct ? source : dma_buffer_, sector, batch);
        if (error != ESP_OK) return failIo(error);
        source += bytes;
        sector += batch;
        count -= batch;
    }
    return true;
}
} // namespace platform::esp::arduino_common::storage
#endif
