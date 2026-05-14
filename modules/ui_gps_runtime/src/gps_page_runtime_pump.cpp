#include "ui_gps_runtime/gps_page_runtime_pump.h"

namespace ui
{
namespace screens
{
namespace gps
{

GpsPageRuntimePump::GpsPageRuntimePump(IGpsStatusRefreshModel& model,
                                       IGpsUiRefreshSink* ui,
                                       uint32_t interval_ms)
    : model_(model),
      ui_(ui),
      interval_ms_(interval_ms == 0 ? 1000 : interval_ms)
{
}

void GpsPageRuntimePump::setActive(bool active)
{
    active_ = active;
    if (!active_)
    {
        has_last_update_ = false;
        last_update_ms_ = 0;
    }
}

bool GpsPageRuntimePump::active() const
{
    return active_;
}

void GpsPageRuntimePump::update(uint32_t now_ms)
{
    if (!active_)
    {
        return;
    }

    if (has_last_update_ && static_cast<uint32_t>(now_ms - last_update_ms_) < interval_ms_)
    {
        return;
    }

    model_.refresh();
    if (ui_ != nullptr)
    {
        ui_->onGpsRuntimeUpdated();
    }
    last_update_ms_ = now_ms;
    has_last_update_ = true;
}

} // namespace gps
} // namespace screens
} // namespace ui
