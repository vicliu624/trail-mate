#include "ui/phone_pairing_binding.h"
#include "ui/widgets/ble_pairing_popup.h"

namespace trailmate::linux_ui
{
bool presentPhonePairing(const ::platform::linux_runtime::PhonePairingDisplay& display)
{
    ::ui::BlePairingPopup::setState(display.active, display.passkey, display.fixed, display.local_name.c_str());
    return ::ui::BlePairingPopup::isVisible() == display.active;
}
} // namespace trailmate::linux_ui
