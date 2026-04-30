#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino.h>

#include "board/GpsBoard.h"
#include "board/MotionBoard.h"
#include "gps/domain/gnss_satellite.h"
#include "gps/domain/gps_state.h"
#include "gps/domain/motion_config.h"
#include "gps/motion_policy.h"
#include "gps/usecase/gps_jitter_filter.h"
#include "gps/usecase/gps_runtime_config.h"
#include "gps/usecase/gps_runtime_state.h"
#include "platform/esp/arduino_common/gps/infra/hal_gps_adapter.h"
#include "platform/esp/arduino_common/gps/infra/hal_motion_adapter.h"

namespace gps
{

class GpsService
{
  public:
    static GpsService& getInstance();

    void begin(GpsBoard& gps_board, MotionBoard& motion_board, uint32_t disable_hw_init,
               uint32_t gps_interval_ms, const MotionConfig& motion_config);
    GpsState getData();
    bool getGnssSnapshot(GnssSatInfo* out, size_t max, size_t* out_count, GnssStatus* status);
    uint32_t getCollectionInterval() const;
    void setEnabled(bool enabled);
    void setCollectionInterval(uint32_t interval_ms);
    void setPowerStrategy(uint8_t strategy);
    void setTeamModeActive(bool active);
    void setGnssConfig(uint8_t mode, uint8_t sat_mask);
    void setExternalNmeaConfig(uint8_t output_hz, uint8_t sentence_mask);
    MotionConfig getMotionConfig() const { return motion_config_; }
    void setMotionConfig(const MotionConfig& config);
    void setMotionIdleTimeout(uint32_t timeout_ms);
    void setMotionSensorId(uint8_t sensor_id);
    TaskHandle_t getTaskHandle() const { return gps_task_handle_; }
    TaskHandle_t getMotionTaskHandle() const { return motion_task_handle_; }
    bool isEnabled() const { return !gps_disabled_ && user_enabled_; }
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
    void applyInternalNmeaConfig();

    GpsBoard* gps_board_ = nullptr;
    MotionBoard* motion_board_ = nullptr;
    GpsState gps_state_{};
    GnssSatInfo gnss_sats_[kMaxGnssSats]{};
    size_t gnss_sat_count_ = 0;
    GnssStatus gnss_status_{};
    SemaphoreHandle_t gps_data_mutex_ = nullptr;
    TaskHandle_t gps_task_handle_ = nullptr;
    TaskHandle_t motion_task_handle_ = nullptr;

    uint32_t gps_last_update_time_ = 0;
    GpsRuntimeState runtime_state_{};
    GpsRuntimeConfig runtime_config_{};
    bool gps_time_synced_ = false;
    bool gps_powered_ = false;
    bool gps_disabled_ = false;
    bool user_enabled_ = true;
    bool gps_ready_ = false;

    MotionConfig motion_config_{};
    MotionPolicy motion_policy_{};
    HalGpsAdapter gps_adapter_{};
    HalMotionAdapter motion_adapter_{};
    GpsJitterFilter jitter_filter_{};
};

} // namespace gps
