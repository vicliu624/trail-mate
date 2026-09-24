#include "platform/esp/arduino_common/storage/sdmmc_block_device.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <driver/sdmmc_host.h>
#include <esp_heap_caps.h>
#include <iostream>
#include <limits>
#include <vector>

using namespace platform::esp::arduino_common::storage;
#define CHECK(x)                                      \
    do                                                \
    {                                                 \
        if (!(x))                                     \
        {                                             \
            std::cerr << __LINE__ << ": " #x << '\n'; \
            std::abort();                             \
        }                                             \
    } while (0)

namespace
{
std::array<uint8_t, 256 * 512> disk{};
std::vector<size_t> batches;
void* allocated = nullptr;
size_t allocated_size = 0;
uintptr_t direct_start = 0, direct_end = 0;
bool fail_alloc = false;
int fail_stage = 0, fail_transfer = 0, deinits = 0;
sdmmc_host_t seen_host;
sdmmc_slot_config_t seen_slot;
bool in_range(uintptr_t p, uintptr_t start, size_t bytes)
{
    return start != 0 && p >= start && p - start < bytes;
}
} // namespace

bool esp_ptr_dma_capable(const void* ptr)
{
    const auto p = reinterpret_cast<uintptr_t>(ptr);
    return in_range(p, reinterpret_cast<uintptr_t>(allocated), allocated_size) ||
           (direct_start != 0 && p >= direct_start && p < direct_end);
}
void* heap_caps_malloc(size_t bytes, unsigned caps)
{
    CHECK(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    CHECK(allocated == nullptr);
    if (fail_alloc) return nullptr;
    allocated = std::malloc(bytes);
    allocated_size = bytes;
    return allocated;
}
void heap_caps_free(void* ptr)
{
    if (!ptr) return;
    CHECK(ptr == allocated);
    std::free(ptr);
    allocated = nullptr;
    allocated_size = 0;
}
esp_err_t sdmmc_host_init() { return fail_stage == 1 ? -1 : ESP_OK; }
esp_err_t sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t* config)
{
    CHECK(slot == 0 || slot == 1);
    seen_slot = *config;
    return fail_stage == 2 ? -1 : ESP_OK;
}
esp_err_t sdmmc_host_deinit()
{
    ++deinits;
    return ESP_OK;
}
esp_err_t sdmmc_card_init(const sdmmc_host_t* host, sdmmc_card_t* card)
{
    seen_host = *host;
    card->csd.sector_size = fail_stage == 4 ? 1024 : 512;
    card->csd.capacity = 256;
    return fail_stage == 3 ? -1 : ESP_OK;
}
esp_err_t sdmmc_read_sectors(sdmmc_card_t*, void* dst, size_t sector, size_t count)
{
    CHECK(sector + count <= 256 && count > 0);
    CHECK(esp_ptr_dma_capable(dst));
    CHECK(reinterpret_cast<uintptr_t>(dst) % 4 == 0);
    batches.push_back(count);
    if (fail_transfer == static_cast<int>(batches.size())) return -1;
    std::memcpy(dst, disk.data() + sector * 512, count * 512);
    return ESP_OK;
}
esp_err_t sdmmc_write_sectors(sdmmc_card_t*, const void* src, size_t sector, size_t count)
{
    CHECK(sector + count <= 256 && count > 0);
    CHECK(esp_ptr_dma_capable(src));
    CHECK(reinterpret_cast<uintptr_t>(src) % 4 == 0);
    batches.push_back(count);
    if (fail_transfer == static_cast<int>(batches.size())) return -1;
    std::memcpy(disk.data() + sector * 512, src, count * 512);
    return ESP_OK;
}

