/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_private/esp_gpio_reserve.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C"
{
#endif
    void trail_mate_idf_note_user_activity(void);
#ifdef __cplusplus
}
#endif
#include "bsp/display.h"
#include "bsp/m5stack_tab5.h"
#include "bsp/touch.h"
#include "bsp_err_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_lcd_ili9881c.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7123.h"
#include "esp_lcd_st7703.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_touch_st7123.h"
#include "esp_ldo_regulator.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
#include "usb/usb_host.h"
#include <inttypes.h>

static const char* TAG = "M5STACK_TAB5";
static uint32_t sys_i2c_generation = 0;
static bool sys_i2c_reinit_in_progress = false;
static SemaphoreHandle_t sys_i2c_reinit_mutex = NULL;

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
static lv_indev_t* disp_indev = NULL;
#endif // (BSP_CONFIG_NO_GRAPHIC_LIB == 0)

// Global uSD card handler
sdmmc_card_t* bsp_sdcard = NULL;

// USB Host Library task
static TaskHandle_t usb_host_task;

// sys i2c
static bool i2c_initialized = false;
static i2c_master_bus_handle_t i2c_handle = NULL;
// SYS I2C bus mutex to protect against concurrent access from multiple devices
// This is critical because ST7123 touch screen, Module GNSS sensors, RTC, built-in IMU, etc.
// all share the same SYS I2C bus (GPIO 31/32)
static SemaphoreHandle_t sys_i2c_mutex = NULL;
// ext i2c
static bool ext_i2c_initialized = false;
static i2c_master_bus_handle_t ext_i2c_bus_handle = NULL;
// grove i2c
static bool grove_i2c_initialized = false;
static i2c_master_bus_handle_t grove_i2c_bus_handle = NULL;
// i2s
static i2s_chan_handle_t i2s_tx_chan = NULL;
static i2s_chan_handle_t i2s_rx_chan = NULL;
static const audio_codec_data_if_t* i2s_data_if = NULL; /* Codec data interface */
static bool s_lcd_backlight_initialized = false;

//==================================================================================
// camera 设置输出时钟
//==================================================================================

esp_err_t bsp_cam_osc_init(void)
{
    ledc_timer_config_t timer_conf = {0};
    timer_conf.duty_resolution = LEDC_TIMER_1_BIT;
    timer_conf.freq_hz = 24000000; // <<<< change this to the frequency you want
    timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_conf.deconfigure = false;
    timer_conf.clk_cfg = LEDC_AUTO_CLK;
    timer_conf.timer_num = LEDC_TIMER_0;
    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ledc_timer_config failed for freq %d, rc=%x", 24000000, err);
    }

    ledc_channel_config_t ch_conf = {0};
    ch_conf.gpio_num = 36; // 摄像头时钟输�?
    ch_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_conf.channel = LEDC_CHANNEL_0;
    ch_conf.intr_type = LEDC_INTR_DISABLE;
    ch_conf.timer_sel = LEDC_TIMER_0;
    ch_conf.duty = 1;
    ch_conf.hpoint = 0;
    err = ledc_channel_config(&ch_conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ledc_channel_config failed, rc=%x", err);
    }

    return ESP_OK;
}

