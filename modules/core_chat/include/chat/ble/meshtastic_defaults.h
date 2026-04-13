#pragma once

#include <cstdint>

namespace ble::meshtastic_defaults
{

constexpr uint32_t kConfigNonceOnlyConfig = 69420U;
constexpr uint32_t kConfigNonceOnlyNodes = 69421U;
constexpr uint8_t kMaxMeshtasticChannels = 8U;
constexpr uint32_t kOfficialMinAppVersion = 30200U;
constexpr uint32_t kOfficialDeviceStateVersion = 24U;
constexpr const char* kCompatFirmwareVersion = "2.7.4.0";
constexpr uint32_t kModuleConfigVersion = 1U;

constexpr const char* kDefaultMqttAddress = "mqtt.meshtastic.org";
constexpr const char* kDefaultMqttUsername = "meshdev";
constexpr const char* kDefaultMqttPassword = "large4cats";
constexpr const char* kDefaultMqttRoot = "msh";
constexpr bool kDefaultMqttEncryptionEnabled = true;
constexpr bool kDefaultMqttTlsEnabled = false;
constexpr uint32_t kDefaultMapPublishIntervalSecs = 60U * 60U;
constexpr uint32_t kDefaultDetectionMinBroadcastSecs = 45U;
constexpr uint32_t kDefaultAmbientCurrent = 10U;

} // namespace ble::meshtastic_defaults
