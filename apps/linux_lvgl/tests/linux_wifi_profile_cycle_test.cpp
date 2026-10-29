// Exercise the private worker with link-time platform fakes. No additional
// production test hooks, NetworkManager daemon or radio changes are required.
#include "../../../platform/linux/common/src/platform/ui/wifi_runtime.cpp"

#include <stdexcept>

namespace
{
struct FakeNetwork
{
    std::mutex mutex;
    std::vector<std::string> attempts;
    std::string connected;
    std::string interface = "renamed-radio";
    bool enabled = true;
    bool saved_enabled = true;
    unsigned disconnects = 0, disables = 0;
    std::string successful = "second";
} network;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
template <typename Predicate>
void await(Predicate predicate, const char* message)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error(message);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
void reset(const std::string& connected = {}, const std::string& successful = "second")
{
    std::lock_guard<std::mutex> guard(network.mutex);
    network.attempts.clear();
    network.connected = connected;
    network.interface = "renamed-radio";
    network.successful = successful;
    network.enabled = network.saved_enabled = true;
    network.disconnects = network.disables = 0;
}
size_t attempts()
{
    std::lock_guard<std::mutex> guard(network.mutex);
    return network.attempts.size();
}
} // namespace

namespace platform::linux_runtime
{
std::string discover_wireless_interface()
{
    std::lock_guard<std::mutex> guard(network.mutex);
    return network.interface;
}
ProcessResult run_process(const std::vector<std::string>& args, std::chrono::milliseconds, const std::atomic_bool*)
{
    std::lock_guard<std::mutex> guard(network.mutex);
    ProcessResult result;
    result.exit_code = 0;
    if (args.size() > 6 && args[3] == "device" && args[4] == "wifi" && args[5] == "connect")
    {
        network.attempts.push_back(args[6]);
        require(args.size() >= 9 && args[7] == "ifname" && args[8] == network.interface, "wrong interface in command");
        if (args[6] == network.successful) network.connected = args[6];
        else result.exit_code = 10;
    }
    else if (args.size() >= 4 && args[1] == "device" && args[2] == "disconnect")
    {
        ++network.disconnects;
        network.connected.clear();
    }
    else if (args.size() >= 4 && args[1] == "radio")
    {
        network.enabled = args[3] == "on";
        if (!network.enabled) ++network.disables;
    }
    else if (args.size() > 4 && args[3] == "GENERAL.STATE,GENERAL.CONNECTION,IP4.ADDRESS")
        result.output = network.enabled ? (network.connected.empty() ? "idle" : "connected") : "disabled";
    else if (args.size() > 4 && args[3] == "WIFI") result.output = network.enabled ? "enabled" : "disabled";
    return result;
}
void parse_nmcli_device(const std::string& output, platform::ui::wifi::Status& state)
{
    using namespace platform::ui::wifi;
    state.supported = true;
    state.enabled = output != "disabled";
    state.connected = output == "connected";
    state.state = state.connected ? ConnectionState::Connected : state.enabled ? ConnectionState::Idle
                                                                               : ConnectionState::Disabled;
}
std::vector<platform::ui::wifi::ScanResult> parse_nmcli_access_points(const std::string&, platform::ui::wifi::Status*) { return {}; }
} // namespace platform::linux_runtime
namespace platform::ui::settings_store
{
void put_bool(const char*, const char*, bool) {}
} // namespace platform::ui::settings_store
namespace platform::ui::wifi
{
bool read_saved_profiles_into(bool* enabled, size_t* count, Config* output, size_t capacity)
{
    std::lock_guard<std::mutex> guard(network.mutex);
    require(capacity >= 2, "profile staging too small");
    *enabled = network.saved_enabled;
    *count = 2;
    output[0] = {};
    output[1] = {};
    std::snprintf(output[0].ssid, sizeof(output[0].ssid), "first");
    std::snprintf(output[1].ssid, sizeof(output[1].ssid), "second");
    return true;
}
} // namespace platform::ui::wifi

