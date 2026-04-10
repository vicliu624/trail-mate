#pragma once

#include "app/app_config.h"
#include "gps/domain/gnss_satellite.h"
#include "gps/domain/gps_state.h"

#include <cstddef>
#include <cstdint>

namespace boards::gat562_mesh_evb_pro
{

class GpsRuntime
{
  public:
    GpsRuntime();
    ~GpsRuntime();

    bool start(const app::AppConfig& config);
    bool begin(const app::AppConfig& config);
    void applyConfig(const app::AppConfig& config);
    void tick();

    bool isReady() const;
    ::gps::GpsState data() const;
    bool enabled() const;
    bool powered() const;
    uint32_t lastMotionMs() const;
    bool gnssSnapshot(::gps::GnssSatInfo* out,
                      std::size_t max,
                      std::size_t* out_count,
                      ::gps::GnssStatus* status) const;

    void setCollectionInterval(uint32_t interval_ms);
    void setPowerStrategy(uint8_t strategy);
    void setConfig(uint8_t mode, uint8_t sat_mask);
    void setNmeaConfig(uint8_t output_hz, uint8_t sentence_mask);
    void setMotionIdleTimeout(uint32_t timeout_ms);
    void setMotionSensorId(uint8_t sensor_id);
    void suspend();
    void resume();
    void setCurrentEpochSeconds(uint32_t epoch_s);
    uint32_t currentEpochSeconds() const;
    bool isRtcReady() const;

  private:
    struct Impl;
    Impl* impl();
    const Impl* impl() const;

    Impl* impl_ = nullptr;
};

} // namespace boards::gat562_mesh_evb_pro
