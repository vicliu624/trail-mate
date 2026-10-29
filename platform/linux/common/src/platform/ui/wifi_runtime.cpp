#include "platform/ui/wifi_runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

#include "platform/linux/network_manager_wifi.h"
#include "platform/linux/process_runner.h"
#include "platform/ui/settings_store.h"

namespace platform::ui::wifi
{
namespace
{
constexpr const char* kSettingsNs = "settings";
constexpr const char* kWifiEnabledKey = "wifi_enabled";

void copy_text(char* output, std::size_t size, const char* value)
{
    if (output && size) std::snprintf(output, size, "%s", value ? value : "");
}

enum class Operation
{
    Refresh,
    Enable,
    Disable,
    Connect,
    AutoConnect,
    Disconnect
};
struct Request
{
    Operation operation;
    Config config{};
};

class NetworkManagerWorker
{
  public:
    NetworkManagerWorker() : worker_([this]
                                     { loop(); }) {}
    ~NetworkManagerWorker()
    {
        stopping_ = true;
        wake_.notify_one();
        worker_.join();
    }
    bool enqueue(Request request)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (requests_.size() >= 8) return false;
        requests_.push_back(request);
        if (request.operation == Operation::Connect || request.operation == Operation::AutoConnect)
            status_.state = ConnectionState::Connecting;
        wake_.notify_one();
        return true;
    }
    Status status()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return status_;
    }
    bool scanResults(ScanResult* output, std::size_t capacity, std::size_t& count)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        count = std::min(capacity, access_points_.size());
        if (count && !output)
        {
            count = 0;
            return false;
        }
        if (count) std::copy_n(access_points_.begin(), count, output);
        return status_.supported;
    }
    bool scanNow(ScanResult* output, std::size_t capacity, std::size_t& count)
    {
        // The existing settings action owns a busy overlay and expects this
        // API to return the completed scan, not a stale pre-scan cache.
        count = 0;
        if (!output && capacity) return false;
        const auto interface = platform::linux_runtime::discover_wireless_interface();
        if (interface.empty()) return false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            status_.scanning = true;
        }
        const auto result = run({"nmcli", "--wait", "5", "-t", "--escape", "yes", "-f",
                                 "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list",
                                 "ifname", interface, "--rescan", "yes"},
                                6);
        auto parsed = platform::linux_runtime::parse_nmcli_access_points(result.output);
        {
            std::lock_guard<std::mutex> guard(mutex_);
            status_.scanning = false;
            if (result.ok()) access_points_ = std::move(parsed);
            else copy_text(status_.message, sizeof(status_.message), "Wi-Fi scan failed or timed out");
        }
        if (!result.ok()) return false;
        scanResults(output, capacity, count);
        return true;
    }

  private:
    platform::linux_runtime::ProcessResult run(const std::vector<std::string>& args, int seconds = 5)
    {
        if (stopping_) return {};
        return platform::linux_runtime::run_process(args, std::chrono::seconds(seconds), &stopping_);
    }
    void refresh(const std::string& interface)
    {
        Status next;
        const auto device = run({"nmcli", "-t", "-f", "GENERAL.STATE,GENERAL.CONNECTION,IP4.ADDRESS",
                                 "device", "show", interface});
        if (device.ok())
        {
            platform::linux_runtime::parse_nmcli_device(device.output, next);
            const auto radio = run({"nmcli", "-t", "-f", "WIFI", "general"});
            if (radio.ok() && radio.output.rfind("disabled", 0) == 0)
            {
                next.enabled = false;
                next.state = ConnectionState::Disabled;
            }
            const auto points = run({"nmcli", "-t", "--escape", "yes", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
                                     "device", "wifi", "list", "ifname", interface, "--rescan", "no"});
            if (points.ok())
            {
                auto parsed = platform::linux_runtime::parse_nmcli_access_points(points.output, &next);
                std::lock_guard<std::mutex> guard(mutex_);
                access_points_ = std::move(parsed);
            }
            next.has_credentials = next.connected;
            copy_text(next.message, sizeof(next.message), next.connected ? "Connected via NetworkManager" : next.enabled ? "Ready"
                                                                                                                         : "Wi-Fi disabled");
        }
        else
        {
            next.state = ConnectionState::Error;
            copy_text(next.message, sizeof(next.message), "NetworkManager unavailable or permission denied");
        }
        std::lock_guard<std::mutex> guard(mutex_);
        status_ = next;
    }
    void loop()
    {
        while (!stopping_)
        {
            Request request{Operation::Refresh};
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!first_) wake_.wait_for(lock, std::chrono::seconds(2), [&]
                                            { return stopping_ || !requests_.empty(); });
                first_ = false;
                if (stopping_) break;
                if (!requests_.empty())
                {
                    request = requests_.front();
                    requests_.pop_front();
                }
            }
            const auto interface = platform::linux_runtime::discover_wireless_interface();
            if (interface.empty())
            {
                std::lock_guard<std::mutex> guard(mutex_);
                status_ = {};
                status_.state = ConnectionState::Unsupported;
                copy_text(status_.message, sizeof(status_.message), "No Linux wireless interface");
                continue;
            }
            platform::linux_runtime::ProcessResult result;
            bool action = true;
            if (request.operation == Operation::Connect || request.operation == Operation::Disable ||
                request.operation == Operation::Disconnect)
            {
                automatic_ = false;
                exhausted_ = false;
                awaiting_ = false;
            }
            switch (request.operation)
            {
            case Operation::Enable:
                result = run({"nmcli", "radio", "wifi", "on"});
                break;
            case Operation::Disable:
                result = run({"nmcli", "radio", "wifi", "off"});
                break;
            case Operation::Disconnect:
                result = run({"nmcli", "device", "disconnect", interface});
                break;
            case Operation::Connect:
                if (request.config.ssid[0])
                {
                    std::vector<std::string> args{"nmcli", "--wait", "15", "device", "wifi", "connect",
                                                  request.config.ssid, "ifname", interface};
                    if (request.config.password[0])
                    {
                        args.emplace_back("password");
                        args.emplace_back(request.config.password);
                    }
                    result = run(args, 18);
                }
                else result = run({"nmcli", "--wait", "15", "device", "connect", interface}, 18);
                break;
            case Operation::AutoConnect:
                action = false;
                startProfiles(interface, false);
                break;
            default:
                action = false;
                break;
            }
            refresh(interface);
            if (request.operation == Operation::Enable && result.ok()) startProfiles(interface, true);
            if (action && !result.ok())
            {
                std::lock_guard<std::mutex> guard(mutex_);
                status_.state = ConnectionState::Error;
                // Never project raw output that could contain credentials.
                copy_text(status_.message, sizeof(status_.message), "NetworkManager request failed; check desktop authorization");
            }
            else serviceProfiles(interface);
        }
    }
    void startProfiles(const std::string& interface, bool explicit_enable)
    {
        bool enabled = false;
        profile_count_ = 0;
        profile_index_ = 0;
        awaiting_ = false;
        exhausted_ = false;
        automatic_ = read_saved_profiles_into(&enabled, &profile_count_, profiles_.data(), profiles_.size()) &&
                     (enabled || explicit_enable) && profile_count_ > 0;
        automatic_interface_ = interface;
    }
    platform::linux_runtime::ProcessResult connectProfile(const Config& config, const std::string& interface)
    {
        std::vector<std::string> args{"nmcli", "--wait", "15", "device", "wifi", "connect", config.ssid, "ifname", interface};
        if (config.password[0])
        {
            args.emplace_back("password");
            args.emplace_back(config.password);
        }
        return run(args, 18);
    }
    void serviceProfiles(const std::string& interface)
    {
        auto current = status();
        if (current.connected)
        {
            // Keep any desktop-owned connection, even if it is not app-owned.
            profile_index_ = 0;
            awaiting_ = false;
            exhausted_ = false;
            return;
        }
        if (exhausted_)
        {
            std::lock_guard<std::mutex> guard(mutex_);
            status_.state = ConnectionState::Error;
            copy_text(status_.message, sizeof(status_.message), "Saved Wi-Fi profiles exhausted; connect or enable to retry");
            return;
        }
        if (!automatic_) return;
        if (interface != automatic_interface_ || !current.enabled || !current.supported)
        {
            automatic_ = false;
            awaiting_ = false;
            return;
        }
        // User operations always take precedence over the next profile. A
        // connection command already running remains bounded by run_process.
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!requests_.empty()) return;
        }
        if (awaiting_)
        {
            if (current.state == ConnectionState::Connecting && std::chrono::steady_clock::now() < attempt_deadline_) return;
            awaiting_ = false;
            if (++profile_index_ >= profile_count_)
            {
                automatic_ = false;
                exhausted_ = true;
                serviceProfiles(interface);
                return;
            }
        }
        else if (current.state == ConnectionState::Connecting) return;
        if (stopping_) return;
        const auto result = connectProfile(profiles_[profile_index_], interface);
        refresh(interface);
        current = status();
        if (current.connected)
        {
            profile_index_ = 0;
            return;
        }
        awaiting_ = true;
        attempt_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        std::lock_guard<std::mutex> guard(mutex_);
        status_.state = result.ok() ? ConnectionState::Connecting : ConnectionState::Error;
        copy_text(status_.message, sizeof(status_.message), result.ok() ? "Waiting for saved Wi-Fi profile" : "Saved Wi-Fi profile failed; trying next");
    }
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Request> requests_;
    Status status_;
    std::vector<ScanResult> access_points_;
    std::atomic_bool stopping_{false};
    bool first_ = true;
    std::array<Config, kMaxSavedProfileCount> profiles_{};
    size_t profile_count_ = 0, profile_index_ = 0;
    bool automatic_ = false, exhausted_ = false, awaiting_ = false;
    std::string automatic_interface_;
    std::chrono::steady_clock::time_point attempt_deadline_;
    std::thread worker_;
};

