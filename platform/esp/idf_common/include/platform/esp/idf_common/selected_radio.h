#pragma once
#if defined(TRAIL_MATE_IDF_RADIO_LR2021)
#if !defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#error "The IDF LR2021 transport requires the T-Display P4 board profile"
#endif
#include "platform/esp/idf_common/lr2021_radio.h"
#else
#include "platform/esp/idf_common/sx126x_radio.h"
#endif

namespace platform::esp::idf_common
{
#if defined(TRAIL_MATE_IDF_RADIO_LR2021)
using SelectedRadio = Lr2021Radio;
inline constexpr const char* kSelectedRadioName = "LR2021";
inline constexpr float kWalkieRxBandwidthKhz = 153.8f;
#else
using SelectedRadio = Sx126xRadio;
inline constexpr const char* kSelectedRadioName = "SX1262";
inline constexpr float kWalkieRxBandwidthKhz = 156.2f;
#endif
} // namespace platform::esp::idf_common
