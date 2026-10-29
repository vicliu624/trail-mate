#include "platform/linux/network_manager_wifi.h"
#include "platform/linux/process_runner.h"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <sys/wait.h>

int main()
{
    using namespace platform::linux_runtime;
    using namespace platform::ui::wifi;
    const auto fields = split_nmcli_record("*:Cafe\\:West\\\\Guest:88:WPA2");
    if (fields.size() != 4 || fields[1] != "Cafe:West\\Guest") return 1;
    Status state;
    parse_nmcli_device("GENERAL.STATE:100 (connected)\nGENERAL.CONNECTION:old-profile\nIP4.ADDRESS[1]:192.0.2.4/24\n", state);
    if (!state.connected || !state.enabled || std::strcmp(state.ip, "192.0.2.4")) return 2;
    auto points = parse_nmcli_access_points("*:Cafe\\:West:88:WPA2\n:Cafe\\:West:40:WPA2\n:Open:50:--\n", &state);
    if (points.size() != 2 || points[0].rssi != -56 || points[1].requires_password ||
        std::strcmp(state.ssid, "Cafe:West")) return 3;
    parse_nmcli_device("GENERAL.STATE:20 (unavailable)\n", state);
    if (state.enabled || state.connected || state.state != ConnectionState::Disabled) return 4;
    const std::string literal = "$(printf injected); ' quoted : value";
    const auto echo = run_process({"/usr/bin/printf", "%s", literal});
    if (!echo.ok() || echo.output != literal) return 5;
    if (run_process({"/usr/bin/false"}).exit_code != 1) return 6;
    const auto limited = run_process({"/usr/bin/head", "-c", "300000", "/dev/zero"});
    if (!limited.ok() || limited.output.size() != 256 * 1024) return 7;
    const auto begin = std::chrono::steady_clock::now();
    if (!run_process({"/usr/bin/sleep", "2"}, std::chrono::milliseconds(30)).timed_out) return 8;
    if (std::chrono::steady_clock::now() - begin > std::chrono::seconds(1)) return 9;
    int child_status = 0;
    if (waitpid(-1, &child_status, WNOHANG) != -1 || errno != ECHILD) return 10;
    std::atomic_bool cancelled{true};
    if (!run_process({"/usr/bin/sleep", "2"}, std::chrono::seconds(5), &cancelled).timed_out) return 11;
    return 0;
}
