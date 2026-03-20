/***************************************************




***************************************************/
#include "accel_gyro_bmi270.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Conditionally include BSP header for I2C bus lock support
#ifdef __has_include
#if __has_include("bsp/m5stack_tab5.h")
#include "bsp/m5stack_tab5.h"
#define BSP_I2C_LOCK_AVAILABLE 1
#else
#define BSP_I2C_LOCK_AVAILABLE 0
#endif
#else
// Fallback: try to include and define based on compilation result
#ifdef CONFIG_BSP_ENABLE_I2C_LOCK
#include "bsp/m5stack_tab5.h"
#define BSP_I2C_LOCK_AVAILABLE 1
#else
#define BSP_I2C_LOCK_AVAILABLE 0
#endif
#endif

static const char* TAG = "bmi270";

#define I2C_MASTER_TIMEOUT_MS 100
#define I2C_DEV_ADDR_BMI270 0x68

void bmi2_error_codes_print_result(int8_t rslt);
static int8_t bmi270_i2c_read(uint8_t reg_addr, uint8_t* reg_data, uint32_t len, void* intf_ptr);
static int8_t bmi270_i2c_write(uint8_t reg_addr, const uint8_t* reg_data, uint32_t len, void* intf_ptr);
static void bmi270_delay_us(uint32_t period, void* intf_ptr);

// BMI270
#define ACCEL UINT8_C(0x00)
#define GYRO UINT8_C(0x01)
#define AUX UINT8_C(0x02)
static i2c_master_dev_handle_t i2c_dev_handle_bmi270 = NULL;
static struct bmi2_dev bmi270;
static bool bmi270_initialized = false;
static bool bmi270_device_valid = false; // Track if device handle is still valid after I2C errors

