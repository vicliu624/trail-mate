#pragma once
#if defined(TRAIL_MATE_BLUEZ_GIO)
#include <gio/gio.h>
#include <string>
namespace platform::linux_runtime::detail
{
// Returns a dedicated connection; callers may close it without affecting peers.
GDBusConnection* connectBluezBus(const std::string& address, std::string& error);
}
#endif
