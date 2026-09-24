#pragma once

#include <cstddef>

namespace platform::esp::arduino_common::storage
{
// File request sizes and physical DMA batches are deliberately independent.
// SPI keeps its existing transaction boundaries; SDMMC can expose contiguous
// extents to SdFat without requiring an equally large internal DMA buffer.
struct SharedSpiSdTransferPolicy
{
    static constexpr std::size_t file_slice_bytes = 512;
};

struct SdmmcSdTransferPolicy
{
    static constexpr std::size_t sector_bytes = 512;
    static constexpr std::size_t file_slice_bytes = 16 * 1024;
    static constexpr std::size_t dma_buffer_bytes = 4 * 1024;
    static constexpr std::size_t max_direct_sectors = file_slice_bytes / sector_bytes;
};

#if defined(TRAIL_MATE_SDFAT_SDMMC) && defined(TRAIL_MATE_SDFAT_SHARED_SPI)
#error "Select exactly one SD transport for this runtime."
#endif

#if defined(TRAIL_MATE_SDFAT_SDMMC)
using ActiveSdTransferPolicy = SdmmcSdTransferPolicy;
#else
using ActiveSdTransferPolicy = SharedSpiSdTransferPolicy;
#endif
} // namespace platform::esp::arduino_common::storage