//==================================================================================
// i2c
//==================================================================================
esp_err_t bsp_i2c_init(void)
{
    /* I2C was initialized before */
    if (i2c_initialized)
    {
        return ESP_OK;
    }
    if (sys_i2c_reinit_mutex == NULL)
    {
        sys_i2c_reinit_mutex = xSemaphoreCreateMutex();
        if (sys_i2c_reinit_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create SYS I2C reinit mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    if (xSemaphoreTake(sys_i2c_reinit_mutex, pdMS_TO_TICKS(2000)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    sys_i2c_reinit_in_progress = true;

    // Create mutex for SYS I2C bus protection
    // This mutex protects against concurrent access from multiple devices sharing the same I2C bus
    // (ST7123 touch screen, Module GNSS sensors, RTC, built-in IMU, etc.)
    if (sys_i2c_mutex == NULL)
    {
        sys_i2c_mutex = xSemaphoreCreateMutex();
        if (sys_i2c_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create SYS I2C mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .i2c_port = BSP_I2C_NUM,
        .flags.enable_internal_pullup = false, // Disable internal pullup - hardware has external pullup resistors
    };
    BSP_ERROR_CHECK_RETURN_ERR(i2c_new_master_bus(&i2c_bus_conf, &i2c_handle));
    sys_i2c_generation++;

    // Suppress I2C driver error logs for normal operation
    // I2C transaction failures can occur during normal operation (e.g., when touch screen has no data)
    // Set log level to NONE to suppress ALL I2C logs including NACK/timeout errors
    // These errors are expected during normal operation when devices are busy or have no data
    // Touch panel I2C errors are handled by the touch driver with rate-limiting
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);

    i2c_initialized = true;
    sys_i2c_reinit_in_progress = false;
    xSemaphoreGive(sys_i2c_reinit_mutex);

    return ESP_OK;
}

esp_err_t bsp_i2c_deinit(void)
{
    if (sys_i2c_reinit_mutex != NULL)
    {
        if (xSemaphoreTake(sys_i2c_reinit_mutex, pdMS_TO_TICKS(2000)) != pdTRUE)
        {
            return ESP_ERR_TIMEOUT;
        }
        sys_i2c_reinit_in_progress = true;
    }
    BSP_ERROR_CHECK_RETURN_ERR(i2c_del_master_bus(i2c_handle));
    i2c_initialized = false;
    sys_i2c_generation++;
    sys_i2c_reinit_in_progress = false;
    if (sys_i2c_reinit_mutex != NULL)
    {
        xSemaphoreGive(sys_i2c_reinit_mutex);
    }
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_get_handle(void)
{
    // CRITICAL: Ensure I2C is initialized before returning handle
    // This prevents crashes when handle is NULL
    if (!i2c_initialized || i2c_handle == NULL)
    {
        ESP_LOGE(TAG, "I2C bus not initialized, initializing now");
        if (bsp_i2c_init() != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize I2C bus");
            return NULL;
        }
    }
    static uint32_t last_log_ms = 0;
    uint32_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - last_log_ms >= 5000)
    {
        ESP_LOGI(TAG, "bsp_i2c_get_handle: handle=%p gen=%" PRIu32 " reinit=%d",
                 (void*)i2c_handle, sys_i2c_generation, sys_i2c_reinit_in_progress);
        last_log_ms = now_ms;
    }
    return i2c_handle;
}

uint32_t bsp_i2c_get_generation(void)
{
    return sys_i2c_generation;
}

bool bsp_i2c_is_reinit_in_progress(void)
{
    return sys_i2c_reinit_in_progress;
}

bool bsp_i2c_lock(uint32_t timeout_ms)
{
    if (sys_i2c_mutex == NULL)
    {
        // Mutex not initialized yet, try to initialize I2C first
        if (bsp_i2c_init() != ESP_OK)
        {
            return false;
        }
    }

    TickType_t timeout_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms == UINT32_MAX)
    {
        timeout_ticks = portMAX_DELAY;
    }

    return xSemaphoreTake(sys_i2c_mutex, timeout_ticks) == pdTRUE;
}

void bsp_i2c_unlock(void)
{
    if (sys_i2c_mutex != NULL)
    {
        xSemaphoreGive(sys_i2c_mutex);
    }
}

esp_err_t bsp_ext_i2c_init(void)
{
    if (ext_i2c_initialized)
    {
        return ESP_OK;
    }

    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BSP_EXT_I2C_NUM,
        .scl_io_num = BSP_EXT_I2C_SCL,
        .sda_io_num = BSP_EXT_I2C_SDA,
        .flags.enable_internal_pullup = false, // Disable internal pullup - hardware has external pullup resistors
    };
    i2c_new_master_bus(&i2c_mst_config, &ext_i2c_bus_handle);

    ext_i2c_initialized = true;

    return ESP_OK;
}

esp_err_t bsp_ext_i2c_deinit(void)
{
    ext_i2c_initialized = false;
    return i2c_del_master_bus(ext_i2c_bus_handle);
}

i2c_master_bus_handle_t bsp_ext_i2c_get_handle(void)
{
    return ext_i2c_bus_handle;
}

esp_err_t bsp_grove_i2c_init(void)
{
    if (grove_i2c_initialized)
    {
        return ESP_OK;
    }

    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BSP_EXT_I2C_NUM,
        .scl_io_num = 54,                      // BSP_EXT_I2C_SCL,
        .sda_io_num = 53,                      // BSP_EXT_I2C_SDA,
        .flags.enable_internal_pullup = false, // Disable internal pullup - hardware has external pullup resistors
    };
    i2c_new_master_bus(&i2c_mst_config, &grove_i2c_bus_handle);

    grove_i2c_initialized = true;

    return ESP_OK;
}

esp_err_t bsp_grove_i2c_deinit(void)
{
    grove_i2c_initialized = false;
    return i2c_del_master_bus(grove_i2c_bus_handle);
}

i2c_master_bus_handle_t bsp_grove_i2c_get_handle(void)
{
    return grove_i2c_bus_handle;
}

esp_err_t bsp_i2c_scan()
{
    esp_err_t ret;
    uint8_t address;

    // Lock SYS I2C bus before scanning to prevent conflicts
    if (!bsp_i2c_lock(1000))
    { // 1 second timeout
        ESP_LOGE(TAG, "Failed to lock I2C bus for scan");
        return ESP_ERR_TIMEOUT;
    }

    printf("scan i2c device\n");
    printf("\n     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16)
    {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++)
        {
            fflush(stdout);
            address = i + j;
            ret = i2c_master_probe(i2c_handle, address, 50);
            if (ret == ESP_OK)
            {
                printf("%02x ", address);
            }
            else if (ret == ESP_ERR_TIMEOUT)
            {
                printf("UU ");
            }
            else
            {
                printf("-- ");
            }
        }
        printf("\r\n");
    }
    printf("\nscan i2c device finished\n");

    bsp_i2c_unlock();
    return ESP_OK;
}

//==================================================================================
// I/O Exapnder PI4IOE5V6416
//==================================================================================
#define I2C_DEV_ADDR_PI4IOE1 0x43 // addr pin low
#define I2C_DEV_ADDR_PI4IOE2 0x44 // addr pin high
#define I2C_MASTER_TIMEOUT_MS 50

static i2c_master_dev_handle_t i2c_dev_handle_pi4ioe1;
static i2c_master_dev_handle_t i2c_dev_handle_pi4ioe2;

// PI4IO registers
#define PI4IO_REG_CHIP_RESET 0x01
#define PI4IO_REG_IO_DIR 0x03
#define PI4IO_REG_OUT_SET 0x05
#define PI4IO_REG_OUT_H_IM 0x07
#define PI4IO_REG_IN_DEF_STA 0x09
#define PI4IO_REG_PULL_EN 0x0B
#define PI4IO_REG_PULL_SEL 0x0D
#define PI4IO_REG_IN_STA 0x0F
#define PI4IO_REG_INT_MASK 0x11
#define PI4IO_REG_IRQ_STA 0x13

#define setbit(x, y) x |= (0x01 << y)
#define clrbit(x, y) x &= ~(0x01 << y)

void bsp_io_expander_pi4ioe_init(i2c_master_bus_handle_t bus_handle)
{
    uint8_t write_buf[2] = {0};
    uint8_t read_buf[1] = {0};

    /* */
    i2c_device_config_t dev_cfg1 = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_DEV_ADDR_PI4IOE1,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg1, &i2c_dev_handle_pi4ioe1));

    // Lock SYS I2C bus for initialization
    if (!bsp_i2c_lock(1000))
    {
        ESP_LOGE(TAG, "Failed to lock I2C bus for PI4IOE1 init");
        return;
    }

    write_buf[0] = PI4IO_REG_CHIP_RESET;
    write_buf[1] = 0xFF;
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_CHIP_RESET;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe1, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_IO_DIR;
    write_buf[1] = 0b01111111;
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS); // 0: input 1: output
    write_buf[0] = PI4IO_REG_OUT_H_IM;
    write_buf[1] = 0b00000000;
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2,
                        I2C_MASTER_TIMEOUT_MS); // 使用到的引脚关闭 High-Impedance
    write_buf[0] = PI4IO_REG_PULL_SEL;
    write_buf[1] = 0b01111111;
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2,
                        I2C_MASTER_TIMEOUT_MS); // pull up/down select, 0 down, 1 up
    write_buf[0] = PI4IO_REG_PULL_EN;
    write_buf[1] = 0b01111111;

    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2,
                        I2C_MASTER_TIMEOUT_MS); // P7 中断使能 0 enable, 1 disable
    /* Output Port Register P1(SPK_EN), P2(EXT5V_EN), P4(LCD_RST), P5(TP_RST), P6(CAM)RST 输出高电�?*/
    write_buf[0] = PI4IO_REG_OUT_SET;
    write_buf[1] = 0b01110110;
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS);

    /* */
    i2c_device_config_t dev_cfg2 = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_DEV_ADDR_PI4IOE2,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg2, &i2c_dev_handle_pi4ioe2));

    write_buf[0] = PI4IO_REG_CHIP_RESET;
    write_buf[1] = 0xFF;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_CHIP_RESET;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe2, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);
    write_buf[0] = PI4IO_REG_IO_DIR;
    write_buf[1] = 0b10111001;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS); // 0: input 1: output
    write_buf[0] = PI4IO_REG_OUT_H_IM;
    write_buf[1] = 0b00000110;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2,
                        I2C_MASTER_TIMEOUT_MS); // 使用到的引脚关闭 High-Impedance
    write_buf[0] = PI4IO_REG_PULL_SEL;
    write_buf[1] = 0b10111001;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2,
                        I2C_MASTER_TIMEOUT_MS); // pull up/down select, 0 down, 1 up
    write_buf[0] = PI4IO_REG_PULL_EN;
    write_buf[1] = 0b11111001;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2,
                        I2C_MASTER_TIMEOUT_MS); // pull up/down enable, 0 disable, 1 enable
    write_buf[0] = PI4IO_REG_IN_DEF_STA;
    write_buf[1] = 0b01000000;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS); // P6 默认高电�?
    write_buf[0] = PI4IO_REG_INT_MASK;
    write_buf[1] = 0b10111111;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2,
                        I2C_MASTER_TIMEOUT_MS); // P6 中断使能 0 enable, 1 disable
    /* Output Port Register P0(WLAN_PWR_EN), P3(USB5V_EN), P7(CHG_EN) 输出高电�?*/
    write_buf[0] = PI4IO_REG_OUT_SET;
    // write_buf[1] = 0b10001001;
    write_buf[1] = 0b00001001;
    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);

    bsp_i2c_unlock();
}

void bsp_set_charge_qc_en(bool en)
{
    uint8_t write_buf[2] = {0};
    uint8_t read_buf[1] = {0};

    // Lock SYS I2C bus before accessing PI4IOE2
    if (!bsp_i2c_lock(100))
    {
        ESP_LOGE(TAG, "Failed to lock I2C bus for bsp_set_charge_qc_en");
        return;
    }

    write_buf[0] = PI4IO_REG_OUT_SET;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe2, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);

    write_buf[0] = PI4IO_REG_OUT_SET;
    write_buf[1] = read_buf[0];
    if (en)
    {
        clrbit(write_buf[1], 5);
    }
    else
    {
        setbit(write_buf[1], 5);
    }

    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);

    bsp_i2c_unlock();
}

void bsp_set_charge_en(bool en)
{
    uint8_t write_buf[2] = {0};
    uint8_t read_buf[1] = {0};

    // Lock SYS I2C bus before accessing PI4IOE2
    if (!bsp_i2c_lock(100))
    {
        ESP_LOGE(TAG, "Failed to lock I2C bus for bsp_set_charge_en");
        return;
    }

    write_buf[0] = PI4IO_REG_OUT_SET;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe2, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);

    write_buf[0] = PI4IO_REG_OUT_SET;
    write_buf[1] = read_buf[0];
    if (en)
    {
        setbit(write_buf[1], 7);
    }
    else
    {
        clrbit(write_buf[1], 7);
    }

    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);

    bsp_i2c_unlock();
}

void bsp_set_usb_5v_en(bool en)
{
    uint8_t write_buf[2] = {0};
    uint8_t read_buf[1] = {0};

    // Lock SYS I2C bus before accessing PI4IOE2
    if (!bsp_i2c_lock(100))
    {
        ESP_LOGE(TAG, "Failed to lock I2C bus for bsp_set_usb_5v_en");
        return;
    }

    write_buf[0] = PI4IO_REG_OUT_SET;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe2, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);

    write_buf[0] = PI4IO_REG_OUT_SET;
    write_buf[1] = read_buf[0];
    if (en)
    {
        setbit(write_buf[1], 3);
    }
    else
    {
        clrbit(write_buf[1], 3);
    }

    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);

    bsp_i2c_unlock();
}

void bsp_set_ext_5v_en(bool en)
{
    uint8_t write_buf[2] = {0};
    uint8_t read_buf[1] = {0};

    // Lock SYS I2C bus before accessing PI4IOE1
    if (!bsp_i2c_lock(100))
    {
        ESP_LOGE(TAG, "Failed to lock I2C bus for bsp_set_ext_5v_en");
        return;
    }

    write_buf[0] = PI4IO_REG_OUT_SET;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe1, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);

    write_buf[0] = PI4IO_REG_OUT_SET;
    write_buf[1] = read_buf[0];
    if (en)
    {
        setbit(write_buf[1], 2);
    }
    else
    {
        clrbit(write_buf[1], 2);
    }

    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS);

    bsp_i2c_unlock();
}

