#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "bmi270.h"
#include "driver/i2c_master.h"

    esp_err_t accel_gyro_bmi270_init(i2c_master_bus_handle_t bus_handle);
    void accel_gyro_bmi270_enable_sensor(void);
    void accel_gyro_bmi270_wrist_wear_irq(void);
    void accel_gyro_bmi270_wrist_wear_irq_without_int(void);
    void accel_gyro_bmi270_clear_irq_int(void);
    void accel_gyro_bmi270_get_data(struct bmi2_sens_data* data);
    bool accel_gyro_bmi270_check_irq(void);
    void accel_gyro_bmi270_clear_irq_int(void);
    bool accel_gyro_bmi270_motion_irq(void);

#ifdef __cplusplus
}
#endif
