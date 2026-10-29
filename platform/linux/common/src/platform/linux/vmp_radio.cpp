#include "platform/linux/vmp_radio.h"
namespace platform::linux_runtime
{
bool VmpRadioLease::supports(uint8_t channel) const
{
    FskSettings profile;
    return vmp_media_settings(channel, profile) && radio_.supportsFskConfiguration(profile);
}
bool VmpRadioLease::begin(uint8_t channel)
{
    if (acquired_)
    {
        error_ = "VMP radio lease is already active";
        return false;
    }
    error_.clear();
    if (!vmp_media_settings(channel, profile_) || !radio_.supportsFskConfiguration(profile_))
    {
        error_ = "VMP media PHY is unsupported by this radio";
        return false;
    }
    if (!radio_.acquire())
    {
        error_ = radio_.lastError();
        return false;
    }
    acquired_ = true;
    last_restore_ok_ = true;
    control_ = radio_.appliedLoRaConfig();
    if (!radio_.configureLoRa(control_) || !radio_.startReceive()) return fail(radio_.lastError());
    return true;
}
bool VmpRadioLease::switchToMedia()
{
    if (!acquired_)
    {
        error_ = "VMP radio lease is not active";
        return false;
    }
    // Mark before configuration: a failed SDK call may already change modem.
    media_ = true;
    if (!radio_.configureFsk(profile_) || !radio_.startReceive()) return fail(radio_.lastError());
    return true;
}
bool VmpRadioLease::transmit(const uint8_t* bytes, size_t size)
{
    if (!acquired_ || !bytes || !size || size > RadioPacket{}.data.size())
    {
        error_ = "Invalid VMP transmit request";
        return false;
    }
    return radio_.transmit(bytes, size) || fail(radio_.lastError());
}
bool VmpRadioLease::receive(RadioPacket& out)
{
    out = {};
    return acquired_ && radio_.pollReceive(&out);
}
bool VmpRadioLease::end()
{
    if (!acquired_) return last_restore_ok_;
    const bool restored = radio_.configureLoRa(control_) && radio_.startReceive();
    const bool released = radio_.releaseWithStatus();
    acquired_ = media_ = false;
    last_restore_ok_ = restored && released;
    if (!last_restore_ok_) error_ = "VMP could not verify normal radio restoration";
    return last_restore_ok_;
}
bool VmpRadioLease::fail(const char* error)
{
    const std::string cause = error && *error ? error : "VMP radio operation failed";
    const bool restored = end();
    error_ = cause + (restored ? "" : "; normal radio restoration failed");
    return false;
}
} // namespace platform::linux_runtime