void bsp_generate_poweroff_signal()
{
    ESP_LOGW(TAG, "Generate poweroff signal!");
    uint8_t write_buf[2] = {0};
    uint8_t read_buf[1] = {0};

    // Lock SYS I2C bus before accessing PI4IOE2
    if (!bsp_i2c_lock(1000))
    {
        ESP_LOGE(TAG, "Failed to lock I2C bus for bsp_generate_poweroff_signal");
        return;
    }

    write_buf[0] = PI4IO_REG_OUT_SET;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe2, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);

    write_buf[1] = read_buf[0];

    // Try to generate poweroff signal 3 times to make sure it works :)
    for (int i = 0; i < 3; i++)
    {
        setbit(write_buf[1], 4);
        i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
        vTaskDelay(100 / portTICK_PERIOD_MS);

        clrbit(write_buf[1], 4);
        i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    bsp_i2c_unlock();
}

bool bsp_headphone_detect()
{
    uint8_t write_buf[2] = {0};
    uint8_t read_buf[1] = {0};

    // Lock SYS I2C bus before accessing PI4IOE1
    if (!bsp_i2c_lock(100))
    {
        ESP_LOGE(TAG, "Failed to lock I2C bus for bsp_headphone_detect");
        return false;
    }

    write_buf[0] = PI4IO_REG_IN_STA;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe1, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);

    // printf("get %02x\n", read_buf[0]);

    // Get bit 8
    bool ret = false;
    if (read_buf[0] & 0b10000000)
    {
        ret = true;
    }

    bsp_i2c_unlock();
    return ret;
}

bool bsp_usb_c_detect()
{
    uint8_t write_buf[2] = {0};
    uint8_t read_buf[1] = {0};

    // Lock SYS I2C bus before accessing PI4IOE2
    if (!bsp_i2c_lock(100))
    {
        ESP_LOGE(TAG, "Failed to lock I2C bus for bsp_usb_c_detect");
        return false;
    }

    write_buf[0] = PI4IO_REG_IN_STA;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe2, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);

    // printf("get %02x\n", read_buf[0]);

    // Get bit 6
    bool ret = false;
    if (read_buf[0] & 0b01000000)
    {
        ret = true;
    }

    bsp_i2c_unlock();
    return ret;
}

void bsp_set_ext_antenna_enable(bool en)
{
    uint8_t write_buf[2] = {0};
    uint8_t read_buf[1] = {0};

    // Lock SYS I2C bus before accessing PI4IOE1
    if (!bsp_i2c_lock(100))
    {
        ESP_LOGE(TAG, "Failed to lock I2C bus for bsp_set_ext_antenna_enable");
        return;
    }

    write_buf[0] = PI4IO_REG_OUT_SET;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe1, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);

    write_buf[0] = PI4IO_REG_OUT_SET;
    write_buf[1] = read_buf[0];
    if (en)
    {
        setbit(write_buf[1], 0);
    }
    else
    {
        clrbit(write_buf[1], 0);
    }

    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS);

    bsp_i2c_unlock();
}

void bsp_set_wifi_power_enable(bool en)
{
    uint8_t write_buf[2] = {0};
    uint8_t read_buf[1] = {0};

    ESP_LOGI(TAG, "set_wifi_power_enable: %d", en);

    // Lock SYS I2C bus before accessing PI4IOE2
    if (!bsp_i2c_lock(100))
    {
        ESP_LOGE(TAG, "Failed to lock I2C bus for bsp_set_wifi_power_enable");
        return;
    }

    write_buf[0] = PI4IO_REG_OUT_SET;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe2, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);

    write_buf[0] = PI4IO_REG_OUT_SET;
    write_buf[1] = read_buf[0];
    if (en)
    {
        setbit(write_buf[1], 0);
    }
    else
    {
        clrbit(write_buf[1], 0);
    }

    i2c_master_transmit(i2c_dev_handle_pi4ioe2, write_buf, 2, I2C_MASTER_TIMEOUT_MS);

    write_buf[0] = PI4IO_REG_OUT_SET;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe2, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);
    printf("0x%02X: %02x\n", PI4IO_REG_OUT_SET, read_buf[0]);

    bsp_i2c_unlock();
}

void bsp_reset_tp()
{
    ESP_LOGI(TAG, "reset tp");

    ESP_LOGI(TAG, "reset gpio %d", GPIO_NUM_23);
    gpio_reset_pin(GPIO_NUM_23);

    uint8_t write_buf[2] = {0};
    uint8_t read_buf[1] = {0};

    // Lock SYS I2C bus before accessing PI4IOE1
    if (!bsp_i2c_lock(1000))
    {
        ESP_LOGE(TAG, "Failed to lock I2C bus for bsp_reset_tp");
        return;
    }

    write_buf[0] = PI4IO_REG_OUT_SET;
    i2c_master_transmit_receive(i2c_dev_handle_pi4ioe1, write_buf, 1, read_buf, 1, I2C_MASTER_TIMEOUT_MS);

    write_buf[0] = PI4IO_REG_OUT_SET;
    write_buf[1] = read_buf[0];
    clrbit(write_buf[1], 4);
    clrbit(write_buf[1], 5);
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    write_buf[0] = PI4IO_REG_OUT_SET;
    write_buf[1] = read_buf[0];
    setbit(write_buf[1], 4);
    setbit(write_buf[1], 5);
    i2c_master_transmit(i2c_dev_handle_pi4ioe1, write_buf, 2, I2C_MASTER_TIMEOUT_MS);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    bsp_i2c_unlock();
}

//==================================================================================
// sd card
//==================================================================================
#define BSP_LDO_PROBE_SD_CHAN 4
#define BSP_LDO_PROBE_SD_VOLTAGE_MV 3300

#define SDMMC_BUS_WIDTH (4)          // SDIO 4 线模�?
#define GPIO_SDMMC_DET (GPIO_NUM_NC) // SDIO 卡检�?
// M5Stack-Tab5-P4
#define GPIO_SDMMC_CLK (GPIO_NUM_43) // SDIO 时钟
#define GPIO_SDMMC_CMD (GPIO_NUM_44) // SDIO 命令
#define GPIO_SDMMC_D0 (GPIO_NUM_39)  // SDIO 数据 0
#define GPIO_SDMMC_D1 (GPIO_NUM_40)  // SDIO 数据 1
#define GPIO_SDMMC_D2 (GPIO_NUM_41)  // SDIO 数据 2
#define GPIO_SDMMC_D3 (GPIO_NUM_42)  // SDIO 数据 3

static sdmmc_card_t* card;

esp_err_t bsp_sdcard_init(char* mount_point, size_t max_files)
{
    esp_err_t ret_val = ESP_OK;

    if (NULL != card)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /**
     * @brief Use settings defined above to initialize SD card and mount FAT filesystem.
     *   Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
     *   Please check its source code and implement error recovery when developing
     *   production applications.
     *
     */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0; //
    // host.slot = SDMMC_HOST_SLOT_1; //
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = BSP_LDO_PROBE_SD_CHAN, // `LDO_VO4` is used as the SDMMC IO power
    };
    static sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

    if (pwr_ctrl_handle == NULL)
    {
        ret_val = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
        if (ret_val != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to new an on-chip ldo power control driver");
            return ret_val;
        }
    }
    host.pwr_ctrl_handle = pwr_ctrl_handle;

    /**
     * @brief This initializes the slot without card detect (CD) and write protect (WP) signals.
     *   Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
     *
     */
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = SDMMC_BUS_WIDTH;
    slot_config.clk = GPIO_SDMMC_CLK;
    slot_config.cmd = GPIO_SDMMC_CMD;
    slot_config.d0 = GPIO_SDMMC_D0;
    slot_config.d1 = GPIO_SDMMC_D1;
    slot_config.d2 = GPIO_SDMMC_D2;
    slot_config.d3 = GPIO_SDMMC_D3;
    // slot_config.cd = GPIO_SDMMC_DET;
    // slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    /**
     * @brief Options for mounting the filesystem.
     *   If format_if_mount_failed is set to true, SD card will be partitioned and
     *   formatted in case when mounting fails.
     */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, .max_files = max_files, .allocation_unit_size = 16 * 1024};

    ret_val = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    /* Check for SDMMC mount result. */
    if (ret_val != ESP_OK)
    {
        if (ret_val == ESP_FAIL)
        {
            ESP_LOGE(TAG,
                     "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        }
        else
        {
            ESP_LOGE(TAG,
                     "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.",
                     esp_err_to_name(ret_val));
        }
        return ret_val;
    }

    /* Card has been initialized, print its properties. */
    sdmmc_card_print_info(stdout, card);

    return ret_val;
}

esp_err_t bsp_sdcard_deinit(char* mount_point)
{
    if (mount_point == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* Unmount an SD card from the FAT filesystem and release resources acquired */
    esp_err_t ret_val = esp_vfs_fat_sdcard_unmount(mount_point, card);

    // ret_val = sd_pwr_ctrl_del_on_chip_ldo(card->host.pwr_ctrl_handle);
    // if (ret_val != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to delete on-chip ldo power control driver");
    // }

    /* Make SD/MMC card information structure pointer NULL */
    card = NULL;

    return ret_val;
}

//==================================================================================
// spiffs
//==================================================================================
esp_err_t bsp_spiffs_mount(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_BSP_SPIFFS_MOUNT_POINT,
        .partition_label = CONFIG_BSP_SPIFFS_PARTITION_LABEL,
        .max_files = CONFIG_BSP_SPIFFS_MAX_FILES,
#ifdef CONFIG_BSP_SPIFFS_FORMAT_ON_MOUNT_FAIL
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif
    };

    esp_err_t ret_val = esp_vfs_spiffs_register(&conf);

    BSP_ERROR_CHECK_RETURN_ERR(ret_val);

    size_t total = 0, used = 0;
    ret_val = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret_val != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret_val));
    }
    else
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    return ret_val;
}

