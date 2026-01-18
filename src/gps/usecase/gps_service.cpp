#include "gps/usecase/gps_service.h"

#include "board/TLoRaPagerBoard.h"
#include "board/TLoRaPagerTypes.h"

namespace {
constexpr uint32_t kGpsSampleIntervalMs = 60000;
}

namespace gps {

GpsService &GpsService::getInstance()
{
    static GpsService instance;
    return instance;
}

void GpsService::begin(TLoRaPagerBoard &board, uint32_t disable_hw_init,
                       uint32_t gps_interval_ms, const MotionConfig &motion_config)
{
    board_ = &board;
    gps_adapter_.begin(board);
    motion_adapter_.begin(board);

    gps_disabled_ = (disable_hw_init & NO_HW_GPS) != 0;
    if (gps_disabled_) {
        return;
    }

    gps_data_mutex_ = xSemaphoreCreateMutex();
    if (gps_data_mutex_ == NULL) {
        log_e("Failed to create GPS data mutex");
    }

    gps_collection_interval_ms_ = gps_interval_ms;
    motion_config_ = motion_config;

    if (gps_collection_interval_ms_ < kGpsSampleIntervalMs) {
        gps_collection_interval_ms_ = kGpsSampleIntervalMs;
    }
    if (motion_config_.idle_timeout_ms < 60000) {
        motion_config_.idle_timeout_ms = 60000;
    }

    BaseType_t task_result = xTaskCreate(
        gpsTask,
        "gps_collect",
        4 * 1024,
        this,
        5,
        &gps_task_handle_
    );
    if (task_result != pdPASS) {
        log_e("Failed to create GPS data collection task");
    } else {
        log_d("GPS data collection task created successfully (interval: %lu ms)", gps_collection_interval_ms_);
    }

    motion_control_enabled_ = motion_policy_.begin(motion_adapter_, motion_config_);

    if (motion_control_enabled_ && gps_task_handle_ != nullptr) {
        vTaskSuspend(gps_task_handle_);
    }

    if (motion_control_enabled_ && motion_task_handle_ == nullptr) {
        task_result = xTaskCreate(
            motionTask,
            "motion_mgr",
            3 * 1024,
            this,
            6,
            &motion_task_handle_
        );
        if (task_result != pdPASS) {
            log_e("Failed to create motion manager task");
        } else {
            log_d("Motion manager task created successfully");
        }
    }

    if (!motion_control_enabled_) {
        setGPSPowerState(true);
    }
}

GpsState GpsService::getData()
{
    GpsState data {};
    if (gps_data_mutex_ != NULL) {
        if (xSemaphoreTake(gps_data_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
            data = gps_state_;
            if (data.valid && gps_last_update_time_ > 0) {
                data.age = millis() - gps_last_update_time_;
            } else {
                data.age = UINT32_MAX;
            }
            xSemaphoreGive(gps_data_mutex_);
        }
    }
    return data;
}

uint32_t GpsService::getCollectionInterval() const
{
    uint32_t interval = gps_collection_interval_ms_;

    if (interval < kGpsSampleIntervalMs) {
        interval = kGpsSampleIntervalMs;
    }

    return interval;
}

void GpsService::setCollectionInterval(uint32_t interval_ms)
{
    if (interval_ms < kGpsSampleIntervalMs) interval_ms = kGpsSampleIntervalMs;
    if (interval_ms > 600000) interval_ms = 600000;

    if (gps_data_mutex_ != NULL) {
        if (xSemaphoreTake(gps_data_mutex_, portMAX_DELAY) == pdTRUE) {
            gps_collection_interval_ms_ = interval_ms;
            xSemaphoreGive(gps_data_mutex_);
        }
    }
}

void GpsService::setMotionConfig(const MotionConfig &config)
{
    if (board_ == nullptr || gps_disabled_) {
        return;
    }

    motion_config_ = config;
    if (motion_config_.idle_timeout_ms < 60000) {
        motion_config_.idle_timeout_ms = 60000;
    }

    bool was_enabled = motion_control_enabled_;
    motion_control_enabled_ = motion_policy_.begin(motion_adapter_, motion_config_);

    if (motion_control_enabled_) {
        if (gps_task_handle_ != nullptr) {
            vTaskSuspend(gps_task_handle_);
        }
        if (motion_task_handle_ == nullptr) {
            BaseType_t task_result = xTaskCreate(
                motionTask,
                "motion_mgr",
                3 * 1024,
                this,
                6,
                &motion_task_handle_
            );
            if (task_result != pdPASS) {
                log_e("Failed to create motion manager task");
            } else {
                log_d("Motion manager task created successfully");
            }
        }
    } else if (was_enabled) {
        if (gps_task_handle_ != nullptr) {
            vTaskResume(gps_task_handle_);
        }
        setGPSPowerState(true);
    }
}

void GpsService::setMotionIdleTimeout(uint32_t timeout_ms)
{
    MotionConfig cfg = motion_config_;
    cfg.idle_timeout_ms = timeout_ms;
    setMotionConfig(cfg);
}

void GpsService::setMotionSensorId(uint8_t sensor_id)
{
    MotionConfig cfg = motion_config_;
    cfg.sensor_id = sensor_id;
    setMotionConfig(cfg);
}

void GpsService::gpsTask(void *pvParameters)
{
    GpsService *service = static_cast<GpsService *>(pvParameters);
    if (service == nullptr) {
        vTaskDelete(NULL);
        return;
    }

    TickType_t last_wake_time = xTaskGetTickCount();
    uint32_t loop_count = 0;
    uint32_t task_start_ms = millis();
    uint32_t last_log_ms = 0;

    Serial.printf("[GPS Task] ===== TASK STARTED =====\n");
    Serial.printf("[GPS Task] Started at %lu ms, GPS ready: %d\n", task_start_ms, service->gps_adapter_.isReady());
    Serial.printf("[GPS Task] Collection interval: %lu ms\n", service->getCollectionInterval());

    while (true) {
        loop_count++;
        uint32_t now_ms = millis();
        bool gps_ready = service->gps_adapter_.isReady();

        bool should_log = (loop_count <= 10) ||
                          (loop_count % 10 == 0) ||
                          ((now_ms - last_log_ms) >= 5000);

        if (should_log) {
            Serial.printf("[GPS Task] Loop %lu: GPS ready=%d, valid=%d, mutex=%p\n",
                          loop_count, gps_ready, service->gps_state_.valid, service->gps_data_mutex_);
            last_log_ms = now_ms;
        }

        if (!service->gps_powered_) {
            if (should_log) {
                Serial.printf("[GPS Task] GPS power OFF (motion_control=%d), skipping (loop %lu)\n",
                              service->motion_control_enabled_ ? 1 : 0,
                              loop_count);
            }
        } else if (gps_ready) {
            static uint32_t last_total_chars = 0;
            uint32_t total_chars = service->gps_adapter_.loop();
            uint32_t chars_this_loop = (total_chars > last_total_chars) ? (total_chars - last_total_chars) : 0;
            last_total_chars = total_chars;

            if (should_log && chars_this_loop > 0) {
                Serial.printf("[GPS Task] GPS loop processed %lu characters this cycle (total: %lu)\n",
                              chars_this_loop, total_chars);
            }

            if (service->gps_data_mutex_ != NULL && xSemaphoreTake(service->gps_data_mutex_, portMAX_DELAY) == pdTRUE) {
                bool was_valid = service->gps_state_.valid;
                bool has_fix = service->gps_adapter_.hasFix();
                uint8_t sat_count = service->gps_adapter_.satellites();

                if (!service->gps_time_synced_) {
                    uint32_t gps_interval = service->getCollectionInterval();
                    if (service->gps_adapter_.syncTime(gps_interval)) {
                        service->gps_time_synced_ = true;
                        Serial.printf("[GPS Task] *** TIME SYNCED TO RTC (automatic) *** (loop %lu, sat=%d)\n",
                                      loop_count, sat_count);
                    }
                }

                if (has_fix) {
                    service->gps_state_.lat = service->gps_adapter_.latitude();
                    service->gps_state_.lng = service->gps_adapter_.longitude();
                    service->gps_state_.satellites = sat_count;
                    service->gps_state_.valid = true;
                    service->gps_last_update_time_ = millis();
                    service->gps_state_.age = 0;

                    if (!was_valid || should_log) {
                        Serial.printf("[GPS Task] *** FIX ACQUIRED *** lat=%.6f, lng=%.6f, sat=%d (loop %lu)\n",
                                      service->gps_state_.lat, service->gps_state_.lng,
                                      service->gps_state_.satellites, loop_count);
                    }
                } else {
                    service->gps_state_.valid = false;
                    if (was_valid) {
                        Serial.printf("[GPS Task] *** FIX LOST *** (loop %lu)\n", loop_count);
                    }
                    if (should_log) {
                        Serial.printf("[GPS Task] GPS ready but no fix yet (loop %lu, sat=%d, chars_this_cycle=%lu)\n",
                                      loop_count, sat_count, chars_this_loop);
                    }
                }
                xSemaphoreGive(service->gps_data_mutex_);
            } else {
                Serial.printf("[GPS Task] ERROR: Failed to take mutex (loop %lu)\n", loop_count);
            }
        } else {
            static uint32_t last_retry_ms = 0;
            const uint32_t RETRY_INTERVAL_MS = 300000;

            if (should_log) {
                Serial.printf("[GPS Task] GPS not ready (loop %lu)\n", loop_count);
            }

            if (last_retry_ms == 0 || (now_ms - last_retry_ms) >= RETRY_INTERVAL_MS) {
                Serial.printf("[GPS Task] Attempting to reinitialize GPS (last retry: %lu ms ago, loop %lu)\n",
                              last_retry_ms > 0 ? (now_ms - last_retry_ms) : 0, loop_count);

                bool retry_result = service->gps_adapter_.init();
                last_retry_ms = now_ms;

                if (retry_result) {
                    Serial.printf("[GPS Task] *** GPS REINITIALIZATION SUCCESSFUL *** (loop %lu)\n", loop_count);
                } else {
                    Serial.printf("[GPS Task] GPS reinitialization failed, will retry in %lu ms (loop %lu)\n",
                                  RETRY_INTERVAL_MS, loop_count);
                }
            }
        }

        uint32_t interval_ms = service->getCollectionInterval();
        TickType_t frequency = pdMS_TO_TICKS(interval_ms);

        if (should_log) {
            Serial.printf("[GPS Task] Waiting %lu ms until next cycle...\n", interval_ms);
        }

        vTaskDelayUntil(&last_wake_time, frequency);
    }
}

void GpsService::motionTask(void *pvParameters)
{
    GpsService *service = static_cast<GpsService *>(pvParameters);
    if (service == nullptr) {
        vTaskDelete(NULL);
        return;
    }

    TickType_t last_wake_time = xTaskGetTickCount();

    while (true) {
        uint32_t now_ms = millis();

        if (service->motion_adapter_.isReady() && service->motion_policy_.isEnabled()) {
            if (service->motion_policy_.shouldUpdateSensor(now_ms)) {
                service->motion_adapter_.update();
                service->motion_policy_.markSensorUpdated(now_ms);
            }
            service->updateMotionState(now_ms);
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(service->motion_policy_.taskIntervalMs()));
    }
}

void GpsService::setGPSPowerState(bool enable)
{
    if (enable) {
        if (gps_powered_) {
            return;
        }
        gps_adapter_.powerOn();
        gps_powered_ = true;
        gps_adapter_.init();
        setCollectionInterval(kGpsSampleIntervalMs);
        if (gps_task_handle_ != nullptr) {
            vTaskResume(gps_task_handle_);
        }
    } else {
        if (!gps_powered_) {
            return;
        }
        if (gps_task_handle_ != nullptr) {
            vTaskSuspend(gps_task_handle_);
        }
        gps_adapter_.powerOff();
        gps_powered_ = false;
    }
}

void GpsService::updateMotionState(uint32_t now_ms)
{
    if (!motion_control_enabled_ || !motion_policy_.isEnabled()) {
        return;
    }

    bool should_enable_gps = motion_policy_.shouldEnableGps(now_ms);

    if (should_enable_gps && !gps_powered_) {
        setGPSPowerState(true);
    } else if (!should_enable_gps && gps_powered_) {
        setGPSPowerState(false);
    }
}

}  // namespace gps
