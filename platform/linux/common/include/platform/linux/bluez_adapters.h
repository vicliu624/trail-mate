#pragma once
#include <string>
#include <vector>

namespace platform::linux_runtime
{
struct BluezAdapter
{
    std::string path, address, name;
    bool powered = false, gatt = false, advertising = false;
};
struct BluezAdapters
{
    bool queried = false;
    std::string error;
    std::vector<BluezAdapter> adapters;
};
enum class BluezSelectionStatus
{
    Ready,
    PoweredOff,
    Missing,
    Ambiguous,
    Unsupported,
    InvalidAddress,
    Unavailable
};
struct BluezSelection
{
    BluezSelectionStatus status = BluezSelectionStatus::Unavailable;
    BluezAdapter adapter;
};
// Worker-only D-Bus discovery. An explicit bus address supports isolated tests;
// production callers omit it to use the system bus. No adapter is powered here.
BluezAdapters queryBluezAdapters(const std::string& bus_address = {});
BluezSelection selectBluezAdapter(const BluezAdapters&, const std::string& address = {});
} // namespace platform::linux_runtime