int main()
{
    ArduinoSdmmcBlockDevice device;
    CHECK(!device.begin(-1, 3, 1));
    fail_alloc = true;
    CHECK(!device.begin(2, 3, 1));
    fail_alloc = false;
    for (int stage = 1; stage <= 4; ++stage)
    {
        fail_stage = stage;
        const int before = deinits;
        CHECK(!device.begin(2, 3, 1));
        CHECK(allocated == nullptr);
        CHECK(deinits == before + (stage == 1 ? 0 : 1));
    }
    fail_stage = 0;
    CHECK(device.begin(2, 3, 1));
    CHECK(seen_host.flags == SDMMC_HOST_FLAG_1BIT && seen_host.max_freq_khz == 20000);
    CHECK(seen_slot.width == 1 && seen_slot.clk == 2 && seen_slot.cmd == 3 && seen_slot.d0 == 1);
    CHECK(allocated_size == 4096);
    for (size_t i = 0; i < disk.size(); ++i) disk[i] = static_cast<uint8_t>((i * 17 + i / 512) % 251);
    std::vector<uint8_t> buffer(65 * 512 + 8, 0);
    uint8_t* unaligned = buffer.data() + 1;
    device.resetReadMetrics();
    CHECK(device.readSectors(3, unaligned, 19));
    CHECK(device.readMetrics().calls == 3 && device.readMetrics().sectors == 19);
    CHECK(device.readMetrics().max_sectors == 8 && device.readMetrics().elapsed_us == 30);
    CHECK((batches == std::vector<size_t>{8, 8, 3}));
    CHECK(std::memcmp(unaligned, disk.data() + 3 * 512, 19 * 512) == 0);
    batches.clear();
    CHECK(device.writeSectors(40, unaligned, 19));
    CHECK((batches == std::vector<size_t>{8, 8, 3}));
    CHECK(std::memcmp(unaligned, disk.data() + 40 * 512, 19 * 512) == 0);
    CHECK(device.readMetrics().calls == 3); // writes are not counted as reads
    device.resetReadMetrics();
    CHECK(device.readMetrics().calls == 0 && device.readMetrics().elapsed_us == 0);

    auto* aligned = buffer.data();
    CHECK(reinterpret_cast<uintptr_t>(aligned) % 4 == 0);
    direct_start = reinterpret_cast<uintptr_t>(aligned);
    direct_end = direct_start + buffer.size();
    batches.clear();
    CHECK(device.readSectors(0, aligned, 65));
    CHECK((batches == std::vector<size_t>{32, 32, 1}));
    CHECK(std::memcmp(aligned, disk.data(), 65 * 512) == 0);
    batches.clear();
    CHECK(device.writeSectors(100, aligned, 65));
    CHECK((batches == std::vector<size_t>{32, 32, 1}));
    CHECK(std::memcmp(aligned, disk.data() + 100 * 512, 65 * 512) == 0);
    // Merely DMA-capable at the start is insufficient for the whole range.
    direct_end = direct_start + 512;
    batches.clear();
    CHECK(device.readSectors(0, aligned, 9));
    CHECK((batches == std::vector<size_t>{8, 1}));
    direct_start = direct_end = 0;
    batches.clear();
    CHECK(device.readSectors(256, nullptr, 0));
    CHECK(!device.readSector(256, aligned));
    CHECK(!device.readSectors(255, aligned, 2));
    CHECK(!device.writeSectors(0, nullptr, 1));
    CHECK(!device.writeSectors(0, aligned, std::numeric_limits<size_t>::max()));
    CHECK(batches.empty());
    CHECK(device.readSector(255, aligned));
    CHECK((batches == std::vector<size_t>{1}));

    std::fill(buffer.begin(), buffer.end(), 0xEE);
    batches.clear();
    fail_transfer = 2;
    device.resetReadMetrics();
    device.clearIoError();
    CHECK(device.ioError() == ESP_OK);
    CHECK(!device.readSectors(0, unaligned, 19));
    const auto read_error = device.ioError();
    CHECK(read_error != ESP_OK);
    CHECK(device.readMetrics().calls == 2 && device.readMetrics().sectors == 16);
    CHECK(device.readMetrics().elapsed_us == 20); // failed physical calls are included
    CHECK((batches == std::vector<size_t>{8, 8}));
    CHECK(unaligned[8 * 512] == 0xEE); // failed batch must not be copied out
    batches.clear();
    CHECK(!device.writeSectors(80, unaligned, 19));
    CHECK((batches == std::vector<size_t>{8, 8})); // no retry after partial write
    fail_transfer = 0;
    CHECK(device.readSector(0, aligned));
    CHECK(device.ioError() == read_error); // success cannot erase earlier failure
    device.clearIoError();
    CHECK(device.ioError() == ESP_OK);
    CHECK(device.readSector(0, aligned));
    CHECK(device.ioError() == ESP_OK);
    device.end();
    CHECK(allocated == nullptr && !device.readSector(0, aligned));
    const int before = deinits;
    device.end();
    CHECK(deinits == before);

    SdmmcSdConfig config;
    config.clock = 2;
    config.command = 3;
    config.data0 = 1;
    config.width = 4;
    CHECK(!device.begin(config));
    config.data1 = 4;
    config.data2 = 5;
    config.data3 = 6;
    config.slot = 0;
    config.max_frequency_khz = 40000;
    CHECK(device.begin(config));
    CHECK(seen_host.slot == 0 && seen_host.flags == SDMMC_HOST_FLAG_4BIT);
    CHECK(seen_slot.width == 4 && seen_slot.d3 == 6);
    device.end();
    CHECK(allocated == nullptr);
    std::cout << "SDMMC batching, DMA, data integrity, bounds and lifecycle passed\n";
}
