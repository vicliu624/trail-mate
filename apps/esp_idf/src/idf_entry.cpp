#include "apps/esp_idf/runtime_config.h"
#include "apps/esp_idf/startup_runtime.h"

extern "C" void app_main(void)
{
    apps::esp_idf::startup_runtime::run(apps::esp_idf::runtime_config::get());
}
