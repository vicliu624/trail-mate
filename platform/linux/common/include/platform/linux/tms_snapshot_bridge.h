#pragma once
#include <string>

namespace platform::linux_runtime
{
enum class TmsSnapshotRead
{
    Busy,
    Ready,
    Unavailable,
    Failed
};
// One bounded request crosses from the HostLink worker to the frontend. The
// callback always runs on the owner's frontend thread, outside the bridge lock.
void bind_tms_snapshot(void* owner);
void unbind_tms_snapshot(void* owner);
bool tms_snapshot_available();
TmsSnapshotRead request_tms_snapshot(std::string& output);
void cancel_tms_snapshot();
void service_tms_snapshot(void* owner, bool (*read)(void*, std::string&));
} // namespace platform::linux_runtime
