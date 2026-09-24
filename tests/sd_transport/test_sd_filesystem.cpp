#include "platform/esp/arduino_common/storage/sd_file_lifetime.h"
#include "platform/esp/arduino_common/storage/sd_file_probe.h"
#include "platform/esp/arduino_common/storage/sd_transfer_policy.h"
#include "platform/esp/arduino_common/storage/sdmmc_block_device.h"
#include <FsLib/FsLib.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <driver/sdmmc_host.h>
#include <iostream>
#include <unordered_map>
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
uint32_t capacity = 0;
std::unordered_map<uint32_t, std::array<uint8_t, 512>> sectors;
size_t largest_read = 0, largest_write = 0, read_calls = 0;
size_t write_calls = 0;
bool fail_reads = false;
} // namespace

bool esp_ptr_dma_capable(const void*) { return false; } // PSRAM-like caller buffers
void* heap_caps_malloc(size_t bytes, unsigned) { return std::malloc(bytes); }
void heap_caps_free(void* ptr) { std::free(ptr); }
esp_err_t sdmmc_host_init() { return ESP_OK; }
esp_err_t sdmmc_host_init_slot(int, const sdmmc_slot_config_t*) { return ESP_OK; }
esp_err_t sdmmc_host_deinit() { return ESP_OK; }
esp_err_t sdmmc_card_init(const sdmmc_host_t*, sdmmc_card_t* card)
{
    card->csd.capacity = capacity;
    card->csd.sector_size = 512;
    return ESP_OK;
}
esp_err_t sdmmc_read_sectors(sdmmc_card_t*, void* out, size_t sector, size_t count)
{
    if (fail_reads) return -91;
    CHECK(sector <= capacity && count <= capacity - sector);
    largest_read = std::max(largest_read, count);
    ++read_calls;
    auto* dst = static_cast<uint8_t*>(out);
    for (size_t i = 0; i < count; ++i)
    {
        const auto found = sectors.find(static_cast<uint32_t>(sector + i));
        if (found == sectors.end()) std::memset(dst + i * 512, 0, 512);
        else std::memcpy(dst + i * 512, found->second.data(), 512);
    }
    return ESP_OK;
}
esp_err_t sdmmc_write_sectors(sdmmc_card_t*, const void* input, size_t sector, size_t count)
{
    ++write_calls;
    CHECK(sector <= capacity && count <= capacity - sector);
    largest_write = std::max(largest_write, count);
    const auto* src = static_cast<const uint8_t*>(input);
    for (size_t i = 0; i < count; ++i)
    {
        const auto key = static_cast<uint32_t>(sector + i);
        const auto* block = src + i * 512;
        if (std::all_of(block, block + 512, [](uint8_t byte)
                        { return byte == 0; })) sectors.erase(key);
        else std::memcpy(sectors[key].data(), block, 512);
    }
    return ESP_OK;
}