esp_err_t accel_gyro_bmi270_init(i2c_master_bus_handle_t bus_handle)
{
    // CRITICAL: Check if bus handle is valid
    if (bus_handle == NULL)
    {
        ESP_LOGE(TAG, "I2C bus handle is NULL - bus not initialized");
        return ESP_ERR_INVALID_ARG;
    }

    // CRITICAL: Prevent multiple initialization
    // Multiple calls to this function can cause device handle conflicts and crashes
    if (bmi270_initialized && i2c_dev_handle_bmi270 != NULL)
    {
        ESP_LOGW(TAG, "BMI270 already initialized, skipping");
        return ESP_OK;
    }

    int8_t rslt;

    // CRITICAL: Remove existing device if it exists (shouldn't happen, but be safe)
    if (i2c_dev_handle_bmi270 != NULL)
    {
        ESP_LOGW(TAG, "BMI270 device handle exists, removing before reinitialization");
        i2c_master_bus_rm_device(i2c_dev_handle_bmi270);
        i2c_dev_handle_bmi270 = NULL;
        bmi270_device_valid = false; // Reset device validity flag
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_DEV_ADDR_BMI270,
        .scl_speed_hz = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &i2c_dev_handle_bmi270);
    if (ret != ESP_OK || i2c_dev_handle_bmi270 == NULL)
    {
        ESP_LOGE(TAG, "Failed to add BMI270 device: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    /* To enable the i2c interface settings for bmi270. */
    bmi270.intf = BMI2_I2C_INTF; // 使用 I2C 接口驱动
    bmi270.read = bmi270_i2c_read;
    bmi270.write = bmi270_i2c_write;
    bmi270.delay_us = bmi270_delay_us;
    bmi270.read_write_len = 30;
    bmi270.config_file_ptr = NULL;

    // rslt = bmi2_interface_init(&bmi270, BMI2_I2C_INTF);
    // bmi2_error_codes_print_result(rslt);

    /* Initialize bmi270. */
    rslt = bmi270_init(&bmi270);
    bmi2_error_codes_print_result(rslt);

    if (rslt == BMI2_OK)
    {
        bmi270_initialized = true;
        bmi270_device_valid = true; // Mark device as valid after successful initialization
    }
    else
    {
        // Clean up on failure
        i2c_master_bus_rm_device(i2c_dev_handle_bmi270);
        i2c_dev_handle_bmi270 = NULL;
        bmi270_device_valid = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

#define ACCEL UINT8_C(0x00)
#define GYRO UINT8_C(0x01)
void accel_gyro_bmi270_enable_sensor(void)
{
    int8_t rslt;

    /* List the sensors which are required to enable */
    uint8_t sens_list[2] = {BMI2_ACCEL, BMI2_GYRO};

    /* Structure to define the type of the sensor and its configurations */
    struct bmi2_sens_config config[2];
    config[ACCEL].type = BMI2_ACCEL;
    config[GYRO].type = BMI2_GYRO;

    /* Get default configurations for the type of feature selected. */
    rslt = bmi2_get_sensor_config(config, 2, &bmi270);
    bmi2_error_codes_print_result(rslt);

    if (rslt == BMI2_OK)
    {
        ESP_LOGI(TAG, "Set sensors odr and range");
        config[ACCEL].cfg.acc.odr = BMI2_ACC_ODR_200HZ;   // 加速度数据输出速率 200Hz
        config[ACCEL].cfg.acc.range = BMI2_ACC_RANGE_4G;  // 加速度量程 +- 4g --> 2^16/(8*9.8) --> (1/835.92)LSB m/s^2
        config[GYRO].cfg.gyr.odr = BMI2_GYR_ODR_200HZ;    // 角速度速度数据输出速率 200Hz
        config[GYRO].cfg.gyr.range = BMI2_GYR_RANGE_1000; // 角速度量程 +- 1000dps --> 2^16/2000 --> (1/32.768)LSB °/S
        rslt = bmi270_set_sensor_config(config, 2, &bmi270);
        bmi2_error_codes_print_result(rslt);
        if (rslt == BMI2_OK)
        {
            ESP_LOGI(TAG, "Enable the selected sensors");
            rslt = bmi270_sensor_enable(sens_list, 2, &bmi270);
            bmi2_error_codes_print_result(rslt);
        }
    }
}

void accel_gyro_bmi270_wrist_wear_irq(void)
{
    if (i2c_dev_handle_bmi270 == NULL)
    {
        ESP_LOGE(TAG, "i2c_dev_handle_bmi270 is NULL");
        return;
    }

    struct bmi2_remap remapped_axis = {0};
    remapped_axis.x = BMI2_X;
    remapped_axis.y = BMI2_Y;
    remapped_axis.z = BMI2_Z;

    if (bmi2_set_remap_axes(&remapped_axis, &bmi270) == BMI2_OK)
    {
        struct bmi2_sens_int_config sens_int = {.type = BMI2_WRIST_WEAR_WAKE_UP, .hw_int_pin = BMI2_INT1};
        if (bmi270_map_feat_int(&sens_int, 1, &bmi270) != BMI2_OK)
        {
            ESP_LOGE(TAG, "Wrist gesture mapping failed\n");
        }
        struct bmi2_int_pin_config int_pin_cfg;
        int_pin_cfg.pin_type = BMI2_INT1;
        int_pin_cfg.int_latch = BMI2_INT_LATCH;
        int_pin_cfg.pin_cfg[0].lvl = BMI2_INT_ACTIVE_HIGH; // Active high
        int_pin_cfg.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
        int_pin_cfg.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
        int_pin_cfg.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;

        if (bmi2_set_int_pin_config(&int_pin_cfg, &bmi270) != BMI2_OK)
        {
            ESP_LOGE(TAG, "Wrist gesture pin config failed");
        }
    }
    else
    {
        ESP_LOGE(TAG, "Wrist gesture remap failed");
    }
}

void accel_gyro_bmi270_wrist_wear_irq_without_int(void)
{
    if (i2c_dev_handle_bmi270 == NULL)
    {
        ESP_LOGE(TAG, "i2c_dev_handle_bmi270 is NULL");
        return;
    }

    struct bmi2_remap remapped_axis = {0};
    remapped_axis.x = BMI2_X;
    remapped_axis.y = BMI2_Y;
    remapped_axis.z = BMI2_Z;

    if (bmi2_set_remap_axes(&remapped_axis, &bmi270) == BMI2_OK)
    {
        struct bmi2_sens_int_config sens_int = {.type = BMI2_WRIST_WEAR_WAKE_UP, .hw_int_pin = BMI2_INT_BOTH};
        if (bmi270_map_feat_int(&sens_int, 1, &bmi270) != BMI2_OK)
        {
            ESP_LOGE(TAG, "Wrist gesture mapping failed\n");
        }
        struct bmi2_int_pin_config int_pin_cfg;
        int_pin_cfg.pin_type = BMI2_INT_BOTH;
        int_pin_cfg.int_latch = BMI2_INT_LATCH;
        int_pin_cfg.pin_cfg[0].lvl = BMI2_INT_ACTIVE_LOW; // Active low
        int_pin_cfg.pin_cfg[0].od = BMI2_INT_OPEN_DRAIN;
        int_pin_cfg.pin_cfg[0].output_en = BMI2_INT_OUTPUT_DISABLE;
        int_pin_cfg.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;

        if (bmi2_set_int_pin_config(&int_pin_cfg, &bmi270) != BMI2_OK)
        {
            ESP_LOGE(TAG, "Wrist gesture pin int disable config failed");
        }
    }
    else
    {
        ESP_LOGE(TAG, "Wrist gesture remap failed");
    }
}

bool accel_gyro_bmi270_motion_irq(void)
{
    /* Status of api are returned to this variable. */
    int8_t rslt;

    /* Accel sensor and any-motion feature are listed in array. */
    uint8_t sens_list[2] = {BMI2_ACCEL, BMI2_ANY_MOTION};
    /* Enable the selected sensors. */
    rslt = bmi270_sensor_enable(sens_list, 2, &bmi270);
    // bmi2_error_codes_print_result(rslt);
    if (rslt != BMI2_OK)
    {
        return false;
    }

    /* Structure to define the type of sensor and its configurations. */
    struct bmi2_sens_config config;

    /* Interrupt pin configuration */
    struct bmi2_int_pin_config pin_config = {0};

    /* Configure the type of feature. */
    config.type = BMI2_ANY_MOTION;

    /* Get default configurations for the type of feature selected. */
    rslt = bmi270_get_sensor_config(&config, 1, &bmi270);
    // bmi2_error_codes_print_result(rslt);
    if (rslt != BMI2_OK) return false;

    rslt = bmi2_get_int_pin_config(&pin_config, &bmi270);
    // bmi2_error_codes_print_result(rslt);
    if (rslt != BMI2_OK) return false;

    // /* NOTE: The user can change the following configuration parameters according to their requirement. */
    // /* 1LSB equals 20ms. Default is 100ms, settin68g to 80ms. */
    // // config.cfg.any_motion.duration = 0x04;
    // config.cfg.any_motion.duration = 0x08;

    // /* 1LSB equals to 0.48mg. Default is 83mg, setting to 50mg. */
    // config.cfg.any_motion.threshold = 0x68;

    config.cfg.any_motion.duration = 0x32;
    config.cfg.any_motion.threshold = 0xFF;

    /* Set new configurations. */
    rslt = bmi270_set_sensor_config(&config, 1, &bmi270);
    // bmi2_error_codes_print_result(rslt);
    if (rslt != BMI2_OK) return false;

    /* Interrupt pin configuration */
    pin_config.pin_type = BMI2_INT1;
    pin_config.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;

    // pin_config.pin_cfg[0].lvl       = BMI2_INT_ACTIVE_LOW;
    pin_config.pin_cfg[0].lvl = BMI2_INT_ACTIVE_HIGH;

    pin_config.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
    // pin_config.pin_cfg[0].od        = BMI2_INT_OPEN_DRAIN;

    pin_config.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;

    // pin_config.int_latch            = BMI2_INT_NON_LATCH;
    pin_config.int_latch = BMI2_INT_LATCH;

    rslt = bmi2_set_int_pin_config(&pin_config, &bmi270);

    // bmi2_error_codes_print_result(rslt);
    if (rslt != BMI2_OK) return false;

    /* Map the feature interrupt for no-motion. */
    /* Select features and their pins to be mapped to. */
    struct bmi2_sens_int_config sens_int = {.type = BMI2_ANY_MOTION, .hw_int_pin = BMI2_INT1};
    rslt = bmi270_map_feat_int(&sens_int, 1, &bmi270);
    // bmi2_error_codes_print_result(rslt);
    if (rslt != BMI2_OK) return false;

    return true;
}

void accel_gyro_bmi270_get_data(struct bmi2_sens_data* data)
{
    if (i2c_dev_handle_bmi270 == NULL || !bmi270_device_valid)
    {
        // Device not available - zero out data to prevent using invalid values
        if (data != NULL)
        {
            memset(data, 0, sizeof(struct bmi2_sens_data));
        }
        return;
    }
    bmi2_get_sensor_data(data, &bmi270);
}

bool accel_gyro_bmi270_check_irq(void)
{
    if (i2c_dev_handle_bmi270 == NULL)
    {
        ESP_LOGE(TAG, "i2c_dev_handle_bmi270 is NULL");
        return false;
    }

    uint16_t int_status = 0;
    /* To get the interrupt status of the wrist wear wakeup */
    int8_t rslt = bmi2_get_int_status(&int_status, &bmi270);

    /* To check the interrupt status of the wrist gesture */
    if ((rslt == BMI2_OK) && (int_status & BMI270_WRIST_WAKE_UP_STATUS_MASK))
    {
        ESP_LOGW(TAG, "Wrist detected");
        return true;
    }

    if ((rslt == BMI2_OK) && (int_status & BMI270_ANY_MOT_STATUS_MASK))
    {
        ESP_LOGW(TAG, "Any motion detected");
    }

    return false;
}

void accel_gyro_bmi270_clear_irq_int(void)
{
    if (i2c_dev_handle_bmi270 == NULL)
    {
        ESP_LOGE(TAG, "i2c_dev_handle_bmi270 is NULL");
        return;
    }

    struct bmi2_int_pin_config int_pin_cfg;
    int_pin_cfg.pin_type = BMI2_INT_BOTH;
    int_pin_cfg.int_latch = BMI2_INT_LATCH;
    int_pin_cfg.pin_cfg[0].lvl = BMI2_INT_ACTIVE_LOW; // Active low
    int_pin_cfg.pin_cfg[0].od = BMI2_INT_OPEN_DRAIN;
    int_pin_cfg.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
    int_pin_cfg.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;

    if (bmi2_set_int_pin_config(&int_pin_cfg, &bmi270) != BMI2_OK)
    {
        ESP_LOGE(TAG, "Wrist gesture pin int disable config failed");
    }
}

static int8_t bmi270_i2c_read(uint8_t reg_addr, uint8_t* reg_data, uint32_t len, void* intf_ptr)
{
    if ((reg_data == NULL) || (len == 0) || (len > 32))
    {
        return -1;
    }

    // CRITICAL: Check if device handle is valid before accessing I2C
    // This prevents crashes when I2C device is not initialized or has been destroyed
    if (i2c_dev_handle_bmi270 == NULL || !bmi270_device_valid)
    {
        // Don't log error here - device may be temporarily unavailable
        return -1;
    }

#if BSP_I2C_LOCK_AVAILABLE
    // Lock SYS I2C bus before accessing BMI270
    // Use a reasonable timeout (100ms) - if lock fails, return error without blocking
    if (!bsp_i2c_lock(I2C_MASTER_TIMEOUT_MS))
    {
        // Don't log error here - lock failure is expected when I2C bus is busy
        // Logging would spam the console during normal operation
        return -1;
    }
#endif

    uint8_t write_buffer[1] = {reg_addr};
    esp_err_t ret =
        i2c_master_transmit_receive(i2c_dev_handle_bmi270, write_buffer, 1, reg_data, len, I2C_MASTER_TIMEOUT_MS);

#if BSP_I2C_LOCK_AVAILABLE
    bsp_i2c_unlock();
#endif

    if (ret != ESP_OK)
    {
        // CRITICAL: Check for serious I2C errors that indicate device handle corruption
        // ESP_ERR_INVALID_STATE means the I2C driver internal state is corrupted
        // This can happen after bus errors, and continuing to use the handle will cause crashes
        if (ret == ESP_ERR_INVALID_STATE)
        {
            // Mark device as invalid to prevent further operations that could crash
            bmi270_device_valid = false;
            ESP_LOGE(TAG, "I2C device handle corrupted (INVALID_STATE) - marking device invalid to prevent crash");
            return -1;
        }

        // For other errors (NACK, timeout, etc.), these are usually transient
        // Only log errors for critical operations, not for normal sensor reads
        // I2C read failures can occur when bus is busy, which is normal
        // Use ESP_LOGW (WARN level) so it's visible at INFO log level, but less noisy than ERROR
        // Throttle logging to avoid spam - only log once per second
        static uint32_t last_log_time = 0;
        uint32_t now = esp_timer_get_time() / 1000; // Convert to milliseconds
        if (now - last_log_time > 1000)
        { // Log at most once per second
            ESP_LOGW(TAG, "I2C read failed: %s (reg=0x%02x) - may be normal if bus is busy", esp_err_to_name(ret), reg_addr);
            last_log_time = now;
        }
        return -1;
    }

    return 0;
}

static int8_t bmi270_i2c_write(uint8_t reg_addr, const uint8_t* reg_data, uint32_t len, void* intf_ptr)
{
    if ((reg_data == NULL) || (len == 0) || (len > 32))
    {
        return -1;
    }

    // CRITICAL: Check if device handle is valid before accessing I2C
    // This prevents crashes when I2C device is not initialized or has been destroyed
    if (i2c_dev_handle_bmi270 == NULL || !bmi270_device_valid)
    {
        // Don't log error here - device may be temporarily unavailable
        return -1;
    }

    esp_err_t ret;

    // Prepare write buffer: first byte is the register address, followed by the data bytes
    uint8_t* write_buffer = malloc(len + 1);
    if (write_buffer == NULL)
    {
        ESP_LOGE("BMI270", "Memory allocation failed for write buffer");
        return -1;
    }

    // First byte is register address
    write_buffer[0] = reg_addr;
    // Copy the data to be written into the buffer
    memcpy(&write_buffer[1], reg_data, len);

#if BSP_I2C_LOCK_AVAILABLE
    // Lock SYS I2C bus before accessing BMI270
    // Use a reasonable timeout (100ms) - if lock fails, return error without blocking
    if (!bsp_i2c_lock(I2C_MASTER_TIMEOUT_MS))
    {
        // Don't log error here - lock failure is expected when I2C bus is busy
        // Logging would spam the console during normal operation
        free(write_buffer);
        return -1;
    }
#endif

    // Perform I2C write operation
    ret = i2c_master_transmit(i2c_dev_handle_bmi270, write_buffer, len + 1, I2C_MASTER_TIMEOUT_MS);

#if BSP_I2C_LOCK_AVAILABLE
    bsp_i2c_unlock();
#endif

    if (ret != ESP_OK)
    {
        // CRITICAL: Check for serious I2C errors that indicate device handle corruption
        // ESP_ERR_INVALID_STATE means the I2C driver internal state is corrupted
        // This can happen after bus errors, and continuing to use the handle will cause crashes
        if (ret == ESP_ERR_INVALID_STATE)
        {
            // Mark device as invalid to prevent further operations that could crash
            bmi270_device_valid = false;
            ESP_LOGE(TAG, "I2C device handle corrupted (INVALID_STATE) - marking device invalid to prevent crash");
            free(write_buffer);
            return -1;
        }

        // Only log errors for critical operations, not for normal sensor writes
        // I2C write failures can occur when bus is busy, which is normal
        // Use ESP_LOGW (WARN level) so it's visible at INFO log level, but less noisy than ERROR
        // Throttle logging to avoid spam - only log once per second
        static uint32_t last_log_time = 0;
        uint32_t now = esp_timer_get_time() / 1000; // Convert to milliseconds
        if (now - last_log_time > 1000)
        { // Log at most once per second
            ESP_LOGW(TAG, "I2C write failed: %s (reg=0x%02x) - may be normal if bus is busy", esp_err_to_name(ret), reg_addr);
            last_log_time = now;
        }
        free(write_buffer);
        return -1;
    }

    free(write_buffer); // Clean up allocated memory

    return 0;
}

static void bmi270_delay_us(uint32_t period, void* intf_ptr)
{
    uint64_t m = (uint64_t)esp_timer_get_time();
    if (period)
    {
        uint64_t e = (m + period);
        if (m > e)
        { // overflow
            while ((uint64_t)esp_timer_get_time() > e)
            {
                asm volatile("nop");
            }
        }
        while ((uint64_t)esp_timer_get_time() < e)
        {
            asm volatile("nop");
        }
    }
}

/*!
 *  @brief Prints the execution status of the APIs.
 */
void bmi2_error_codes_print_result(int8_t rslt)
{
    switch (rslt)
    {
    case BMI2_OK:

        /* Do nothing */
        break;

    case BMI2_W_FIFO_EMPTY:
        printf("Warning [%d] : FIFO empty\r\n", rslt);
        break;
    case BMI2_W_PARTIAL_READ:
        printf("Warning [%d] : FIFO partial read\r\n", rslt);
        break;
    case BMI2_E_NULL_PTR:
        printf(
            "Error [%d] : Null pointer error. It occurs when the user tries to assign value (not address) to a "
            "pointer,"
            " which has been initialized to NULL.\r\n",
            rslt);
        break;

    case BMI2_E_COM_FAIL:
        printf(
            "Error [%d] : Communication failure error. It occurs due to read/write operation failure and also due "
            "to power failure during communication\r\n",
            rslt);
        break;

    case BMI2_E_DEV_NOT_FOUND:
        printf("Error [%d] : Device not found error. It occurs when the device chip id is incorrectly read\r\n",
               rslt);
        break;

    case BMI2_E_INVALID_SENSOR:
        printf(
            "Error [%d] : Invalid sensor error. It occurs when there is a mismatch in the requested feature with "
            "the "
            "available one\r\n",
            rslt);
        break;

    case BMI2_E_SELF_TEST_FAIL:
        printf(
            "Error [%d] : Self-test failed error. It occurs when the validation of accel self-test data is "
            "not satisfied\r\n",
            rslt);
        break;

    case BMI2_E_INVALID_INT_PIN:
        printf(
            "Error [%d] : Invalid interrupt pin error. It occurs when the user tries to configure interrupt pins "
            "apart from INT1 and INT2\r\n",
            rslt);
        break;

    case BMI2_E_OUT_OF_RANGE:
        printf(
            "Error [%d] : Out of range error. It occurs when the data exceeds from filtered or unfiltered data "
            "from "
            "fifo and also when the range exceeds the maximum range for accel and gyro while performing FOC\r\n",
            rslt);
        break;

    case BMI2_E_ACC_INVALID_CFG:
        printf(
            "Error [%d] : Invalid Accel configuration error. It occurs when there is an error in accel "
            "configuration"
            " register which could be one among range, BW or filter performance in reg address 0x40\r\n",
            rslt);
        break;

    case BMI2_E_GYRO_INVALID_CFG:
        printf(
            "Error [%d] : Invalid Gyro configuration error. It occurs when there is a error in gyro configuration"
            "register which could be one among range, BW or filter performance in reg address 0x42\r\n",
            rslt);
        break;

    case BMI2_E_ACC_GYR_INVALID_CFG:
        printf(
            "Error [%d] : Invalid Accel-Gyro configuration error. It occurs when there is a error in accel and gyro"
            " configuration registers which could be one among range, BW or filter performance in reg address 0x40 "
            "and 0x42\r\n",
            rslt);
        break;

    case BMI2_E_CONFIG_LOAD:
        printf(
            "Error [%d] : Configuration load error. It occurs when failure observed while loading the "
            "configuration "
            "into the sensor\r\n",
            rslt);
        break;

    case BMI2_E_INVALID_PAGE:
        printf(
            "Error [%d] : Invalid page error. It occurs due to failure in writing the correct feature "
            "configuration "
            "from selected page\r\n",
            rslt);
        break;

    case BMI2_E_SET_APS_FAIL:
        printf(
            "Error [%d] : APS failure error. It occurs due to failure in write of advance power mode configuration "
            "register\r\n",
            rslt);
        break;

    case BMI2_E_AUX_INVALID_CFG:
        printf(
            "Error [%d] : Invalid AUX configuration error. It occurs when the auxiliary interface settings are not "
            "enabled properly\r\n",
            rslt);
        break;

    case BMI2_E_AUX_BUSY:
        printf(
            "Error [%d] : AUX busy error. It occurs when the auxiliary interface buses are engaged while "
            "configuring"
            " the AUX\r\n",
            rslt);
        break;

    case BMI2_E_REMAP_ERROR:
        printf(
            "Error [%d] : Remap error. It occurs due to failure in assigning the remap axes data for all the axes "
            "after change in axis position\r\n",
            rslt);
        break;

    case BMI2_E_GYR_USER_GAIN_UPD_FAIL:
        printf(
            "Error [%d] : Gyro user gain update fail error. It occurs when the reading of user gain update status "
            "fails\r\n",
            rslt);
        break;

    case BMI2_E_SELF_TEST_NOT_DONE:
        printf(
            "Error [%d] : Self-test not done error. It occurs when the self-test process is ongoing or not "
            "completed\r\n",
            rslt);
        break;

    case BMI2_E_INVALID_INPUT:
        printf("Error [%d] : Invalid input error. It occurs when the sensor input validity fails\r\n", rslt);
        break;

    case BMI2_E_INVALID_STATUS:
        printf("Error [%d] : Invalid status error. It occurs when the feature/sensor validity fails\r\n", rslt);
        break;

    case BMI2_E_CRT_ERROR:
        printf("Error [%d] : CRT error. It occurs when the CRT test has failed\r\n", rslt);
        break;

    case BMI2_E_ST_ALREADY_RUNNING:
        printf(
            "Error [%d] : Self-test already running error. It occurs when the self-test is already running and "
            "another has been initiated\r\n",
            rslt);
        break;

    case BMI2_E_CRT_READY_FOR_DL_FAIL_ABORT:
        printf(
            "Error [%d] : CRT ready for download fail abort error. It occurs when download in CRT fails due to "
            "wrong "
            "address location\r\n",
            rslt);
        break;

    case BMI2_E_DL_ERROR:
        printf(
            "Error [%d] : Download error. It occurs when write length exceeds that of the maximum burst length\r\n",
            rslt);
        break;

    case BMI2_E_PRECON_ERROR:
        printf(
            "Error [%d] : Pre-conditional error. It occurs when precondition to start the feature was not "
            "completed\r\n",
            rslt);
        break;

    case BMI2_E_ABORT_ERROR:
        printf("Error [%d] : Abort error. It occurs when the device was shaken during CRT test\r\n", rslt);
        break;

    case BMI2_E_WRITE_CYCLE_ONGOING:
        printf(
            "Error [%d] : Write cycle ongoing error. It occurs when the write cycle is already running and another "
            "has been initiated\r\n",
            rslt);
        break;

    case BMI2_E_ST_NOT_RUNING:
        printf(
            "Error [%d] : Self-test is not running error. It occurs when self-test running is disabled while it's "
            "running\r\n",
            rslt);
        break;

    case BMI2_E_DATA_RDY_INT_FAILED:
        printf(
            "Error [%d] : Data ready interrupt error. It occurs when the sample count exceeds the FOC sample limit "
            "and data ready status is not updated\r\n",
            rslt);
        break;

    case BMI2_E_INVALID_FOC_POSITION:
        printf(
            "Error [%d] : Invalid FOC position error. It occurs when average FOC data is obtained for the wrong"
            " axes\r\n",
            rslt);
        break;

    default:
        printf("Error [%d] : Unknown error code\r\n", rslt);
        break;
    }
}
