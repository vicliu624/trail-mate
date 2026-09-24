#pragma once
#include <cstddef>
#include <cstdint>
struct esp_partition_t
{
    uint32_t size;
};
constexpr int ESP_OK = 0;
constexpr int ESP_PARTITION_TYPE_DATA = 1;
constexpr int ESP_PARTITION_SUBTYPE_DATA_FAT = 0x81;
const esp_partition_t* esp_partition_find_first(int type, int subtype, const char* label);
int esp_partition_read(const esp_partition_t* partition, std::size_t offset, void* out, std::size_t size);
