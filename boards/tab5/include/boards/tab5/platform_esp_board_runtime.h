#pragma once

#include <cstdlib>

#include "boards/tab5/board_profile.h"
#include "boards/tab5/tab5_board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/boards/board_runtime.h"

extern "C"
{
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
    const auto& profile = ::boards::tab5::Tab5Board::profile();
    ESP_LOGI(kTag,
             "Tab5 board runtime bootstrap: caps(display=%d touch=%d audio=%d sd=%d gps=%d rs485=%d lora=%d lora_module=%d) sys_i2c=(%d,%d,%d) ext_i2c=(%d,%d,%d) gps_uart=(%d,%d,%d) rs485_uart=(%d,%d,%d,%d) sdmmc=(%d,%d,%d,%d,%d,%d) audio_i2s=(%d,%d,%d,%d,%d) lora_spi=(host=%d sck=%d miso=%d mosi=%d) lora_ctrl=(nss=%d rst=%d irq=%d busy=%d pwr_en=%d) backlight=%d touch_int=%d wake=%d",
             profile.has_display ? 1 : 0,
             profile.has_touch ? 1 : 0,
             profile.has_audio ? 1 : 0,
             profile.has_sdcard ? 1 : 0,
             profile.has_gps_uart ? 1 : 0,
             profile.has_rs485_uart ? 1 : 0,
             profile.has_lora ? 1 : 0,
             profile.has_m5bus_lora_module_routing ? 1 : 0,
             profile.sys_i2c.port,
             profile.sys_i2c.sda,
             profile.sys_i2c.scl,
             profile.ext_i2c.port,
             profile.ext_i2c.sda,
             profile.ext_i2c.scl,
             profile.gps_uart.port,
             profile.gps_uart.tx,
             profile.gps_uart.rx,
             profile.rs485_uart.port,
             profile.rs485_uart.tx,
             profile.rs485_uart.rx,
             profile.rs485_uart.aux,
             profile.sdmmc.d0,
             profile.sdmmc.d1,
             profile.sdmmc.d2,
             profile.sdmmc.d3,
             profile.sdmmc.cmd,
             profile.sdmmc.clk,
             profile.audio_i2s.bclk,
             profile.audio_i2s.mclk,
             profile.audio_i2s.ws,
             profile.audio_i2s.dout,
             profile.audio_i2s.din,
             profile.lora_module.spi.host,
             profile.lora_module.spi.sck,
             profile.lora_module.spi.miso,
             profile.lora_module.spi.mosi,
             profile.lora_module.nss,
             profile.lora_module.rst,
             profile.lora_module.irq,
             profile.lora_module.busy,
             profile.lora_module.pwr_en,
             profile.lcd_backlight,
             profile.touch_int,
             waking_from_sleep ? 1 : 0);

    (void)::boards::tab5::Tab5Board::instance().begin();
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
    if (!out_handles)
    {
        return false;
    }

    *out_handles = resolveAppContextInitHandles();
    return out_handles->isValid();
}

inline AppContextInitHandles resolveAppContextInitHandles()
{
    return {&::boards::tab5::Tab5Board::instance(), &::boards::tab5::Tab5Board::instance(), nullptr, nullptr};
}

} // namespace platform::esp::boards::detail
