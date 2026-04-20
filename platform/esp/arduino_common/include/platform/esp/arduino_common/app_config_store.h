#pragma once

#include "app/app_config.h"

namespace app
{

bool loadAppConfig(AppConfig& config);
bool saveAppConfig(const AppConfig& config);
uint8_t loadMessageToneVolume();

} // namespace app
