#pragma once
#include <cstddef>

namespace platform::linux_runtime::settings_storage
{
inline constexpr const char* kAppNamespace = "linux_app_facade";
inline constexpr const char* kAppConfigKey = "app_config_v1";
inline constexpr const char* kNetworkNamespace = "linux_reticulum";
inline constexpr const char* kConfigTransferNamespace = "linux_config_transfer";
inline constexpr const char* kResetMarker = "factory_reset_completed";

// Atomically clear settings and install the caller's default AppConfig blob.
// A committed reset blocks old-process writes to the cleared namespaces until
// exec/restart. Failed transactions leave both data and write admission intact.
bool commit_factory_reset(const void* defaults, std::size_t length);
bool factory_reset_committed();
} // namespace platform::linux_runtime::settings_storage