esp_err_t bsp_spiffs_unmount(void)
{
    return esp_vfs_spiffs_unregister(CONFIG_BSP_SPIFFS_PARTITION_LABEL);
}

//==================================================================================
// audio es7210 + es8388
//==================================================================================
static esp_codec_dev_handle_t play_dev_handle;
static esp_codec_dev_handle_t record_dev_handle;
static bsp_codec_config_t g_codec_handle;
static int volume;

/* Can be used for `i2s_std_gpio_config_t` and/or `i2s_std_config_t` initialization */
#define BSP_I2S_GPIO_CFG                                                                                           \
    {                                                                                                              \
        .mclk = BSP_I2S_MCLK, .bclk = BSP_I2S_SCLK, .ws = BSP_I2S_LCLK, .dout = BSP_I2S_DOUT, .din = BSP_I2S_DSIN, \
        .invert_flags = {                                                                                          \
            .mclk_inv = false,                                                                                     \
            .bclk_inv = false,                                                                                     \
            .ws_inv = false,                                                                                       \
        },                                                                                                         \
    }

/* This configuration is used by default in `bsp_extra_audio_init()` */
#define BSP_I2S_DUPLEX_MONO_CFG(_sample_rate)                                                         \
    {                                                                                                 \
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(_sample_rate),                                          \
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), \
        .gpio_cfg = BSP_I2S_GPIO_CFG,                                                                 \
    }

esp_err_t bsp_audio_init(const i2s_std_config_t* i2s_config)
{
    if (i2s_tx_chan && i2s_rx_chan)
    {
        /* Audio was initialized before */
        return ESP_OK;
    }

    /* Setup I2S peripheral */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // Auto clear the legacy data in the DMA buffer
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_chan, &i2s_rx_chan));

    /* Setup I2S channels */
    // const i2s_std_config_t std_cfg_default = BSP_I2S_DUPLEX_MONO_CFG(16000);
    const i2s_std_config_t std_cfg_default = BSP_I2S_DUPLEX_MONO_CFG(48000);
    const i2s_std_config_t* p_i2s_cfg = &std_cfg_default;
    if (i2s_config != NULL)
    {
        p_i2s_cfg = i2s_config;
    }

    if (i2s_tx_chan != NULL)
    {
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_chan, p_i2s_cfg));
        ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_chan));
    }

    // if (i2s_rx_chan != NULL) {
    //     ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_rx_chan, p_i2s_cfg));
    //     ESP_ERROR_CHECK(i2s_channel_enable(i2s_rx_chan));
    // }

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg =
            {
                .sample_rate_hz = (uint32_t)48000,
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .ext_clk_freq_hz = 0,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                .bclk_div = 8,
            },
        .slot_cfg = {.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                     .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                     .slot_mode = I2S_SLOT_MODE_STEREO,
                     .slot_mask = (I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
                     .ws_width = I2S_TDM_AUTO_WS_WIDTH,
                     .ws_pol = false,
                     .bit_shift = true,
                     .left_align = false,
                     .big_endian = false,
                     .bit_order_lsb = false,
                     .skip_mask = false,
                     .total_slot = I2S_TDM_AUTO_SLOT_NUM},
        .gpio_cfg = BSP_I2S_GPIO_CFG,
    };

    if (i2s_rx_chan != NULL)
    {
        ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(i2s_rx_chan, &tdm_cfg));
        ESP_ERROR_CHECK(i2s_channel_enable(i2s_rx_chan));
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = CONFIG_BSP_I2S_NUM,
        .tx_handle = i2s_tx_chan,
        .rx_handle = i2s_rx_chan,
    };
    i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);

    return ESP_OK;
}

esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void)
{
    static esp_codec_dev_handle_t codec = NULL;
    if (codec)
    {
        return codec;
    }

    if (i2s_data_if == NULL)
    {
        /* Initilize I2C */
        bsp_i2c_init();
        /* Configure I2S peripheral and Power Amplifier */
        bsp_audio_init(NULL);
    }
    assert(i2s_data_if);

    // const audio_codec_gpio_if_t* gpio_if = audio_codec_new_gpio();  // Not used in current implementation

    i2c_master_bus_handle_t i2c_bus_handle = bsp_i2c_get_handle();
    audio_codec_i2c_cfg_t i2c_cfg = {
        // .port       = BSP_I2C_NUM,  // Commented out to avoid legacy I2C driver conflict
        .addr = ES8388_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus_handle,
    };
    const audio_codec_ctrl_if_t* i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    BSP_NULL_CHECK(i2c_ctrl_if, NULL);

    // esp_codec_dev_hw_gain_t gain = {  // Not used in current implementation
    //     .pa_voltage        = 5.0,
    //     .codec_dac_voltage = 3.3,
    // };

    es8388_codec_cfg_t es8388_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .master_mode = false,
        .ctrl_if = i2c_ctrl_if,
        .pa_pin = -1, // PI4IOE1 P1 控制
    };
    const audio_codec_if_t* es8388_dev = es8388_codec_new(&es8388_cfg);
    BSP_NULL_CHECK(es8388_dev, NULL);

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8388_dev,
        .data_if = i2s_data_if,
    };
    codec = esp_codec_dev_new(&codec_dev_cfg);
    BSP_NULL_CHECK(codec, NULL);

    return codec;
}

esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void)
{
    if (i2s_data_if == NULL)
    {
        /* Initilize I2C */
        ESP_ERROR_CHECK(bsp_i2c_init());
        /* Configure I2S peripheral and Power Amplifier */
        ESP_ERROR_CHECK(bsp_audio_init(NULL));
        // i2s_data_if = bsp_get_codec_data_if();
    }
    assert(i2s_data_if);

    i2c_master_bus_handle_t i2c_bus_handle = bsp_i2c_get_handle();
    audio_codec_i2c_cfg_t i2c_cfg = {
        // .port       = BSP_I2C_NUM,  // Commented out to avoid legacy I2C driver conflict
        .addr = ES7210_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus_handle,
    };
    const audio_codec_ctrl_if_t* i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    BSP_NULL_CHECK(i2c_ctrl_if, NULL);

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = i2c_ctrl_if, // Codec Control interface
    };
    es7210_cfg.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4;
    const audio_codec_if_t* es7210_dev = es7210_codec_new(&es7210_cfg);
    BSP_NULL_CHECK(es7210_dev, NULL);

    esp_codec_dev_cfg_t codec_es7210_dev_cfg = {
        .dev_type =
            ESP_CODEC_DEV_TYPE_IN, // Codec device type: Codec input device like ADC (capture data from microphone)
        .codec_if = es7210_dev,    // Codec interface
        .data_if = i2s_data_if,    // Codec data interface
    };

    return esp_codec_dev_new(&codec_es7210_dev_cfg);
}

static esp_err_t bsp_i2s_read(void* audio_buffer, size_t len, size_t* bytes_read, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;
    ret = esp_codec_dev_read(record_dev_handle, audio_buffer, len);
    *bytes_read = len;
    return ret;
}

static esp_err_t bsp_i2s_write(void* audio_buffer, size_t len, size_t* bytes_written, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;
    ret = esp_codec_dev_write(play_dev_handle, audio_buffer, len);
    *bytes_written = len;
    return ret;
}

static esp_err_t bsp_codec_set_in_gain(float gain)
{
    return esp_codec_dev_set_in_gain(record_dev_handle, gain);
}

