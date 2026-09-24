#pragma once
#include <sdmmc_cmd.h>
using gpio_num_t = int;
constexpr int SDMMC_HOST_FLAG_1BIT = 1;
constexpr int SDMMC_HOST_FLAG_4BIT = 4;
constexpr int SDMMC_SLOT_FLAG_INTERNAL_PULLUP = 1;
struct sdmmc_slot_config_t
{
    int width = 1;
    int clk = -1, cmd = -1, d0 = -1, d1 = -1, d2 = -1, d3 = -1;
    int flags = 0;
};
#define SDMMC_HOST_DEFAULT() \
    sdmmc_host_t {}
#define SDMMC_SLOT_CONFIG_DEFAULT() \
    sdmmc_slot_config_t {}
esp_err_t sdmmc_host_init();
esp_err_t sdmmc_host_init_slot(int, const sdmmc_slot_config_t*);
esp_err_t sdmmc_host_deinit();
