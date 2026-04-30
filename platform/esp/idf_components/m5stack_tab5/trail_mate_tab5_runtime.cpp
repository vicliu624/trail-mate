#include "bsp/trail_mate_tab5_runtime.h"
#include "bsp/m5stack_tab5.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
constexpr const char* kTag = "tab5-display-runtime";
constexpr uint32_t kLvglTimerPeriodMs = 10;
constexpr int kLvglTaskStackSize = 16384;
constexpr uint32_t kFrameBufferLines = BSP_LCD_V_RES;
constexpr bool kUseDoubleBuffer = true;
constexpr int kStartupBrightnessPercent = 10;

lv_display_t* s_display = nullptr;
bool s_ready = false;
bool s_boot_screen_created = false;

void create_boot_screen()
{
    if (s_boot_screen_created || s_display == nullptr)
    {
        return;
    }

    if (!bsp_display_lock(1000))
    {
        ESP_LOGW(kTag, "Timed out waiting for LVGL lock while creating boot screen");
        return;
    }

    lv_obj_t* screen = lv_screen_active();
    if (screen == nullptr)
    {
        bsp_display_unlock();
        ESP_LOGW(kTag, "LVGL active screen is null");
        return;
    }

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x10151C), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_invalidate(screen);
    bsp_display_unlock();

    s_boot_screen_created = true;
}
} // namespace

extern "C" bool trail_mate_tab5_display_runtime_init(void)
{
    if (s_ready)
    {
        return true;
    }

    ESP_LOGI(kTag, "Initializing Tab5 display runtime");

    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(kTag, "bsp_i2c_init failed: %s", esp_err_to_name(err));
        return false;
    }

    i2c_master_bus_handle_t i2c_handle = bsp_i2c_get_handle();
    if (i2c_handle != nullptr)
    {
        bsp_io_expander_pi4ioe_init(i2c_handle);
        vTaskDelay(pdMS_TO_TICKS(20));
        bsp_reset_tp();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    else
    {
        ESP_LOGW(kTag, "System I2C handle is null; continuing without IO expander reset");
    }

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.timer_period_ms = kLvglTimerPeriodMs;
    lvgl_cfg.task_stack = kLvglTaskStackSize;

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = lvgl_cfg,
        .buffer_size = BSP_LCD_H_RES * kFrameBufferLines,
        .double_buffer = kUseDoubleBuffer,
        .flags = {
#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
            .buff_dma = false,
#else
            .buff_dma = true,
#endif
            .buff_spiram = true,
            .sw_rotate = true,
        },
    };

    s_display = bsp_display_start_with_config(&cfg);
    if (s_display == nullptr)
    {
        ESP_LOGE(kTag, "bsp_display_start_with_config failed");
        return false;
    }

    lv_display_set_rotation(s_display, LV_DISPLAY_ROTATION_90);

    err = bsp_display_brightness_set(kStartupBrightnessPercent);
    if (err != ESP_OK)
    {
        ESP_LOGW(kTag,
                 "bsp_display_brightness_set(%d) failed: %s; falling back to backlight on",
                 kStartupBrightnessPercent,
                 esp_err_to_name(err));
        err = bsp_display_backlight_on();
        if (err != ESP_OK)
        {
            ESP_LOGE(kTag, "bsp_display_backlight_on failed: %s", esp_err_to_name(err));
            return false;
        }
    }

    create_boot_screen();

    s_ready = true;
    ESP_LOGI(kTag,
             "Tab5 display runtime ready: buffer_lines=%lu double_buffer=%d timer_period_ms=%lu task_stack=%d brightness=%d",
             static_cast<unsigned long>(kFrameBufferLines),
             static_cast<int>(kUseDoubleBuffer),
             static_cast<unsigned long>(kLvglTimerPeriodMs),
             kLvglTaskStackSize,
             kStartupBrightnessPercent);
    return true;
}

extern "C" bool trail_mate_tab5_display_runtime_is_ready(void)
{
    return s_ready;
}

extern "C" bool trail_mate_tab5_display_lock(uint32_t timeout_ms)
{
    return bsp_display_lock(timeout_ms);
}

extern "C" void trail_mate_tab5_display_unlock(void)
{
    bsp_display_unlock();
}

extern "C" void trail_mate_tab5_set_ext_5v_enabled(bool enabled)
{
    bsp_set_ext_5v_en(enabled);
}

extern "C" void trail_mate_tab5_set_wifi_power_enabled(bool enabled)
{
    bsp_set_wifi_power_enable(enabled);
}