static esp_err_t bsp_codec_set_mute(bool enable)
{
    esp_err_t ret = ESP_OK;
    ret = esp_codec_dev_set_out_mute(play_dev_handle, enable);
    return ret;
}

static esp_err_t bsp_codec_set_volume(int v)
{
    esp_err_t ret = ESP_OK;

    if (v <= 0)
    {
        volume = 0;
        ret = esp_codec_dev_set_out_mute(play_dev_handle, true);
    }
    else
    {
        volume = v;
        ret = esp_codec_dev_set_out_mute(play_dev_handle, false);
        ret |= esp_codec_dev_set_out_vol(play_dev_handle, volume);
    }

    return ret;
}

static int bsp_codec_get_volume(void)
{
    return volume;
}

bsp_codec_config_t* bsp_get_codec_handle(void)
{
    return &g_codec_handle;
}

static esp_err_t bsp_codec_es8388_set(uint32_t rate, uint32_t bps, i2s_slot_mode_t ch)
{
    esp_err_t ret = ESP_OK;

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = ch,
        .bits_per_sample = bps,
    };

    if (play_dev_handle)
    {
        ret = esp_codec_dev_close(play_dev_handle);
    }
    ret = esp_codec_dev_open(play_dev_handle, &fs);

    return ret;
}

static esp_err_t bsp_codec_es7210_set(uint32_t rate, uint32_t bps, i2s_slot_mode_t ch)
{
    esp_err_t ret = ESP_OK;

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = ch,
        .bits_per_sample = bps,
    };

    if (record_dev_handle)
    {
        ret = esp_codec_dev_close(record_dev_handle);
    }
    ret = esp_codec_dev_open(record_dev_handle, &fs);

    // esp_codec_dev_set_in_gain(record_dev_handle, 80.0); // Set codec input gain

    return ret;
}

void bsp_codec_init(void)
{
    // CRITICAL: Lock I2C bus during codec initialization to prevent conflicts
    // Audio codecs (ES8388/ES7210) initialization requires extensive I2C register access
    // This prevents conflicts with other devices (BMI270, touch screen, etc.) during initialization
    if (!bsp_i2c_lock(2000))
    { // 2 second timeout for initialization
        ESP_LOGE(TAG, "Failed to lock I2C bus for codec initialization");
        return;
    }

    play_dev_handle = bsp_audio_codec_speaker_init();
    assert((play_dev_handle) && "play_dev_handle not initialized");

    record_dev_handle = bsp_audio_codec_microphone_init();
    assert((record_dev_handle) && "record_dev_handle not initialized");

    // bsp_codec_es7210_set(16000, 16, 2);
    // bsp_codec_es8388_set(16000, 16, 2);
    // bsp_codec_es7210_set(48000, 16, 2);
    bsp_codec_es7210_set(48000, 16, 4);
    // Initialize ES8388 with default 48000, audio_player will reconfigure if needed
    // This ensures codec is opened, but audio_player can change sample rate based on audio file
    bsp_codec_es8388_set(48000, 16, 2);

    /* 初始�?codec handle */
    bsp_codec_config_t* codec_cfg = bsp_get_codec_handle(); // 获取 codec handle
    codec_cfg->i2s_read = bsp_i2s_read;                     // I2S 读数�?
    codec_cfg->i2s_write = bsp_i2s_write;                   // I2S 写数�?
    codec_cfg->set_mute = bsp_codec_set_mute;               // 静音设置
    codec_cfg->set_volume = bsp_codec_set_volume;           // 音量设置
    codec_cfg->get_volume = bsp_codec_get_volume;
    codec_cfg->set_in_gain = bsp_codec_set_in_gain; // 麦克风输入增益设�?
    codec_cfg->codec_reconfig_fn = bsp_codec_es7210_set;
    codec_cfg->i2s_reconfig_clk_fn = bsp_codec_es8388_set;

    codec_cfg->set_volume(10);

    // Unlock I2C bus after codec initialization is complete
    bsp_i2c_unlock();
}

uint8_t bsp_codec_feed_channel(void)
{
    return 3; // 2*mic_num + ref_num
}

//==================================================================================
// 显示屏类型检�?
//==================================================================================
typedef enum
{
    BSP_DISPLAY_TYPE_UNKNOWN = 0,
    BSP_DISPLAY_TYPE_ST7703_GT911,
    BSP_DISPLAY_TYPE_ST7123
} bsp_display_type_t;

static bsp_display_type_t bsp_detect_display_type(void)
{
    esp_err_t ret;

    // 确保I2C已初始化
    if (bsp_i2c_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C init failed for display detection");
        return BSP_DISPLAY_TYPE_UNKNOWN;
    }

    // 检测GT911触摸�?(ST7703配套)
    ret = i2c_master_probe(i2c_handle, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, 50);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Detected GT911 touch controller, using ST7703 display");
        return BSP_DISPLAY_TYPE_ST7703_GT911;
    }

    // 检测ST7123触摸�?
    ret = i2c_master_probe(i2c_handle, 0x55, 50);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Detected ST7123 touch controller, using ST7123 display");
        return BSP_DISPLAY_TYPE_ST7123;
    }

    ESP_LOGW(TAG, "No known touch controller detected, defaulting to ST7703");
    return BSP_DISPLAY_TYPE_ST7703_GT911;
}

//==================================================================================
// lcd st7703 1280x720  gt911
//==================================================================================
// Bit number used to represent command and parameter
#define LCD_LEDC_CH LEDC_CHANNEL_1 // CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH
esp_err_t bsp_display_brightness_init(void)
{
    if (s_lcd_backlight_initialized)
    {
        return ESP_OK;
    }

    // gpio_config_t io_conf = {};

    // io_conf.intr_type = GPIO_INTR_DISABLE;   //disable interrupt
    // io_conf.mode = GPIO_MODE_OUTPUT;         //set as output mode
    // io_conf.pin_bit_mask = 1 << BSP_LCD_BACKLIGHT; //select pin
    // io_conf.pull_down_en = 0;                //disable pull-down mode
    // io_conf.pull_up_en = 0;                  //disable pull-up mode
    // gpio_config(&io_conf);                   //configure GPIO with the given settings

    // gpio_set_level(BSP_LCD_BACKLIGHT, 1);

    // Setup LEDC peripheral for PWM backlight control
    const ledc_timer_config_t lcd_backlight_timer = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                                     //  .duty_resolution = LEDC_TIMER_10_BIT,
                                                     .duty_resolution = LEDC_TIMER_12_BIT,
                                                     .timer_num = LEDC_TIMER_0,
                                                     .freq_hz = 5000,
                                                     // .freq_hz = 20000,
                                                     .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&lcd_backlight_timer));

    const ledc_channel_config_t lcd_backlight_channel = {.gpio_num = BSP_LCD_BACKLIGHT,
                                                         .speed_mode = LEDC_LOW_SPEED_MODE,
                                                         .channel = LCD_LEDC_CH,
                                                         .intr_type = LEDC_INTR_DISABLE,
                                                         .timer_sel = LEDC_TIMER_0,
                                                         .duty = 0,
                                                         .hpoint = 0};

    esp_gpio_revoke(BIT64(BSP_LCD_BACKLIGHT));
    ESP_ERROR_CHECK(ledc_channel_config(&lcd_backlight_channel));

    s_lcd_backlight_initialized = true;
    return ESP_OK;
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100)
    {
        brightness_percent = 100;
    }
    if (brightness_percent < 0)
    {
        brightness_percent = 0;
    }

    ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness_percent);
    // uint32_t duty_cycle = (1023 * brightness_percent) / 100; // LEDC resolution set to 10bits, thus: 100% = 1023
    uint32_t duty_cycle = (4095 * brightness_percent) / 100; // LEDC resolution set to 12bits, thus: 100% = 4095
    BSP_ERROR_CHECK_RETURN_ERR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty_cycle));
    BSP_ERROR_CHECK_RETURN_ERR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH));
    return ESP_OK;
}

esp_err_t bsp_display_backlight_off(void)
{
    return bsp_display_brightness_set(0);
}

esp_err_t bsp_display_backlight_on(void)
{
    return bsp_display_brightness_set(100);
}

static esp_err_t bsp_enable_dsi_phy_power(void)
{
#if BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0
    // Turn on the power for MIPI DSI PHY, so it can go from "No Power" state to "Shutdown" state
    static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = BSP_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan), TAG, "Acquire LDO channel for DPHY failed");
    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif // BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0

    return ESP_OK;
}

esp_err_t bsp_display_new(const bsp_display_config_t* config, esp_lcd_panel_handle_t* ret_panel,
                          esp_lcd_panel_io_handle_t* ret_io)
{
    esp_err_t ret = ESP_OK;
    bsp_lcd_handles_t handles;
    ret = bsp_display_new_with_handles(config, &handles);

    *ret_panel = handles.panel;
    *ret_io = handles.io;

    return ret;
}