int main()
{
    using namespace platform::ui::wifi;
    reset();
    {
        NetworkManagerWorker worker;
        require(worker.enqueue({Operation::AutoConnect}), "auto request rejected");
        await([&]
              { return attempts() >= 1; },
              "first profile not attempted");
        worker.enqueue({Operation::Refresh});
        await([&]
              { return worker.status().connected; },
              "second profile did not connect");
        std::lock_guard<std::mutex> guard(network.mutex);
        require(network.attempts == std::vector<std::string>({"first", "second"}), "priority/fallback order incorrect");
    }
    reset("desktop-owned");
    {
        NetworkManagerWorker worker;
        worker.enqueue({Operation::AutoConnect});
        await([&]
              { return worker.status().connected; },
              "desktop connection not retained");
        require(attempts() == 0, "automatic cycle replaced desktop connection");
    }
    reset({}, "never");
    {
        NetworkManagerWorker worker;
        worker.enqueue({Operation::AutoConnect});
        await([&]
              { return attempts() == 1; },
              "first failure missing");
        worker.enqueue({Operation::Refresh});
        await([&]
              { return attempts() == 2; },
              "second failure missing");
        worker.enqueue({Operation::Refresh});
        await([&]
              { return std::strstr(worker.status().message, "exhausted"); },
              "cycle did not exhaust");
        worker.enqueue({Operation::Refresh});
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        require(attempts() == 2, "exhausted cycle retried automatically");
        std::lock_guard<std::mutex> guard(network.mutex);
        require(!network.disables && !network.disconnects, "cycle exhaustion changed system radio");
    }
    reset();
    {
        NetworkManagerWorker worker;
        worker.enqueue({Operation::AutoConnect});
        await([&]
              { return attempts() == 1; },
              "initial attempt missing");
        worker.enqueue({Operation::Disconnect});
        await([&]
              { std::lock_guard<std::mutex> guard(network.mutex); return network.disconnects == 1; },
              "disconnect not processed");
        worker.enqueue({Operation::Refresh});
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        require(attempts() == 1, "disconnect did not cancel fallback");
    }
    reset();
    {
        NetworkManagerWorker worker;
        worker.enqueue({Operation::AutoConnect});
        await([&]
              { return attempts() == 1; },
              "initial attempt missing");
        worker.enqueue({Operation::Disable});
        await([&]
              { return worker.status().state == ConnectionState::Disabled; },
              "disable not processed");
        require(attempts() == 1, "disable did not cancel fallback");
    }
    reset();
    {
        NetworkManagerWorker worker;
        worker.enqueue({Operation::AutoConnect});
        await([&]
              { return attempts() == 1; },
              "initial attempt missing");
        Request manual{Operation::Connect};
        std::snprintf(manual.config.ssid, sizeof(manual.config.ssid), "manual");
        worker.enqueue(manual);
        await([&]
              { return attempts() == 2; },
              "manual override missing");
        worker.enqueue({Operation::Refresh});
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        std::lock_guard<std::mutex> guard(network.mutex);
        require(network.attempts == std::vector<std::string>({"first", "manual"}), "manual override retained automatic fallback");
    }
    reset();
    {
        NetworkManagerWorker worker;
        worker.enqueue({Operation::AutoConnect});
        await([&]
              { return attempts() == 1; },
              "initial attempt missing");
        {
            std::lock_guard<std::mutex> guard(network.mutex);
            network.interface = "other-radio";
        }
        worker.enqueue({Operation::Refresh});
        await([&]
              { return worker.status().state == ConnectionState::Idle; },
              "interface change not processed");
        require(attempts() == 1, "automatic cycle migrated to another interface");
    }
    reset();
    {
        {
            std::lock_guard<std::mutex> guard(network.mutex);
            network.saved_enabled = false;
        }
        NetworkManagerWorker worker;
        worker.enqueue({Operation::AutoConnect});
        await([&]
              { return worker.status().state == ConnectionState::Idle; },
              "disabled preference not processed");
        require(attempts() == 0, "automatic connect ignored disabled preference");
        worker.enqueue({Operation::Enable});
        await([&]
              { return attempts() == 1; },
              "explicit enable did not start saved profiles");
    }
}
