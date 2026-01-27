#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino.h>

#include "../domain/gps_state.h"
#include "../domain/motion_config.h"
#include "../infra/hal_gps_adapter.h"
#include "../infra/hal_motion_adapter.h"
#include "../motion_policy.h"
#include "board/GpsBoard.h"
#include "board/MotionBoard.h"

namespace gps
{

class GpsService
{
  public:
    static GpsService& getInstance();

    void begin(GpsBoard& gps_board, MotionBoard& motion_board, uint32_t disable_hw_init,
               uint32_t gps_interval_ms, const MotionConfig& motion_config);
    GpsState getData();
    uint32_t getCollectionInterval() const;
    void setCollectionInterval(uint32_t interval_ms);
    MotionConfig getMotionConfig() const { return motion_config_; }
    void setMotionConfig(const MotionConfig& config);
    void setMotionIdleTimeout(uint32_t timeout_ms);
    void setMotionSensorId(uint8_t sensor_id);
    TaskHandle_t getTaskHandle() const { return gps_task_handle_; }

  private:
    GpsService() = default;
    GpsService(const GpsService&) = delete;
    GpsService& operator=(const GpsService&) = delete;

    static void gpsTask(void* pvParameters);
    static void motionTask(void* pvParameters);

    void setGPSPowerState(bool enable);
    void updateMotionState(uint32_t now_ms);

    GpsBoard* gps_board_ = nullptr;
    MotionBoard* motion_board_ = nullptr;
    GpsState gps_state_{};
    SemaphoreHandle_t gps_data_mutex_ = nullptr;
    TaskHandle_t gps_task_handle_ = nullptr;
    TaskHandle_t motion_task_handle_ = nullptr;

    uint32_t gps_last_update_time_ = 0;
    uint32_t gps_collection_interval_ms_ = 60000;
    bool gps_time_synced_ = false;
    bool gps_powered_ = false;
    bool gps_disabled_ = false;
    bool motion_control_enabled_ = false;

    MotionConfig motion_config_{};
    MotionPolicy motion_policy_{};
    HalGpsAdapter gps_adapter_{};
    HalMotionAdapter motion_adapter_{};
};

} // namespace gps