#define LCD_MIPI_DSI_USE_ILI9881C

#if defined(LCD_MIPI_DSI_USE_ILI9881C) && !defined(LCD_MIPI_DSI_USE_ST7703)
#include "ili9881_init_data.c"
#endif

esp_err_t bsp_display_new_with_handles(const bsp_display_config_t* config, bsp_lcd_handles_t* ret_handles)
{
    esp_err_t ret = ESP_OK;
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_handle_t disp_panel = NULL;

    ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG, "Brightness init failed");
    ESP_RETURN_ON_ERROR(bsp_enable_dsi_phy_power(), TAG, "DSI PHY power failed");

    /* create MIPI DSI bus first, it will initialize the DSI PHY as well */
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = BSP_LCD_MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus), TAG, "New DSI bus init failed");

    ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
    // we use DBI interface to send LCD commands and parameters
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,   // according to the LCD spec
        .lcd_param_bits = 8, // according to the LCD spec
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io), err, TAG, "New panel IO failed");

#if defined(LCD_MIPI_DSI_USE_ILI9881C) && !defined(LCD_MIPI_DSI_USE_ST7703)
    ESP_LOGI(TAG, "Install LCD driver of ili9881c");
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 60, // 720*1280 RGB24 60Hz RGB24 // 80,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,
        .video_timing =
            {
                .h_size = BSP_LCD_H_RES,
                .v_size = BSP_LCD_V_RES,
                .hsync_back_porch = 140,
                .hsync_pulse_width = 40,
                .hsync_front_porch = 40,
                .vsync_back_porch = 20,
                .vsync_pulse_width = 4,
                .vsync_front_porch = 20,
            },
        .flags.use_dma2d = true,
    };

    ili9881c_vendor_config_t vendor_config = {
        .init_cmds = tab5_lcd_ili9881c_specific_init_code_default,
        .init_cmds_size = sizeof(tab5_lcd_ili9881c_specific_init_code_default) /
                          sizeof(tab5_lcd_ili9881c_specific_init_code_default[0]),
        .mipi_config =
            {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
                .lane_num = 2,
            },
    };

    const esp_lcd_panel_dev_config_t lcd_dev_config = {
        .bits_per_pixel = 16,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .reset_gpio_num = -1,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9881c(io, &lcd_dev_config, &disp_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(disp_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(disp_panel));
    //  ESP_ERROR_CHECK(esp_lcd_panel_mirror(disp_panel, false, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(disp_panel, true));

#elif defined(LCD_MIPI_DSI_USE_ST7703) && !defined(LCD_MIPI_DSI_USE_ILI9881C)
    ESP_LOGI(TAG, "Install LCD driver of ST7703");
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 60,                      // LCD_MIPI_DSI_DPI_CLK_MHZ_ST7703,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565, // LCD_COLOR_PIXEL_FORMAT_RGB888,
        .num_fbs = 1,
        .video_timing =
            {
                .h_size = BSP_LCD_H_RES, // lcd_param.width,
                .v_size = BSP_LCD_V_RES, // lcd_param.height,
                .hsync_back_porch = 40,
                .hsync_pulse_width = 10,
                .hsync_front_porch = 40,
                .vsync_back_porch = 16,
                .vsync_pulse_width = 4,
                .vsync_front_porch = 16,
            },
        //.flags.use_dma2d = true, // ??? 开启后需要等�?previous draw 完成
    };

    st7703_vendor_config_t vendor_config = {
        .flags.use_mipi_interface = 1,
        .mipi_config =
            {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
            },
    };
    esp_lcd_panel_dev_config_t lcd_dev_config = {
        .bits_per_pixel = 16, // 24,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .reset_gpio_num = -1,
        .vendor_config = &vendor_config,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7703(io, &lcd_dev_config, &disp_panel), err, TAG,
                      "New LCD panel EK79007 failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(disp_panel), err, TAG, "LCD panel init failed");
#endif

    /* Return all handles */
    ret_handles->io = io;
    ret_handles->mipi_dsi_bus = mipi_dsi_bus;
    ret_handles->panel = disp_panel;
    ret_handles->control = NULL;

    ESP_LOGI(TAG, "Display initialized with resolution %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);

    return ret;

err:
    if (disp_panel)
    {
        esp_lcd_panel_del(disp_panel);
    }
    if (io)
    {
        esp_lcd_panel_io_del(io);
    }
    if (mipi_dsi_bus)
    {
        esp_lcd_del_dsi_bus(mipi_dsi_bus);
    }
    return ret;
}

#define LCD_MIPI_DSI_USE_ST7123
#ifdef LCD_MIPI_DSI_USE_ST7123
#include "esp_lcd_st7123.h"

// ST7123 触摸坐标打印回调函数
static void st7123_touch_callback(esp_lcd_touch_handle_t tp)
{
    uint16_t touch_x[10];
    uint16_t touch_y[10];
    uint16_t touch_strength[10];
    uint8_t touch_cnt = 0;

    // 读取触摸数据
    esp_err_t ret = esp_lcd_touch_read_data(tp);
    if (ret == ESP_OK)
    {
        // 获取触摸坐标
        bool pressed = esp_lcd_touch_get_coordinates(tp, touch_x, touch_y, touch_strength, &touch_cnt, 10);

        if (pressed && touch_cnt > 0)
        {
            for (int i = 0; i < touch_cnt; i++)
            {
                printf("ST7123 Touch %d: x=%d, y=%d, strength=%d\n", i, touch_x[i], touch_y[i], touch_strength[i]);
            }
        }
    }
}

// ST7123 vendor specific initialization commands
static const st7123_lcd_init_cmd_t st7123_vendor_specific_init_default[] = {
    {0x60, (uint8_t[]){0x71, 0x23, 0xa2}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x23, 0xa3}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x23, 0xa4}, 3, 0},
    {0xA4, (uint8_t[]){0x31}, 1, 0},
    {0xD7, (uint8_t[]){0x10, 0x0A, 0x10, 0x2A, 0x80, 0x80}, 6, 0},
    {0x90, (uint8_t[]){0x71, 0x23, 0x5A, 0x20, 0x24, 0x09, 0x09}, 7, 0},
    {0xA3, (uint8_t[]){0x80, 0x01, 0x88, 0x30, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00, 0x1E, 0x5C, 0x1E, 0x80, 0x00, 0x4F, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00, 0x1E, 0x5C, 0x1E, 0x80, 0x00, 0x6F, 0x58, 0x00, 0x00, 0x00, 0xFF},
     40, 0},
    {0xA6, (uint8_t[]){0x03, 0x00, 0x24, 0x55, 0x36, 0x00, 0x39, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0x24, 0x55, 0x38, 0x00, 0x37, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0x24, 0x11, 0x00, 0x00, 0x00, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0xEC, 0x11, 0x00, 0x03, 0x00, 0x03, 0x6E, 0x6E, 0xFF, 0xFF, 0x00, 0x08, 0x80, 0x08, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00},
     55, 0},
    {0xA7, (uint8_t[]){0x19, 0x19, 0x80, 0x64, 0x40, 0x07, 0x16, 0x40, 0x00, 0x44, 0x03, 0x6E, 0x6E, 0x91, 0xFF, 0x08, 0x80, 0x64, 0x40, 0x25, 0x34, 0x40, 0x00, 0x02, 0x01, 0x6E, 0x6E, 0x91, 0xFF, 0x08, 0x80, 0x64, 0x40, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x08, 0x80, 0x64, 0x40, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x6E, 0x6E, 0x84, 0xFF, 0x08, 0x80, 0x44},
     60, 0},
    {0xAC, (uint8_t[]){0x03, 0x19, 0x19, 0x18, 0x18, 0x06, 0x13, 0x13, 0x11, 0x11, 0x08, 0x08, 0x0A, 0x0A, 0x1C, 0x1C, 0x07, 0x07, 0x00, 0x00, 0x02, 0x02, 0x01, 0x19, 0x19, 0x18, 0x18, 0x06, 0x12, 0x12, 0x10, 0x10, 0x09, 0x09, 0x0B, 0x0B, 0x1C, 0x1C, 0x07, 0x07, 0x03, 0x03, 0x01, 0x01},
     44, 0},
    {0xAD, (uint8_t[]){0xF0, 0x00, 0x46, 0x00, 0x03, 0x50, 0x50, 0xFF, 0xFF, 0xF0, 0x40, 0x06, 0x01, 0x07, 0x42, 0x42, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF},
     25, 0},
    {0xAE, (uint8_t[]){0xFE, 0x3F, 0x3F, 0xFE, 0x3F, 0x3F, 0x00}, 7, 0},
    {0xB2,
     (uint8_t[]){0x15, 0x19, 0x05, 0x23, 0x49, 0xAF, 0x03, 0x2E, 0x5C, 0xD2, 0xFF, 0x10, 0x20, 0xFD, 0x20, 0xC0, 0x00},
     17, 0},
    {0xE8, (uint8_t[]){0x20, 0x6F, 0x04, 0x97, 0x97, 0x3E, 0x04, 0xDC, 0xDC, 0x3E, 0x06, 0xFA, 0x26, 0x3E}, 15, 0},
    {0x75, (uint8_t[]){0x03, 0x04}, 2, 0},
    {0xE7, (uint8_t[]){0x3B, 0x00, 0x00, 0x7C, 0xA1, 0x8C, 0x20, 0x1A, 0xF0, 0xB1, 0x50, 0x00, 0x50, 0xB1, 0x50, 0xB1, 0x50, 0xD8, 0x00, 0x55, 0x00, 0xB1, 0x00, 0x45, 0xC9, 0x6A, 0xFF, 0x5A, 0xD8, 0x18, 0x88, 0x15, 0xB1, 0x01, 0x01, 0x77},
     36, 0},
    {0xEA, (uint8_t[]){0x13, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x2C}, 8, 0},
    {0xB0, (uint8_t[]){0x22, 0x43, 0x11, 0x61, 0x25, 0x43, 0x43}, 7, 0},
    {0xb7, (uint8_t[]){0x00, 0x00, 0x73, 0x73}, 0x04, 0},
    {0xBF, (uint8_t[]){0xA6, 0XAA}, 2, 0},
    {0xA9, (uint8_t[]){0x00, 0x00, 0x73, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03}, 10, 0},
    {0xC8, (uint8_t[]){0x00, 0x00, 0x10, 0x1F, 0x36, 0x00, 0x5D, 0x04, 0x9D, 0x05, 0x10, 0xF2, 0x06, 0x60, 0x03, 0x11, 0xAD, 0x00, 0xEF, 0x01, 0x22, 0x2E, 0x0E, 0x74, 0x08, 0x32, 0xDC, 0x09, 0x33, 0x0F, 0xF3, 0x77, 0x0D, 0xB0, 0xDC, 0x03, 0xFF},
     37, 0},
    {0xC9, (uint8_t[]){0x00, 0x00, 0x10, 0x1F, 0x36, 0x00, 0x5D, 0x04, 0x9D, 0x05, 0x10, 0xF2, 0x06, 0x60, 0x03, 0x11, 0xAD, 0x00, 0xEF, 0x01, 0x22, 0x2E, 0x0E, 0x74, 0x08, 0x32, 0xDC, 0x09, 0x33, 0x0F, 0xF3, 0x77, 0x0D, 0xB0, 0xDC, 0x03, 0xFF},
     37, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 100},
    {0x29, (uint8_t[]){0x00}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 100},
};

