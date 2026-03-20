#pragma once

#include <cstdlib>

#include "boards/t_display_p4/board_profile.h"
#include "esp_log.h"
#include "platform/esp/boards/board_runtime.h"

namespace platform::esp::boards::detail
{

namespace
{
constexpr const char* kTag = "t-display-p4-board-runtime";
}

inline void initializeBoard(bool waking_from_sleep)
{
    ESP_LOGI(kTag,
             "T-Display-P4 board runtime bootstrap: caps(display=%d touch=%d audio=%d sd=%d gps=%d lora=%d ioexp_lora=%d) sys_i2c=(%d,%d,%d) ext_i2c=(%d,%d,%d) gps_uart=(%d,%d,%d) sdmmc=(%d,%d,%d,%d,%d,%d) audio_i2s=(%d,%d,%d,%d,%d) lora_spi=(host=%d sck=%d miso=%d mosi=%d) lora_ctrl=(nss=%d rst=%d irq=%d busy=%d pwr_en=%d) boot=%d expander_int=%d backlight=%d wake=%d",
             ::boards::t_display_p4::kBoardProfile.has_display ? 1 : 0,
             ::boards::t_display_p4::kBoardProfile.has_touch ? 1 : 0,
             ::boards::t_display_p4::kBoardProfile.has_audio ? 1 : 0,
             ::boards::t_display_p4::kBoardProfile.has_sdcard ? 1 : 0,
             ::boards::t_display_p4::kBoardProfile.has_gps_uart ? 1 : 0,
             ::boards::t_display_p4::kBoardProfile.has_lora ? 1 : 0,
             ::boards::t_display_p4::kBoardProfile.uses_io_expander_for_lora ? 1 : 0,
             ::boards::t_display_p4::kBoardProfile.sys_i2c.port,
             ::boards::t_display_p4::kBoardProfile.sys_i2c.sda,
             ::boards::t_display_p4::kBoardProfile.sys_i2c.scl,
             ::boards::t_display_p4::kBoardProfile.ext_i2c.port,
             ::boards::t_display_p4::kBoardProfile.ext_i2c.sda,
             ::boards::t_display_p4::kBoardProfile.ext_i2c.scl,
             ::boards::t_display_p4::kBoardProfile.gps_uart.port,
             ::boards::t_display_p4::kBoardProfile.gps_uart.tx,
             ::boards::t_display_p4::kBoardProfile.gps_uart.rx,
             ::boards::t_display_p4::kBoardProfile.sdmmc.d0,
             ::boards::t_display_p4::kBoardProfile.sdmmc.d1,
             ::boards::t_display_p4::kBoardProfile.sdmmc.d2,
             ::boards::t_display_p4::kBoardProfile.sdmmc.d3,
             ::boards::t_display_p4::kBoardProfile.sdmmc.cmd,
             ::boards::t_display_p4::kBoardProfile.sdmmc.clk,
             ::boards::t_display_p4::kBoardProfile.audio_i2s.bclk,
             ::boards::t_display_p4::kBoardProfile.audio_i2s.mclk,
             ::boards::t_display_p4::kBoardProfile.audio_i2s.ws,
             ::boards::t_display_p4::kBoardProfile.audio_i2s.dout,
             ::boards::t_display_p4::kBoardProfile.audio_i2s.din,
             ::boards::t_display_p4::kBoardProfile.lora.spi.host,
             ::boards::t_display_p4::kBoardProfile.lora.spi.sck,
             ::boards::t_display_p4::kBoardProfile.lora.spi.miso,
             ::boards::t_display_p4::kBoardProfile.lora.spi.mosi,
             ::boards::t_display_p4::kBoardProfile.lora.nss,
             ::boards::t_display_p4::kBoardProfile.lora.rst,
             ::boards::t_display_p4::kBoardProfile.lora.irq,
             ::boards::t_display_p4::kBoardProfile.lora.busy,
             ::boards::t_display_p4::kBoardProfile.lora.pwr_en,
             ::boards::t_display_p4::kBoardProfile.boot,
             ::boards::t_display_p4::kBoardProfile.expander_int,
             ::boards::t_display_p4::kBoardProfile.lcd_backlight,
             waking_from_sleep ? 1 : 0);
}

inline void initializeDisplay()
{
    ESP_LOGI(kTag, "T-Display-P4 display runtime not wired yet; waiting for IDF board/display adapter migration");
}

inline bool tryResolveAppContextInitHandles(AppContextInitHandles* out_handles)
{
    (void)out_handles;
    ESP_LOGI(kTag, "T-Display-P4 AppContext handles are not available yet; board adapter still pending");
    return false;
}

inline AppContextInitHandles resolveAppContextInitHandles()
{
    AppContextInitHandles handles{};
    (void)handles;
    ESP_LOGE(kTag, "T-Display-P4 resolveAppContextInitHandles() called before adapter implementation");
    abort();
}

} // namespace platform::esp::boards::detail
