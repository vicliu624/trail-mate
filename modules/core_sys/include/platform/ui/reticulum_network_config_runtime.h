/**
 * @file reticulum_network_config_runtime.h
 * @brief Active Reticulum network configuration snapshot
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/domain/reticulum_network_config.h"

#include <cstddef>
#include <cstdint>

namespace platform::ui::reticulum_network_config
{

enum class Source : uint8_t
{
    Defaults = 0,
    Tms = 1,
    LegacyJson = 2,
};

enum class LegacyImportResult : uint8_t
{
    NotPresent,
    Imported,
    Unavailable,
    Invalid,
};

struct Status
{
    bool supported = false;
    bool sd_present = false;
    bool file_present = false;
    bool valid = false;
    bool reload_deferred = false;
    Source source = Source::Defaults;
    uint32_t generation = 0;
    uint8_t configured_interfaces = 0;
    char message[64] = {};
    char detail[96] = {};
};

void initialize(const chat::MeshConfig& legacy_config);
void poll(const chat::MeshConfig& legacy_config);
const chat::reticulum::ReticulumNetworkConfig& active();
bool validateForTms(const chat::reticulum::ReticulumNetworkConfig& config);
bool setFromTms(const chat::reticulum::ReticulumNetworkConfig& config);
// TCP ordinal, independent of LoRa/Auto interface ordering. Empty host removes
// an existing entry; adding is allowed at the next unused ordinal only.
bool updateTcpEndpoint(std::size_t slot, const char* host, uint16_t port);
// Replace TCP entries only. The caller persists the updated TMS snapshot.
bool restoreDefaultTcpEndpoints();
bool snapshotForTms(const chat::MeshConfig& legacy_config,
                    chat::reticulum::ReticulumNetworkConfig* out);
LegacyImportResult importLegacy(const chat::MeshConfig& legacy_config);
bool discardLegacySource();
Status status();
bool reload(const chat::MeshConfig& legacy_config);
bool export_template(const chat::MeshConfig& legacy_config);
// Resets the runtime projection. The owning TMS repository removes the
// canonical document and legacy migration source during Factory Reset.
bool reset(const chat::MeshConfig& legacy_config);
const char* config_path();
const char* source_name(Source source);

} // namespace platform::ui::reticulum_network_config
