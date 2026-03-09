#pragma once

#include <cstdlib>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/boards/tab5_board_profile.h"

extern "C" {
#include "bsp/trail_mate_tab5_runtime.h"

void bsp_set_ext_5v_en(bool en);
}

namespace platform::esp::boards::detail
{

namespace
{
constexpr const char* kTag = "tab5-board-runtime";
}

inline void initializeBoard(bool waking_from_sleep)
{
    ESP_LOGI(kTag,
             "Tab5 board runtime bootstrap: caps(display=%d touch=%d audio=%d sd=%d gps=%d rs485=%d lora=%d lora_module=%d) sys_i2c=(%d,%d,%d) ext_i2c=(%d,%d,%d) gps_uart=(%d,%d,%d) rs485_uart=(%d,%d,%d,%d) sdmmc=(%d,%d,%d,%d,%d,%d) audio_i2s=(%d,%d,%d,%d,%d) lora_spi=(host=%d sck=%d miso=%d mosi=%d) lora_ctrl=(nss=%d rst=%d irq=%d busy=%d pwr_en=%d) backlight=%d touch_int=%d wake=%d",
             tab5::kBoardProfile.has_display ? 1 : 0,
             tab5::kBoardProfile.has_touch ? 1 : 0,
             tab5::kBoardProfile.has_audio ? 1 : 0,
             tab5::kBoardProfile.has_sdcard ? 1 : 0,
             tab5::kBoardProfile.has_gps_uart ? 1 : 0,
             tab5::kBoardProfile.has_rs485_uart ? 1 : 0,
             tab5::kBoardProfile.has_lora ? 1 : 0,
             tab5::kBoardProfile.has_m5bus_lora_module_routing ? 1 : 0,
             tab5::kBoardProfile.sys_i2c.port,
             tab5::kBoardProfile.sys_i2c.sda,
             tab5::kBoardProfile.sys_i2c.scl,
             tab5::kBoardProfile.ext_i2c.port,
             tab5::kBoardProfile.ext_i2c.sda,
             tab5::kBoardProfile.ext_i2c.scl,
             tab5::kBoardProfile.gps_uart.port,
             tab5::kBoardProfile.gps_uart.tx,
             tab5::kBoardProfile.gps_uart.rx,
             tab5::kBoardProfile.rs485_uart.port,
             tab5::kBoardProfile.rs485_uart.tx,
             tab5::kBoardProfile.rs485_uart.rx,
             tab5::kBoardProfile.rs485_uart.aux,
             tab5::kBoardProfile.sdmmc.d0,
             tab5::kBoardProfile.sdmmc.d1,
             tab5::kBoardProfile.sdmmc.d2,
             tab5::kBoardProfile.sdmmc.d3,
             tab5::kBoardProfile.sdmmc.cmd,
             tab5::kBoardProfile.sdmmc.clk,
             tab5::kBoardProfile.audio_i2s.bclk,
             tab5::kBoardProfile.audio_i2s.mclk,
             tab5::kBoardProfile.audio_i2s.ws,
             tab5::kBoardProfile.audio_i2s.dout,
             tab5::kBoardProfile.audio_i2s.din,
             tab5::kBoardProfile.lora_module.spi.host,
             tab5::kBoardProfile.lora_module.spi.sck,
             tab5::kBoardProfile.lora_module.spi.miso,
             tab5::kBoardProfile.lora_module.spi.mosi,
             tab5::kBoardProfile.lora_module.nss,
             tab5::kBoardProfile.lora_module.rst,
             tab5::kBoardProfile.lora_module.irq,
             tab5::kBoardProfile.lora_module.busy,
             tab5::kBoardProfile.lora_module.pwr_en,
             tab5::kBoardProfile.lcd_backlight,
             tab5::kBoardProfile.touch_int,
             waking_from_sleep ? 1 : 0);
}

inline void initializeDisplay()
{
    if (trail_mate_tab5_display_runtime_init())
    {
        ESP_LOGI(kTag, "Tab5 display runtime initialized");
        bsp_set_ext_5v_en(true);
        vTaskDelay(pdMS_TO_TICKS(300));
        ESP_LOGI(kTag, "Tab5 M5-Bus Ext5V enabled for GNSS/LoRa modules");
    }
    else
    {
        ESP_LOGE(kTag, "Tab5 display runtime initialization failed");
    }
}

inline bool tryResolveAppContextInitHandles(AppContextInitHandles* out_handles)
{
    (void)out_handles;
    ESP_LOGI(kTag, "Tab5 AppContext handles are not available yet; board adapter still pending");
    return false;
}

inline AppContextInitHandles resolveAppContextInitHandles()
{
    AppContextInitHandles handles{};
    (void)handles;
    ESP_LOGE(kTag, "Tab5 resolveAppContextInitHandles() called before adapter implementation");
    abort();
}

} // namespace platform::esp::boards::detail
