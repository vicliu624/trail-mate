#include "esp32_lvgl_historical_source_descriptor.h"

namespace trailmate::apps::esp32_lvgl
{

const Esp32LvglHistoricalSourceDescriptor&
esp32LvglHistoricalSourceDescriptor()
{
    static const Esp32LvglHistoricalSourceDescriptor descriptor{};
    return descriptor;
}

} // namespace trailmate::apps::esp32_lvgl
