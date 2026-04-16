#include "../../include/ble/bluefruit_runtime.h"

#include <bluefruit.h>

namespace ble::bluefruit_runtime
{
namespace
{

bool s_bluefruit_initialized = false;

} // namespace

void ensureInitialized(const char* device_name)
{
    Bluefruit.autoConnLed(false);
    if (!s_bluefruit_initialized)
    {
        Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
        Bluefruit.begin();
        s_bluefruit_initialized = true;
    }

    Bluefruit.setName(device_name ? device_name : "");
}

} // namespace ble::bluefruit_runtime
