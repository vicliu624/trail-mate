#include "platform/ui/reticulum_network_config_runtime.h"
#include "chat/domain/reticulum_network_policy.h"
#include "platform/linux/reticulum_network_config_binding.h"

#include <cstdio>

namespace
{
using Config = chat::reticulum::ReticulumNetworkConfig;
namespace policy = chat::reticulum::network_config_policy;
namespace runtime = platform::ui::reticulum_network_config;
platform::linux_runtime::ReticulumNetworkConfigBinding binding;
Config active_config;
runtime::Status config_status;

void publish(const Config& config, bool explicit_config)
{
    active_config = config;
    config_status.supported = binding.owner && binding.save;
    config_status.valid = chat::reticulum::validateNetworkConfig(config);
    config_status.source = explicit_config ? runtime::Source::Tms : runtime::Source::Defaults;
    config_status.file_present = explicit_config;
    config_status.configured_interfaces = config.interface_count;
    if (++config_status.generation == 0) ++config_status.generation;
    std::snprintf(config_status.message, sizeof(config_status.message), "%s", explicit_config ? "Saved Reticulum network configuration" : "Using Reticulum defaults");
    std::snprintf(config_status.detail, sizeof(config_status.detail), "%s", "Native SQLite network projection");
}
} // namespace

namespace platform::linux_runtime
{
void bind_reticulum_network_config(ReticulumNetworkConfigBinding hooks, const Config& config, bool explicit_config)
{
    binding = hooks;
    publish(config, explicit_config);
}
void publish_reticulum_network_config(void* owner, const Config& config, bool explicit_config)
{
    if (owner == binding.owner) publish(config, explicit_config);
}
void unbind_reticulum_network_config(void* owner)
{
    if (owner != binding.owner) return;
    binding = {};
    active_config = Config{};
    config_status = {};
}
} // namespace platform::linux_runtime

namespace platform::ui::reticulum_network_config
{
void initialize(const chat::MeshConfig&) {}
void poll(const chat::MeshConfig&) {}
const Config& active() { return active_config; }
bool validateForTms(const Config& config) { return chat::reticulum::validateNetworkConfig(config); }
bool setFromTms(const Config& config)
{
    return binding.owner && binding.save && validateForTms(config) && binding.save(binding.owner, config);
}
bool updateTcpEndpoint(std::size_t slot, const char* host, uint16_t port)
{
    Config candidate = active_config;
    return policy::updateTcpEndpoint(candidate, slot, host, port) && setFromTms(candidate);
}
bool restoreDefaultTcpEndpoints()
{
    Config candidate = active_config;
    return policy::restoreDefaultTcpEndpoints(candidate) && setFromTms(candidate);
}
bool snapshotForTms(const chat::MeshConfig&, Config* output)
{
    if (!output || !config_status.supported || !config_status.valid) return false;
    *output = active_config;
    return true;
}
LegacyImportResult importLegacy(const chat::MeshConfig&) { return LegacyImportResult::NotPresent; }
bool discardLegacySource() { return true; }
Status status()
{
    auto result = config_status;
    result.reload_deferred = binding.owner && binding.pending && binding.pending(binding.owner);
    return result;
}
bool reload(const chat::MeshConfig&) { return config_status.supported && config_status.valid; }
bool export_template(const chat::MeshConfig&) { return false; }
bool reset(const chat::MeshConfig& legacy)
{
    Config candidate;
    return policy::build_defaults(legacy, &candidate) && setFromTms(candidate);
}
const char* config_path() { return "SQLite:linux_reticulum/network_tms_v1"; }
const char* source_name(Source source)
{
    switch (source)
    {
    case Source::Defaults:
        return "defaults";
    case Source::Tms:
        return "TMS projection";
    case Source::LegacyJson:
        return "legacy JSON";
    }
    return "unknown";
}
} // namespace platform::ui::reticulum_network_config
