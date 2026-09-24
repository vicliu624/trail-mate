#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_INVALID_ARG = 0x102;
struct sdmmc_host_t
{
    int flags = 0;
    int slot = 1;
    int max_freq_khz = 20000;
};
struct sdmmc_card_t
{
    struct
    {
        size_t sector_size = 512;
        uint32_t capacity = 256;
    } csd;
    uint32_t ocr = 0;
};
esp_err_t sdmmc_card_init(const sdmmc_host_t*, sdmmc_card_t*);
esp_err_t sdmmc_read_sectors(sdmmc_card_t*, void*, size_t, size_t);
esp_err_t sdmmc_write_sectors(sdmmc_card_t*, const void*, size_t, size_t);
inline const char* esp_err_to_name(esp_err_t) { return "injected error"; }