// 适配st7123 tab5 屏幕
esp_err_t bsp_display_new_with_handles_to_st7123(const bsp_display_config_t* config, bsp_lcd_handles_t* ret_handles)
{
    esp_err_t ret = ESP_OK;
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_handle_t disp_panel = NULL;
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;

    ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG, "Brightness init failed");
    ESP_RETURN_ON_ERROR(bsp_enable_dsi_phy_power(), TAG, "DSI PHY power failed");

    /* create MIPI DSI bus first, it will initialize the DSI PHY as well */
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2, // ST7123 uses 2 data lanes
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 965, // ST7123 lane bitrate
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus), TAG, "New DSI bus init failed");

    ESP_LOGI(TAG, "Install MIPI DSI LCD control panel for ST7123");
    // we use DBI interface to send LCD commands and parameters
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,   // according to the LCD spec
        .lcd_param_bits = 8, // according to the LCD spec
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io), err, TAG, "New panel IO failed");

    ESP_LOGI(TAG, "Install LCD driver of ST7123");
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 70, // ST7123 DPI clock frequency
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,
        .video_timing =
            {
                .h_size = 720,
                .v_size = 1280,
                .hsync_pulse_width = 2,
                .hsync_back_porch = 40,
                .hsync_front_porch = 40,
                .vsync_pulse_width = 2,
                .vsync_back_porch = 8,
                .vsync_front_porch = 220,
            },
        .flags =
            {
                .use_dma2d = true,
            },
    };

    st7123_vendor_config_t vendor_config = {
        .init_cmds = st7123_vendor_specific_init_default,
        .init_cmds_size = sizeof(st7123_vendor_specific_init_default) / sizeof(st7123_vendor_specific_init_default[0]),
        .mipi_config =
            {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
                .lane_num = 2,
            },
    };

    const esp_lcd_panel_dev_config_t lcd_dev_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 24,
        .vendor_config = &vendor_config,
    };

    // 使用实际�?ST7123 驱动函数
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7123(io, &lcd_dev_config, &disp_panel), err, TAG,
                      "New LCD panel ST7123 failed");

    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(disp_panel), err, TAG, "LCD panel reset failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(disp_panel), err, TAG, "LCD panel init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(disp_panel, true), err, TAG, "LCD panel display on failed");

    /* Return all handles */
    ret_handles->io = io;
    ret_handles->mipi_dsi_bus = mipi_dsi_bus;
    ret_handles->panel = disp_panel;
    ret_handles->control = NULL;

    ESP_LOGI(TAG, "ST7123 Display initialized with resolution %dx%d", 720, 1280);

    return ret;

err:
    if (disp_panel)
    {
        esp_lcd_panel_del(disp_panel);
    }
    if (io)
    {
        esp_lcd_panel_io_del(io);
    }
    if (mipi_dsi_bus)
    {
        esp_lcd_del_dsi_bus(mipi_dsi_bus);
    }
    return ret;
}

#endif // LCD_MIPI_DSI_USE_ST7123

esp_err_t bsp_touch_new(const bsp_touch_config_t* config, esp_lcd_touch_handle_t* ret_touch)
{
    /* Initilize I2C */
    BSP_ERROR_CHECK_RETURN_ERR(bsp_i2c_init());

    /* Initialize touch */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = -1, // BSP_LCD_TOUCH_RST, // NC
        .int_gpio_num = BSP_LCD_TOUCH_INT,
        .levels =
            {
                .reset = 0,
                .interrupt = 0,
            },
        .flags =
            {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP; // 更改 GT911 地址
    tp_io_config.scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_handle, &tp_io_config, &tp_io_handle), TAG, "");
    return esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, ret_touch);
}

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
static lv_display_t* bsp_display_lcd_init(const bsp_display_cfg_t* cfg)
{
    assert(cfg != NULL);
    bsp_lcd_handles_t lcd_panels;

    // 动态检测显示屏类型
    bsp_display_type_t display_type = bsp_detect_display_type();

    if (display_type == BSP_DISPLAY_TYPE_ST7123)
    {
        BSP_ERROR_CHECK_RETURN_NULL(bsp_display_new_with_handles_to_st7123(NULL, &lcd_panels));
    }
    else
    {
        BSP_ERROR_CHECK_RETURN_NULL(bsp_display_new_with_handles(NULL, &lcd_panels));
    }

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg =
    {.io_handle = lcd_panels.io,
     .panel_handle = lcd_panels.panel,
     .control_handle = lcd_panels.control,
     .buffer_size = cfg->buffer_size,
     .double_buffer = cfg->double_buffer,
     .hres = BSP_LCD_H_RES,
     .vres = BSP_LCD_V_RES,
     .monochrome = false,
     /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
     .rotation =
         {
             .swap_xy = false,
             .mirror_x = false,
             .mirror_y = false,
         },
#if LVGL_VERSION_MAJOR >= 9
#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
     .color_format = LV_COLOR_FORMAT_RGB888,
#else
     .color_format = LV_COLOR_FORMAT_RGB565,
#endif
#endif
     .flags = {
         .buff_dma = cfg->flags.buff_dma,
         .buff_spiram = cfg->flags.buff_spiram,
#if LVGL_VERSION_MAJOR >= 9
         .swap_bytes = (BSP_LCD_BIGENDIAN ? true : false),
#endif
#if CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR
         .sw_rotate = false, /* Avoid tearing is not supported for SW rotation */
#else
         .sw_rotate = cfg->flags.sw_rotate, /* Only SW rotation is supported for 90° and 270° */
#endif
#if CONFIG_BSP_DISPLAY_LVGL_FULL_REFRESH
         .full_refresh = true,
#elif CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE
         .direct_mode = true,
#endif
     } };

    const lvgl_port_display_dsi_cfg_t dpi_cfg = {.flags = {
#if CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR
                                                     .avoid_tearing = true,
#else
                                                     .avoid_tearing = false,
#endif
                                                 }};

    return lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
}

