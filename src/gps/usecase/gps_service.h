#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino.h>

#include "../domain/gps_state.h"
#include "../domain/motion_config.h"
#include "../infra/hal_gps_adapter.h"
#include "../infra/hal_motion_adapter.h"
#include "../motion_policy.h"
#include "gps/usecase/gps_jitter_filter.h"
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
    void setPowerStrategy(uint8_t strategy);
    void setGnssConfig(uint8_t mode, uint8_t sat_mask);
    void setNmeaConfig(uint8_t output_hz, uint8_t sentence_mask);
    MotionConfig getMotionConfig() const { return motion_config_; }
    void setMotionConfig(const MotionConfig& config);
    void setMotionIdleTimeout(uint32_t timeout_ms);
    void setMotionSensorId(uint8_t sensor_id);
    TaskHandle_t getTaskHandle() const { return gps_task_handle_; }
    bool isEnabled() const { return !gps_disabled_ && gps_board_ != nullptr; }
    bool isPowered() const { return gps_powered_; }
    uint32_t getLastMotionMs() const;

  private:
    GpsService() = default;
    GpsService(const GpsService&) = delete;
    GpsService& operator=(const GpsService&) = delete;

    static void gpsTask(void* pvParameters);
    static void motionTask(void* pvParameters);

    void setGPSPowerState(bool enable);
    void updateMotionState(uint32_t now_ms);
    void applyGnssConfig();
    void applyNmeaConfig();

    GpsBoard* gps_board_ = nullptr;
    MotionBoard* motion_board_ = nullptr;
    GpsState gps_state_{};
    SemaphoreHandle_t gps_data_mutex_ = nullptr;
    TaskHandle_t gps_task_handle_ = nullptr;
    TaskHandle_t motion_task_handle_ = nullptr;

    uint32_t gps_last_update_time_ = 0;
    uint32_t gps_collection_interval_ms_ = 60000;
    uint8_t power_strategy_ = 0;
    uint8_t gnss_mode_ = 0;
    uint8_t gnss_sat_mask_ = 0x1 | 0x8 | 0x4;
    bool gnss_config_pending_ = false;
    uint8_t nmea_output_hz_ = 0;
    uint8_t nmea_sentence_mask_ = 0;
    bool nmea_config_pending_ = false;
    bool gps_time_synced_ = false;
    bool gps_powered_ = false;
    bool gps_disabled_ = false;
    bool motion_control_enabled_ = false;

    MotionConfig motion_config_{};
    MotionPolicy motion_policy_{};
    HalGpsAdapter gps_adapter_{};
    HalMotionAdapter motion_adapter_{};
    GpsJitterFilter jitter_filter_{};
};

} // namespace gps
