#include "boards/tab5/heading_runtime.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)

#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "bmi2.h"
#include "bmi270.h"
#include "boards/tab5/tab5_board.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace boards::tab5::heading_runtime
{
namespace
{

constexpr const char* kTag = "tab5-heading";
constexpr uint8_t kBmi270Address = 0x69;
constexpr uint8_t kBmm150Address = 0x10;
constexpr uint8_t kBmm150RegChipId = 0x40;
constexpr uint8_t kBmm150RegData = 0x42;
constexpr uint8_t kBmm150RegStatus = 0x48;
constexpr uint8_t kBmm150RegPower = 0x4B;
constexpr uint8_t kBmm150RegOpmode = 0x4C;
constexpr uint8_t kBmm150RegAxes = 0x4E;
constexpr uint8_t kBmm150RegRepxy = 0x51;
constexpr uint8_t kBmm150RegRepz = 0x52;
constexpr uint32_t kI2cTimeoutMs = 60;
constexpr uint32_t kSampleIntervalMs = 200;
constexpr uint32_t kLogIntervalMs = 3000;
constexpr float kPi = 3.14159265358979323846f;

struct RuntimeState
{
    std::mutex mutex;
    HeadingState data{};
    bool started = false;
    bool sensor_ready = false;
    bool measurement_active = false;
    uint32_t retry_backoff_ms = 1000;
    uint32_t next_retry_ms = 0;
    TaskHandle_t task = nullptr;
    i2c_master_dev_handle_t bmi_handle = nullptr;
    bmi2_dev bmi{};
};

RuntimeState g_runtime{};

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

float normalize_degrees(float angle)
{
    while (angle < 0.0f)
    {
        angle += 360.0f;
    }
    while (angle >= 360.0f)
    {
        angle -= 360.0f;
    }
    return angle;
}

float blend_heading(float previous, float next, float alpha)
{
    float delta = normalize_degrees(next - previous);
    if (delta > 180.0f)
    {
        delta -= 360.0f;
    }
    return normalize_degrees(previous + delta * alpha);
}

int8_t bmi_read(uint8_t reg_addr, uint8_t* reg_data, uint32_t len, void* intf_ptr)
{
    (void)intf_ptr;
    if (g_runtime.bmi_handle == nullptr || reg_data == nullptr || len == 0)
    {
        return BMI2_E_COM_FAIL;
    }

    auto& board = ::boards::tab5::Tab5Board::instance();
    if (!board.lockSystemI2c(kI2cTimeoutMs))
    {
        return BMI2_E_COM_FAIL;
    }

    const uint8_t write_buffer[1] = {reg_addr};
    const esp_err_t err =
        i2c_master_transmit_receive(g_runtime.bmi_handle, write_buffer, sizeof(write_buffer), reg_data, len, kI2cTimeoutMs);
    board.unlockSystemI2c();
    return err == ESP_OK ? BMI2_OK : BMI2_E_COM_FAIL;
}

int8_t bmi_write(uint8_t reg_addr, const uint8_t* reg_data, uint32_t len, void* intf_ptr)
{
    (void)intf_ptr;
    if (g_runtime.bmi_handle == nullptr || reg_data == nullptr || len == 0 || len > 31)
    {
        return BMI2_E_COM_FAIL;
    }

    auto& board = ::boards::tab5::Tab5Board::instance();
    if (!board.lockSystemI2c(kI2cTimeoutMs))
    {
        return BMI2_E_COM_FAIL;
    }

    uint8_t write_buffer[32];
    write_buffer[0] = reg_addr;
    std::memcpy(&write_buffer[1], reg_data, len);
    const esp_err_t err = i2c_master_transmit(g_runtime.bmi_handle, write_buffer, len + 1, kI2cTimeoutMs);
    board.unlockSystemI2c();
    return err == ESP_OK ? BMI2_OK : BMI2_E_COM_FAIL;
}

void bmi_delay_us(uint32_t period, void* intf_ptr)
{
    (void)intf_ptr;
    const uint32_t delay_ms = (period + 999U) / 1000U;
    vTaskDelay(pdMS_TO_TICKS(delay_ms == 0 ? 1 : delay_ms));
}

bool add_bmi_device()
{
    if (g_runtime.bmi_handle != nullptr)
    {
        return true;
    }

    auto& board = ::boards::tab5::Tab5Board::instance();
    const ::boards::tab5::Tab5Board::SystemI2cDeviceConfig config{
        "heading_bmi270",
        kBmi270Address,
        400000,
    };
    g_runtime.bmi_handle = board.getManagedSystemI2cDevice(config, kI2cTimeoutMs);
    if (g_runtime.bmi_handle == nullptr)
    {
        ESP_LOGE(kTag, "Failed to provision managed BMI270 device");
        return false;
    }

    g_runtime.bmi = {};
    g_runtime.bmi.intf = BMI2_I2C_INTF;
    g_runtime.bmi.read = bmi_read;
    g_runtime.bmi.write = bmi_write;
    g_runtime.bmi.delay_us = bmi_delay_us;
    g_runtime.bmi.read_write_len = 30;
    g_runtime.bmi.config_file_ptr = nullptr;
    return true;
}

bool init_bmi270()
{
    if (!add_bmi_device())
    {
        return false;
    }

    ESP_LOGI(kTag, "Initializing Module GNSS BMI270 on SYS I2C");
    const int8_t rslt = bmi270_init(&g_runtime.bmi);
    if (rslt != BMI2_OK)
    {
        ESP_LOGE(kTag, "bmi270_init failed: %d", static_cast<int>(rslt));
        return false;
    }
    return true;
}

bool configure_bmm150()
{
    bmi2_sens_config aux_cfg{};
    aux_cfg.type = BMI2_AUX;
    aux_cfg.cfg.aux.aux_en = BMI2_ENABLE;
    aux_cfg.cfg.aux.i2c_device_addr = kBmm150Address;
    aux_cfg.cfg.aux.manual_en = BMI2_ENABLE;
    aux_cfg.cfg.aux.fcu_write_en = BMI2_ENABLE;
    aux_cfg.cfg.aux.man_rd_burst = BMI2_AUX_READ_LEN_3;
    aux_cfg.cfg.aux.aux_rd_burst = BMI2_AUX_READ_LEN_3;
    aux_cfg.cfg.aux.odr = BMI2_AUX_ODR_100HZ;
    aux_cfg.cfg.aux.read_addr = kBmm150RegData;
    aux_cfg.cfg.aux.offset = 0;

    if (bmi2_set_sensor_config(&aux_cfg, 1, &g_runtime.bmi) != BMI2_OK)
    {
        ESP_LOGE(kTag, "Failed to configure BMI270 AUX interface");
        return false;
    }

    uint8_t reg = 0x01;
    if (bmi2_write_aux_man_mode(kBmm150RegPower, &reg, 1, &g_runtime.bmi) != BMI2_OK)
    {
        ESP_LOGE(kTag, "Failed to wake BMM150 from suspend");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    reg = 0x82;
    if (bmi2_write_aux_man_mode(kBmm150RegPower, &reg, 1, &g_runtime.bmi) != BMI2_OK)
    {
        ESP_LOGE(kTag, "Failed to soft reset BMM150");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(4));

    reg = 0x01;
    if (bmi2_write_aux_man_mode(kBmm150RegPower, &reg, 1, &g_runtime.bmi) != BMI2_OK)
    {
        ESP_LOGE(kTag, "Failed to confirm BMM150 sleep mode");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t chip_id = 0;
    if (bmi2_read_aux_man_mode(kBmm150RegChipId, &chip_id, 1, &g_runtime.bmi) != BMI2_OK)
    {
        ESP_LOGE(kTag, "Failed to read BMM150 chip id");
        return false;
    }
    if (chip_id != 0x32)
    {
        ESP_LOGE(kTag, "Unexpected BMM150 chip id: 0x%02X", chip_id);
        return false;
    }

    reg = 0x01;
    (void)bmi2_write_aux_man_mode(kBmm150RegRepxy, &reg, 1, &g_runtime.bmi);
    (void)bmi2_write_aux_man_mode(kBmm150RegRepz, &reg, 1, &g_runtime.bmi);
    reg = 0x07;
    (void)bmi2_write_aux_man_mode(kBmm150RegAxes, &reg, 1, &g_runtime.bmi);
    vTaskDelay(pdMS_TO_TICKS(5));

    ESP_LOGI(kTag, "BMM150 ready on AUX bus");
    return true;
}

bool start_measurement()
{
    if (bmi2_set_adv_power_save(BMI2_DISABLE, &g_runtime.bmi) != BMI2_OK)
    {
        ESP_LOGW(kTag, "Failed to disable BMI270 advanced power save");
    }

    uint8_t opmode = 0x00;
    if (bmi2_write_aux_man_mode(kBmm150RegOpmode, &opmode, 1, &g_runtime.bmi) != BMI2_OK)
    {
        ESP_LOGE(kTag, "Failed to enter BMM150 normal mode");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    g_runtime.measurement_active = true;
    ESP_LOGI(kTag, "BMM150 measurement started");
    return true;
}

bool ensure_sensor_ready()
{
    if (g_runtime.sensor_ready)
    {
        return true;
    }

    const uint32_t now = now_ms();
    if (g_runtime.next_retry_ms != 0 && now < g_runtime.next_retry_ms)
    {
        return false;
    }

    (void)::boards::tab5::Tab5Board::instance().acquireExt5vRail("heading_runtime");
    vTaskDelay(pdMS_TO_TICKS(300));

    if (!init_bmi270())
    {
        g_runtime.next_retry_ms = now_ms() + g_runtime.retry_backoff_ms;
        g_runtime.retry_backoff_ms = std::min<uint32_t>(g_runtime.retry_backoff_ms * 2U, 15000U);
        return false;
    }
    if (!configure_bmm150())
    {
        g_runtime.next_retry_ms = now_ms() + g_runtime.retry_backoff_ms;
        g_runtime.retry_backoff_ms = std::min<uint32_t>(g_runtime.retry_backoff_ms * 2U, 15000U);
        return false;
    }
    if (!start_measurement())
    {
        g_runtime.next_retry_ms = now_ms() + g_runtime.retry_backoff_ms;
        g_runtime.retry_backoff_ms = std::min<uint32_t>(g_runtime.retry_backoff_ms * 2U, 15000U);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        g_runtime.sensor_ready = true;
        g_runtime.data.sensor_ready = true;
    }
    g_runtime.retry_backoff_ms = 1000;
    g_runtime.next_retry_ms = 0;
    return true;
}

void publish_invalid()
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.data.available = false;
    g_runtime.data.sensor_ready = g_runtime.sensor_ready;
}

void publish_heading(int16_t raw_x, int16_t raw_y, int16_t raw_z, float heading_deg)
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    HeadingState next = g_runtime.data;
    const bool had_previous = g_runtime.data.available;
    next.available = true;
    next.sensor_ready = true;
    next.raw_x = raw_x;
    next.raw_y = raw_y;
    next.raw_z = raw_z;
    next.heading_deg = had_previous ? blend_heading(next.heading_deg, heading_deg, 0.35f) : heading_deg;
    next.last_update_ms = now_ms();
    g_runtime.data = next;
}

bool sample_once()
{
    if (!g_runtime.measurement_active)
    {
        return false;
    }

    uint8_t status_reg = 0;
    const int8_t status_rslt = bmi2_read_aux_man_mode(kBmm150RegStatus, &status_reg, 1, &g_runtime.bmi);
    if (status_rslt != BMI2_OK || (status_reg & 0x01U) == 0)
    {
        return false;
    }

    uint8_t mag_data[8] = {};
    const int8_t rslt = bmi2_read_aux_man_mode(kBmm150RegData, mag_data, sizeof(mag_data), &g_runtime.bmi);
    if (rslt != BMI2_OK)
    {
        return false;
    }

    const int16_t raw_x = static_cast<int16_t>((mag_data[1] << 8) | (mag_data[0] & 0xF8)) >> 3;
    const int16_t raw_y = static_cast<int16_t>((mag_data[3] << 8) | (mag_data[2] & 0xF8)) >> 3;
    const int16_t raw_z = static_cast<int16_t>((mag_data[5] << 8) | (mag_data[4] & 0xFE)) >> 1;

    float heading_deg = std::atan2(static_cast<float>(raw_y), static_cast<float>(raw_x)) * 180.0f / kPi;
    if (heading_deg < 0.0f)
    {
        heading_deg += 360.0f;
    }
    publish_heading(raw_x, raw_y, raw_z, heading_deg);
    return true;
}

void task_main(void* arg)
{
    (void)arg;
    uint32_t last_log_ms = 0;
    uint32_t last_error_ms = 0;

    ESP_LOGI(kTag, "Heading task started");

    for (;;)
    {
        if (!ensure_sensor_ready())
        {
            publish_invalid();
            if (now_ms() - last_error_ms >= kLogIntervalMs)
            {
                ESP_LOGW(kTag,
                         "Heading sensor not ready yet backoff=%lu next_retry_in=%ld ms",
                         static_cast<unsigned long>(g_runtime.retry_backoff_ms),
                         g_runtime.next_retry_ms > now_ms() ? static_cast<long>(g_runtime.next_retry_ms - now_ms()) : 0L);
                last_error_ms = now_ms();
            }
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        if (sample_once())
        {
            const HeadingState current = get_data();
            if (now_ms() - last_log_ms >= kLogIntervalMs)
            {
                ESP_LOGI(kTag,
                         "heading=%.1f raw=(%d,%d,%d) age=%lu ms",
                         static_cast<double>(current.heading_deg),
                         static_cast<int>(current.raw_x),
                         static_cast<int>(current.raw_y),
                         static_cast<int>(current.raw_z),
                         static_cast<unsigned long>(now_ms() - current.last_update_ms));
                last_log_ms = now_ms();
            }
        }
        else if (now_ms() - last_error_ms >= kLogIntervalMs)
        {
            const HeadingState current = get_data();
            ESP_LOGW(kTag,
                     "No fresh BMM150 sample yet sensor_ready=%d active=%d age=%lu ms",
                     g_runtime.sensor_ready ? 1 : 0,
                     g_runtime.measurement_active ? 1 : 0,
                     current.last_update_ms != 0 ? static_cast<unsigned long>(now_ms() - current.last_update_ms) : 0UL);
            last_error_ms = now_ms();
        }

        vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
    }
}

} // namespace

void ensure_started()
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (g_runtime.started)
    {
        return;
    }

    BaseType_t ok = xTaskCreate(task_main, "tab5_heading", 6144, nullptr, 4, &g_runtime.task);
    if (ok != pdPASS)
    {
        ESP_LOGE(kTag, "Failed to create heading task");
        return;
    }
    g_runtime.started = true;
}

HeadingState get_data()
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    return g_runtime.data;
}

} // namespace boards::tab5::heading_runtime

#endif
