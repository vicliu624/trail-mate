#pragma once
#include "app/linux_tms_store.h"
#include "hostlink/management_envelope.h"

namespace platform::linux_runtime
{
struct TmsImportCommand
{
    std::array<uint8_t, 16> id{};
    std::string document;
    trailmate::linux_app::TmsCommitOptions commit;
};
void bind_tms_import(void*);
void unbind_tms_import(void*);
bool tms_import_available();
hostlink::management::Status request_tms_import(const TmsImportCommand&);
// Explicit abort can cancel queued work, but cannot race an applying commit.
bool cancel_tms_import(const std::array<uint8_t, 16>& id);
void service_tms_import(void*, hostlink::management::Status (*)(void*, const TmsImportCommand&));
} // namespace platform::linux_runtime
