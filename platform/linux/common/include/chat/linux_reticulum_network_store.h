#pragma once

#include "app/app_config.h"
#include "chat/domain/reticulum_network_config.h"
#include "platform/ui/settings_store.h"
#include <string>

namespace platform::linux_runtime
{

// An internal SQLite projection using the shared TMS network record dialect.
// It is separate from the platform's working AppConfig blob and does not claim
// to export the complete device TMS document or its other platform extensions.
class LinuxReticulumNetworkStore
{
  public:
    using Result = ::platform::ui::settings_store::BlobReadResult;
    static Result load(chat::reticulum::ReticulumNetworkConfig& output);
    static bool save(const chat::reticulum::ReticulumNetworkConfig&, const app::AppConfig&);
    // Storage-free projection codec. Export callers can decode bytes obtained
    // from their existing SQLite snapshot without recursively locking the store.
    // Failed operations leave the caller's output unchanged.
    static bool decode(const char* document, size_t length, chat::reticulum::ReticulumNetworkConfig& output);
    static bool encode(const chat::reticulum::ReticulumNetworkConfig&, const app::AppConfig&, std::string& output);
};

} // namespace platform::linux_runtime
