#pragma once

#include "app/reticulum_network_records.h"
#include "app/tms_c6_config_extension.h"
#include "app/tms_compatibility_records.h"
#include "app/tms_settings_records.h"
#include <memory>
#include <string>

namespace trailmate::linux_app
{
// Complete current-schema document staging, with no storage or live service
// effects. Keep this object on the heap. Callers commit only a successful parse.
// C6 settings are compatibility data on Linux. Cellular documents are rejected
// explicitly by the strict unknown-record policy until that block has an owner.
struct LinuxTmsDocument
{
    app::AppConfig config;
    app::tms::SettingsRecords settings;
    chat::reticulum::ReticulumNetworkConfig network;
    app::tms::CompatibilityRecords compatibility;
    app::tms::c6::Config companion;

    static std::unique_ptr<LinuxTmsDocument> parse(const char* bytes, size_t length,
                                                   app::tms::DocumentKind kind);
    bool write(app::tms::DocumentKind kind, std::string& output);

  private:
    uint64_t network_seen_ = 0;
    uint32_t access_seen_ = 0;
    app::tms::ReticulumNetworkRecords network_records() { return {network, network_seen_, access_seen_}; }
    static app::tms::RecordConsumeResult consume(void*, const app::tms::RecordReader&);
    static bool finish(void*, bool, uint16_t);
    static bool emit(void*, app::tms::RecordWriter&);
};
} // namespace trailmate::linux_app