NetworkManagerWorker& worker()
{
    static NetworkManagerWorker runtime;
    return runtime;
}
} // namespace

bool is_supported()
{
    std::error_code error;
    return !platform::linux_runtime::discover_wireless_interface().empty() &&
           std::filesystem::exists("/usr/bin/nmcli", error);
}
bool apply_enabled(bool enabled)
{
    return is_supported() && worker().enqueue({enabled ? Operation::Enable : Operation::Disable});
}
bool set_user_enabled(bool enabled)
{
    if (!apply_enabled(enabled)) return false;
    settings_store::put_bool(kSettingsNs, kWifiEnabledKey, enabled);
    return true;
}
bool suspend_for_external_storage(ExternalStorageSuspension* suspension)
{
    // Linux filesystem access does not share the ESP radio/SD resource.
    if (suspension) *suspension = {};
    return true;
}
void resume_after_external_storage(ExternalStorageSuspension* suspension)
{
    if (suspension) *suspension = {};
}
bool connect(const Config* config)
{
    Request request{config ? Operation::Connect : Operation::AutoConnect};
    if (config)
    {
        if (!std::memchr(config->ssid, 0, sizeof(config->ssid)) ||
            !std::memchr(config->password, 0, sizeof(config->password))) return false;
        request.config = *config;
    }
    return is_supported() && worker().enqueue(request);
}
void disconnect()
{
    if (is_supported()) worker().enqueue({Operation::Disconnect});
}
bool scan(ScanResult* output, std::size_t capacity, std::size_t& count)
{
    count = 0;
    if (!is_supported()) return false;
    return worker().scanNow(output, capacity, count);
}
Status status()
{
    if (is_supported()) return worker().status();
    Status unavailable;
    unavailable.state = ConnectionState::Unsupported;
    copy_text(unavailable.message, sizeof(unavailable.message), "No NetworkManager wireless interface");
    return unavailable;
}
} // namespace platform::ui::wifi
