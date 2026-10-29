#include "platform/linux/sstv_service.h"
#include "platform/ui/sstv_runtime.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "Usage: trailmate-linux-sstv-probe INPUT_S16LE_15000_PCM OUTPUT_DIRECTORY\n");
        return 2;
    }
    if (setenv("TRAIL_MATE_SSTV_PCM_FILE", argv[1], 1) ||
        setenv("TRAIL_MATE_SSTV_OUTPUT_DIR", argv[2], 1)) return 2;
    namespace port = platform::ui::sstv;
    if (!port::start()) return 1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (port::is_active() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto status = port::get_status();
    const bool complete = status.state == port::State::Complete && status.has_image;
    std::printf("state=%u line=%u progress=%.3f mode=%s\n", unsigned(status.state), status.line,
                status.progress, port::mode_name());
    if (complete) std::printf("saved=%s\n", port::last_saved_path());
    else std::fprintf(stderr, "SSTV did not complete: %s\n", port::last_error());
    platform::linux_runtime::shutdown_native_sstv();
    return complete ? 0 : 1;
}
