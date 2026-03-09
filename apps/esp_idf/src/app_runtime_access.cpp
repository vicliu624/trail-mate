#include "apps/esp_idf/app_runtime_access.h"

#include "apps/esp_idf/app_facade_runtime.h"

#include "app/app_facade_access.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/idf_common/app_runtime_support.h"

#include "esp_log.h"

namespace apps::esp_idf::app_runtime_access
{
namespace
{

Status s_status{};
RuntimeConfig s_runtime_config{};

} // namespace

void initialize(const RuntimeConfig& config)
{
    if (s_status.initialized)
    {
        return;
    }

    s_status = Status{};
    s_status.initialized = true;
    s_runtime_config = config;

    s_status.app_context_bound = app_facade_runtime::initialize(config);
    s_status.lifecycle_ready = s_status.app_context_bound && app::hasAppFacade();

    platform::esp::boards::AppContextInitHandles handles{};
    s_status.board_handles_ready = platform::esp::boards::tryResolveAppContextInitHandles(&handles) && handles.isValid();

    if (s_status.board_handles_ready)
    {
        ESP_LOGI(s_runtime_config.log_tag,
                 "%s board handles resolved for shared app shell",
                 s_runtime_config.target_name);
    }
    else
    {
        ESP_LOGI(s_runtime_config.log_tag, "%s AppContext handles are not available yet", s_runtime_config.target_name);
    }

    ESP_LOGI(s_runtime_config.log_tag, "IDF application shell initialized for %s", s_runtime_config.target_name);
}

void tick()
{
    if (!s_status.initialized)
    {
        return;
    }

    if (s_status.lifecycle_ready)
    {
        platform::esp::idf_common::tickBoundLifecycle();
    }
}

const Status& status()
{
    return s_status;
}

} // namespace apps::esp_idf::app_runtime_access
