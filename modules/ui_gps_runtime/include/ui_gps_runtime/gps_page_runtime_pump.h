#pragma once

#include "ui_gps_runtime/gps_ui_refresh_sink.h"

#include <cstdint>

namespace ui
{
namespace screens
{
namespace gps
{

class IGpsStatusRefreshModel
{
  public:
    virtual ~IGpsStatusRefreshModel() = default;

    virtual void refresh() = 0;
};

class GpsPageRuntimePump
{
  public:
    GpsPageRuntimePump(IGpsStatusRefreshModel& model,
                       IGpsUiRefreshSink* ui,
                       uint32_t interval_ms);

    void setActive(bool active);
    bool active() const;

    void update(uint32_t now_ms);

  private:
    IGpsStatusRefreshModel& model_;
    IGpsUiRefreshSink* ui_ = nullptr;

    uint32_t interval_ms_ = 1000;
    uint32_t last_update_ms_ = 0;
    bool active_ = false;
    bool has_last_update_ = false;
};

} // namespace gps
} // namespace screens
} // namespace ui
