#pragma once

#include "app/app_config.h"

#include <cstdint>

namespace boards::gat562_mesh_evb_pro::settings_store
{

void normalizeConfig(app::AppConfig& config);
bool loadAppConfig(app::AppConfig& config);
bool saveAppConfig(const app::AppConfig& config);
uint8_t loadMessageToneVolume();
bool saveMessageToneVolume(uint8_t volume);

} // namespace boards::gat562_mesh_evb_pro::settings_store
