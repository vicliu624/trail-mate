#pragma once

#include <cstdint>
#include <vector>

namespace platform::ui::wifi
{

constexpr std::size_t kMaxSsidLength = 32;
constexpr std::size_t kMaxPasswordLength = 64;
constexpr std::size_t kMaxIpLength = 47;
constexpr std::size_t kMaxStatusMessageLength = 95;

enum class ConnectionState : uint8_t
{
    Unsupported = 0,
    Disabled,
    Idle,
    Scanning,
    Connecting,
    Connected,
    Error,
};

struct Config
{
    bool enabled = false;
    char ssid[kMaxSsidLength + 1] = {};
    char password[kMaxPasswordLength + 1] = {};
};

struct Status
{
    bool supported = false;
    bool enabled = false;
    bool connected = false;
    bool scanning = false;
    bool has_credentials = false;
    int rssi = -127;
    char ssid[kMaxSsidLength + 1] = {};
    char ip[kMaxIpLength + 1] = {};
    char message[kMaxStatusMessageLength + 1] = {};
    ConnectionState state = ConnectionState::Unsupported;
};

struct ScanResult
{
    char ssid[kMaxSsidLength + 1] = {};
    int rssi = -127;
    bool requires_password = true;
};

bool is_supported();
bool load_config(Config& out);
bool save_config(const Config& config);
bool apply_enabled(bool enabled);
bool connect(const Config* override_config = nullptr);
void disconnect();
bool scan(std::vector<ScanResult>& out_results);
Status status();

} // namespace platform::ui::wifi
