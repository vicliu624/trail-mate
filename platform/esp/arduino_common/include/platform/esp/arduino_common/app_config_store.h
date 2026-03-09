#pragma once

#include "app/app_config.h"

class Preferences;

namespace app
{

bool loadAppConfigFromPreferences(AppConfig& config, Preferences& prefs);
bool saveAppConfigToPreferences(AppConfig& config, Preferences& prefs);
bool loadAppConfig(AppConfig& config);
bool saveAppConfig(const AppConfig& config);
uint8_t loadMessageToneVolume();

} // namespace app