void round_trip(uint8_t fat_type, uint32_t card_sectors)
{
    sectors.clear();
    capacity = card_sectors;
    ArduinoSdmmcBlockDevice device;
    CHECK(device.begin(2, 3, 1));
    std::array<uint8_t, 512> scratch{};
    if (fat_type == 64)
    {
        ExFatFormatter formatter;
        CHECK(formatter.format(&device, scratch.data()));
    }
    else
    {
        FatFormatter formatter;
        CHECK(formatter.format(&device, scratch.data()));
    }
    FsVolume volume;
    CHECK(volume.begin(&device));
    CHECK(volume.fatType() == fat_type);
    std::vector<uint8_t> expected(70 * 1024 + 37);
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<uint8_t>((i * 13 + i / 512) % 251);
    auto file = volume.open("batch.bin", O_CREAT | O_RDWR | O_TRUNC);
    CHECK(file);
    auto neighbor = volume.open("neighbor.bin", O_CREAT | O_RDWR | O_TRUNC);
    CHECK(neighbor);
    largest_write = 0;
    for (size_t offset = 0; offset < expected.size();)
    {
        const size_t bytes = std::min(SdmmcSdTransferPolicy::file_slice_bytes, expected.size() - offset);
        CHECK(file.write(expected.data() + offset, bytes) == bytes);
        // Interleave allocations so reads also cross noncontiguous extents.
        CHECK(neighbor.write(scratch.data(), scratch.size()) == scratch.size());
        offset += bytes;
    }
    CHECK(file.sync());
    CHECK(largest_write > 1);
    CHECK(file.close());
    CHECK(neighbor.close());
    volume.end();

    // Remount to exclude file/volume cache as the explanation for correctness.
    CHECK(volume.begin(&device));
    size_t old_calls = 0;
    for (const size_t slice : {SharedSpiSdTransferPolicy::file_slice_bytes, SdmmcSdTransferPolicy::file_slice_bytes})
    {
        file = volume.open("batch.bin", O_RDONLY);
        CHECK(file && file.fileSize() == expected.size());
        std::vector<uint8_t> result(expected.size() + 2, 0xA5);
        largest_read = read_calls = 0;
        for (size_t offset = 0; offset < expected.size();)
        {
            const size_t bytes = std::min(slice, expected.size() - offset);
            CHECK(file.read(result.data() + 1 + offset, bytes) == static_cast<int>(bytes));
            offset += bytes;
        }
        CHECK(result.front() == 0xA5 && result.back() == 0xA5);
        CHECK(std::memcmp(result.data() + 1, expected.data(), expected.size()) == 0);
        if (slice == 512)
        {
            CHECK(largest_read == 1);
            old_calls = read_calls;
        }
        else
        {
            CHECK(largest_read > 1 && largest_read <= 8);
            CHECK(read_calls < old_calls);
            std::cout << "FAT type " << static_cast<int>(fat_type) << ": " << old_calls
                      << " -> " << read_calls << " hardware reads; max sectors=" << largest_read << '\n';
        }
        for (const uint32_t offset : {1U, 511U, 8191U, 16383U, 65535U})
        {
            CHECK(file.seekSet(offset));
            std::array<uint8_t, 4096> partial{};
            CHECK(file.read(partial.data(), partial.size()) == static_cast<int>(partial.size()));
            CHECK(std::memcmp(partial.data(), expected.data() + offset, partial.size()) == 0);
        }
        CHECK(file.close());
    }
    SdPathProbeScratch probe;
    CHECK(probe_sd_path(volume, "/batch.bin", probe) == SdPathEvidence::Present);
    CHECK(probe_sd_path(volume, "/BATCH.BIN", probe) == SdPathEvidence::Present);
    CHECK(probe_sd_path(volume, "/absent.bin", probe) == SdPathEvidence::Absent);
    CHECK(probe_sd_path(volume, "/absent/child.png", probe) == SdPathEvidence::Absent);
    CHECK(probe_sd_path(volume, "/batch.bin/child.png", probe) == SdPathEvidence::Uncertain);
    CHECK(probe_sd_path(volume, "../batch.bin", probe) == SdPathEvidence::Uncertain);
    CHECK(probe_sd_path(volume, "/../batch.bin", probe) == SdPathEvidence::Uncertain);
    CHECK(volume.mkdir("/maps/base", true));
    file = volume.open("/maps/base/long tile name.png", O_CREAT | O_WRONLY);
    CHECK(file && file.close());
    CHECK(probe_sd_path(volume, "/maps/base/long tile name.png", probe) == SdPathEvidence::Present);
    CHECK(probe_sd_path(volume, "/maps/base/not there.png", probe) == SdPathEvidence::Absent);
    CHECK(!probe.directory && !probe.entry);

    if (fat_type != 64)
    {
        // A corrupt LFN checksum makes openNext return false without setting
        // the directory's read-error flag. It is not proof of directory EOF.
        file = volume.open("/maps/base", O_RDONLY);
        CHECK(file);
        const auto directory_sector = file.firstSector();
        CHECK(file.close());
        volume.end();
        auto& directory_bytes = sectors[directory_sector];
        std::size_t checksum_offset = 512;
        for (std::size_t offset = 0; offset < 512; offset += 32)
        {
            if (directory_bytes[offset + 11] == 0x0F)
            {
                checksum_offset = offset + 13;
                break;
            }
        }
        CHECK(checksum_offset < 512);
        const auto checksum = directory_bytes[checksum_offset];
        directory_bytes[checksum_offset] ^= 1;
        CHECK(volume.begin(&device));
        CHECK(probe_sd_path(volume, "/maps/base/not there.png", probe) == SdPathEvidence::Uncertain);
        volume.end();
        sectors[directory_sector][checksum_offset] = checksum;
        CHECK(volume.begin(&device));
    }

    volume.end();
    CHECK(volume.begin(&device));
    fail_reads = true;
    CHECK(probe_sd_path(volume, "/maps/base/not there.png", probe) == SdPathEvidence::Uncertain);
    CHECK(device.ioError() == -91);
    fail_reads = false;
    CHECK(!probe.directory && !probe.entry);
    volume.end();
    CHECK(volume.begin(&device));
    device.clearIoError();
    CHECK(probe_sd_path(volume, "/maps/base/not there.png", probe) == SdPathEvidence::Absent);
    CHECK(device.ioError() == ESP_OK);
    // Dirty file metadata must not be flushed by stale-handle disposal after
    // a media boundary. Exercise the same helper used by SdRuntimeFile.
    file = volume.open("abandon.bin", O_CREAT | O_RDWR | O_TRUNC);
    CHECK(file);
    CHECK(file.write("pending", 7) == 7);
    const auto before_abandon_reads = read_calls;
    const auto before_abandon_writes = write_calls;
    abandon_sd_file(file);
    CHECK(!file);
    CHECK(read_calls == before_abandon_reads && write_calls == before_abandon_writes);
    CHECK(file.close());
    CHECK(read_calls == before_abandon_reads && write_calls == before_abandon_writes);
    volume.end();
}

int main()
{
    round_trip(16, 65536);
    round_trip(32, 8 * 1024 * 1024);
    round_trip(64, 1024 * 1024);
}
