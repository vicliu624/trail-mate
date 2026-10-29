#pragma once
#include "platform/linux/bluez_phone_gatt.h"
namespace trailmate::linux_ui
{
// Application/LVGL thread only. Both Linux shells reuse the shared P4 widget.
bool presentPhonePairing(const ::platform::linux_runtime::PhonePairingDisplay&);
} // namespace trailmate::linux_ui
