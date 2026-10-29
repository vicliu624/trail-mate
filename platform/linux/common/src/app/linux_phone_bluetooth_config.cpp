#include "app/linux_phone_bluetooth_config.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "phone/meshtastic/meshtastic_phone_config_bridge.h"
#include "platform/ui/settings_store.h"
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>

namespace trailmate::linux_app
{
namespace
{
namespace bridge = phone::meshtastic::config_bridge;
constexpr const char* settings_namespace = "phone";
constexpr const char* settings_key = "bluetooth_v1";
constexpr std::array<uint8_t, 4> magic{'B', 'L', 'C', 1};
using Encoded = std::array<uint8_t, magic.size() + meshtastic_Config_BluetoothConfig_size>;
bool valid(const meshtastic_Config_BluetoothConfig& config)
{
    return static_cast<unsigned>(config.mode) <= static_cast<unsigned>(meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN) &&
           bridge::isValidBlePin(config.fixed_pin);
}
} // namespace
LinuxPhoneBluetoothConfig::LinuxPhoneBluetoothConfig(std::function<bool()> enabled) : enabled_(std::move(enabled))
{
    // Shared initialization owns defaults; its large module scratch stays off stack.
    const auto empty = std::make_unique<bridge::PersistedState>();
    bridge::initializeConfigState(*empty, enabled_ && enabled_(), 0, &config_, nullptr);
    Encoded bytes{};
    size_t length = 0;
    const auto result = platform::ui::settings_store::read_blob_into(settings_namespace, settings_key, bytes.data(), bytes.size(), &length);
    if (result == platform::ui::settings_store::BlobReadResult::Missing)
    {
        ready_ = true;
        return;
    }
    if (result != platform::ui::settings_store::BlobReadResult::Ok)
    {
        error_ = result == platform::ui::settings_store::BlobReadResult::Unavailable ? "Bluetooth settings unavailable" : "Invalid Bluetooth settings record";
        return;
    }
    if (length < magic.size() || std::memcmp(bytes.data(), magic.data(), magic.size()) != 0)
    {
        error_ = "Unsupported Bluetooth settings record";
        return;
    }
    meshtastic_Config_BluetoothConfig decoded = meshtastic_Config_BluetoothConfig_init_zero;
    auto stream = pb_istream_from_buffer(bytes.data() + magic.size(), length - magic.size());
    if (!pb_decode(&stream, meshtastic_Config_BluetoothConfig_fields, &decoded) || !valid(decoded))
    {
        error_ = "Invalid Bluetooth settings payload";
        return;
    }
    bridge::normalizeBluetoothConfig(&decoded);
    config_ = decoded;
    ready_ = true;
}
bool LinuxPhoneBluetoothConfig::loadBluetoothConfig(meshtastic_Config_BluetoothConfig* out) const
{
    return ready_ && bridge::loadBluetoothConfigView(config_, enabled_ && enabled_(), out);
}
void LinuxPhoneBluetoothConfig::saveBluetoothConfig(const meshtastic_Config_BluetoothConfig& requested)
{
    if (!valid(requested)) throw std::invalid_argument("Invalid Bluetooth pairing configuration");
    auto next = requested;
    bridge::normalizeBluetoothConfig(&next);
    Encoded bytes{};
    std::memcpy(bytes.data(), magic.data(), magic.size());
    auto stream = pb_ostream_from_buffer(bytes.data() + magic.size(), bytes.size() - magic.size());
    if (!pb_encode(&stream, meshtastic_Config_BluetoothConfig_fields, &next) ||
        !platform::ui::settings_store::put_blob(settings_namespace, settings_key, bytes.data(), magic.size() + stream.bytes_written))
        throw std::runtime_error("Bluetooth configuration could not be saved");
    config_ = next;
    ready_ = true;
    error_.clear();
    ++revision_;
}
} // namespace trailmate::linux_app