esp_lcd_touch_handle_t _lcd_touch_handle;

esp_lcd_touch_handle_t bsp_display_get_touch_handle(void)
{
    return _lcd_touch_handle;
}

bool trail_mate_tab5_touch_interrupt_active(void)
{
    return (BSP_LCD_TOUCH_INT != GPIO_NUM_NC) && (gpio_get_level(BSP_LCD_TOUCH_INT) == 0);
}

static void trail_mate_touch_activity_event_cb(lv_event_t* e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED)
    {
        trail_mate_idf_note_user_activity();
    }
}

static void trail_mate_attach_touch_activity_hook(lv_indev_t* indev)
{
    if (indev == NULL)
    {
        return;
    }
    lv_indev_add_event_cb(indev, trail_mate_touch_activity_event_cb, LV_EVENT_PRESSED, NULL);
    lv_indev_add_event_cb(indev, trail_mate_touch_activity_event_cb, LV_EVENT_PRESSING, NULL);
    lv_indev_add_event_cb(indev, trail_mate_touch_activity_event_cb, LV_EVENT_RELEASED, NULL);
}

static void lvgl_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    if (_lcd_touch_handle == NULL)
    {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    // Lock SYS I2C bus before accessing touch screen
    // Use try-lock (0ms timeout) to avoid blocking other sensors
    // Touch screen is polled frequently (every LVGL refresh cycle ~33ms), so we must not block
    // If bus is busy, skip this read cycle - missing one touch read is acceptable
    // This ensures other sensors (BMI270, RX8130, INA226, Module GNSS, etc.) can access I2C bus
    if (!bsp_i2c_lock(0))
    { // Try-lock: if bus is busy, skip this read cycle
        // Bus is busy, assume no touch and return immediately
        // This prevents touch screen from monopolizing the I2C bus
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    esp_lcd_touch_point_data_t touch_data[1] = {0};
    uint8_t touch_cnt = 0;

    esp_err_t ret = esp_lcd_touch_read_data(_lcd_touch_handle);
    if (ret != ESP_OK)
    {
        // If I2C read fails, assume no touch and return
        bsp_i2c_unlock();
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    ret = esp_lcd_touch_get_data(_lcd_touch_handle, touch_data, &touch_cnt, 1);

    // Unlock I2C bus after operation
    bsp_i2c_unlock();

    if (ret != ESP_OK)
    {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    bool touchpad_pressed = (touch_cnt > 0);

    // Validate touch coordinates to prevent invalid values from being passed to LVGL
    // Invalid coordinates (e.g., -64256, 16383) can cause rendering issues
    if (!touchpad_pressed)
    {
        data->state = LV_INDEV_STATE_REL;
    }
    else if (touch_cnt > 0 && touch_data[0].x < 2000 && touch_data[0].y < 2000)
    {
        // Check if coordinates are within valid screen bounds
        // Screen size is typically 1280x720, but allow some margin for edge cases
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touch_data[0].x;
        data->point.y = touch_data[0].y;
        trail_mate_idf_note_user_activity();
    }
    else
    {
        // Invalid coordinates, treat as no touch
        data->state = LV_INDEV_STATE_REL;
    }
}

static lv_indev_t* bsp_display_indev_init(lv_display_t* disp)
{
    esp_lcd_touch_handle_t tp;
    BSP_ERROR_CHECK_RETURN_NULL(bsp_touch_new(NULL, &tp));
    esp_lcd_touch_exit_sleep(tp); // !!!
    assert(tp);
    _lcd_touch_handle = tp;

    disp_indev = lv_indev_create();
    lv_indev_set_type(disp_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(disp_indev, lvgl_read_cb);
    lv_indev_set_display(disp_indev, disp);
    trail_mate_attach_touch_activity_hook(disp_indev);

    return disp_indev;
}

static lv_indev_t* bsp_display_indev_init_to_st7123(lv_display_t* disp)
{
    esp_err_t ret = ESP_OK;
    esp_lcd_touch_handle_t tp = NULL;

    /* Initialize I2C for ST7123 touch */
    BSP_ERROR_CHECK_RETURN_NULL(bsp_i2c_init());

    // bsp_io_expander_pi4ioe_restart_touch();

    /* Initialize ST7123 touch panel */
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = 0x55, // ST7123 touch I2C address
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .flags =
            {
                .disable_control_phase = 1,
            },
    };
    tp_io_config.scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ;

    ret = esp_lcd_new_panel_io_i2c_v2(bsp_i2c_get_handle(), &tp_io_config, &tp_io_handle);
    // ret = esp_lcd_new_panel_io_i2c(bsp_i2c_get_handle(), &tp_io_config, &tp_io_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create ST7123 touch I2C IO: %s", esp_err_to_name(ret));
        return NULL;
    }

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = -1, // BSP_LCD_TOUCH_RST, // NC
        .int_gpio_num = BSP_LCD_TOUCH_INT,
        .levels =
            {
                .reset = 0,
                .interrupt = 0,
            },
        .flags =
            {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
    };

    ret = esp_lcd_touch_new_i2c_st7123(tp_io_handle, &tp_cfg, &tp);
    if (ret != ESP_OK || tp == NULL)
    {
        ESP_LOGE(TAG, "Failed to create ST7123 touch panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(tp_io_handle);
        return NULL;
    }

    // Exit sleep mode for touch panel
    // esp_lcd_touch_exit_sleep(tp);

    // Store the touch handle globally
    _lcd_touch_handle = tp;

    /* Add touch input to LVGL */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = tp,
    };

    lv_indev_t* indev = lvgl_port_add_touch(&touch_cfg);
    if (indev == NULL)
    {
        ESP_LOGE(TAG, "Failed to add ST7123 touch to LVGL");
        esp_lcd_panel_io_del(tp_io_handle);
        return NULL;
    }

    trail_mate_attach_touch_activity_hook(indev);
    ESP_LOGI(TAG, "ST7123 touch panel initialized successfully");
    return indev;
}

lv_display_t* bsp_display_start(void)
{
    bsp_display_cfg_t cfg = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
                             .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
                             .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
                             .flags = {
#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
                                 .buff_dma = false,
#else
                                 .buff_dma = true,
#endif
                                 .buff_spiram = false,
                                 .sw_rotate = true,
                             }};
    return bsp_display_start_with_config(&cfg);
}

lv_display_t* bsp_display_start_with_config(const bsp_display_cfg_t* cfg)
{
    lv_display_t* disp;

    assert(cfg != NULL);
    BSP_ERROR_CHECK_RETURN_NULL(lvgl_port_init(&cfg->lvgl_port_cfg));

    BSP_ERROR_CHECK_RETURN_NULL(bsp_display_brightness_init());

    BSP_NULL_CHECK(disp = bsp_display_lcd_init(cfg), NULL);

    // 动态检测显示屏类型并初始化对应的触摸屏
    bsp_display_type_t display_type = bsp_detect_display_type();

    if (display_type == BSP_DISPLAY_TYPE_ST7123)
    {
        BSP_NULL_CHECK(disp_indev = bsp_display_indev_init_to_st7123(disp), NULL);
    }
    else
    {
        BSP_NULL_CHECK(disp_indev = bsp_display_indev_init(disp), NULL);
    }

    return disp;
}

lv_indev_t* bsp_display_get_input_dev(void)
{
    return disp_indev;
}

void bsp_display_rotate(lv_display_t* disp, lv_disp_rotation_t rotation)
{
    lv_disp_set_rotation(disp, rotation);
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}
#endif // (BSP_CONFIG_NO_GRAPHIC_LIB == 0)

//==================================================================================
// usb
//==================================================================================
static void usb_lib_task(void* arg)
{
    while (1)
    {
        // Start handling system events
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
        {
            ESP_ERROR_CHECK(usb_host_device_free_all());
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)
        {
            ESP_LOGI(TAG, "USB: All devices freed");
            // Continue handling USB events to allow device reconnection
            // The only way this task can be stopped is by calling bsp_usb_host_stop()
        }
    }
}

esp_err_t bsp_usb_host_start(bsp_usb_host_power_mode_t mode, bool limit_500mA)
{
    // Install USB Host driver. Should only be called once in entire application
    ESP_LOGI(TAG, "Installing USB Host");
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    BSP_ERROR_CHECK_RETURN_ERR(usb_host_install(&host_config));

    // Create a task that will handle USB library events
    if (xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 10, &usb_host_task) != pdTRUE)
    {
        ESP_LOGE(TAG, "Creating USB host lib task failed");
        abort();
    }

    return ESP_OK;
}

esp_err_t bsp_usb_host_stop(void)
{
    usb_host_uninstall();
    if (usb_host_task)
    {
        vTaskSuspend(usb_host_task);
        vTaskDelete(usb_host_task);
    }
    return ESP_OK;
}
