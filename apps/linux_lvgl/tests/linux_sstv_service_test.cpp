#include "platform/audio/audio_focus.h"
#include "platform/linux/sstv_service.h"
#include "platform/ui/sstv_runtime.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
void waitFor(const std::function<bool()>& predicate)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() >= end) throw std::runtime_error("SSTV operation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
} // namespace
int main(int argc, char** argv)
{
    require(argc == 3, "PCM fixture and BMP artifact paths required");
    namespace port = platform::ui::sstv;
    char temporary[] = "/tmp/trailmate-sstv-test-XXXXXX";
    require(mkdtemp(temporary), "temporary root failed");
    const std::filesystem::path root(temporary);
    setenv("TRAIL_MATE_SD_ROOT", temporary, 1);
    require(port::is_supported() && !port::is_active() && port::framebuffer() == nullptr, "idle SSTV fabricated an image");
    require(!std::filesystem::exists(root / "sstv"), "idle getter performed storage I/O");
    setenv("TRAIL_MATE_SSTV_PCM_FILE", argv[1], 1);
    require(port::start() && !port::start(), "SSTV accepted overlapping starts");
    waitFor([]
            { return !port::is_active(); });
    require(port::get_status().state == port::State::Complete, port::last_error());
    require(port::framebuffer() && std::string(port::mode_name()) == "BW8", "received image was not published");
    const std::filesystem::path saved(port::last_saved_path());
    require(saved.parent_path() == root / "sstv" && saved.extension() == ".bmp", "image escaped configured storage");
    require(std::filesystem::file_size(saved) == 54 + 288 * 192 * 3, "BMP size/header invalid");
    std::ifstream image(saved, std::ios::binary);
    char magic[2];
    image.read(magic, 2);
    require(magic[0] == 'B' && magic[1] == 'M', "saved image is not BMP");
    std::filesystem::copy_file(saved, argv[2], std::filesystem::copy_options::overwrite_existing);
    setenv("TRAIL_MATE_SSTV_PCM_FILE", "/nonexistent/trailmate-sstv-test", 1);
    require(port::start(), "restart after completion rejected");
    waitFor([]
            { return !port::is_active(); });
    require(port::get_status().state == port::State::Error && !port::get_status().has_image, "missing input fabricated a completed image");
    const auto incomplete = root / "incomplete.pcm";
    {
        std::ofstream partial(incomplete, std::ios::binary);
        partial << std::string(2048, '\0');
    }
    setenv("TRAIL_MATE_SSTV_PCM_FILE", incomplete.c_str(), 1);
    require(port::start(), "incomplete input start rejected");
    waitFor([]
            { return !port::is_active(); });
    require(port::get_status().state == port::State::Error && !port::get_status().has_image,
            "incomplete PCM became a completed image");
    unsetenv("TRAIL_MATE_SSTV_PCM_FILE");
    require(port::start(), "native microphone capture rejected");
    waitFor([]
            { return platform::audio::music_blocked(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    require(port::get_status().state == port::State::Waiting && !port::get_status().has_image, "dummy silence fabricated pixels");
    port::stop();
    setenv("TRAIL_MATE_SSTV_PCM_FILE", argv[1], 1);
    require(port::start(), "immediate restart after stop rejected");
    waitFor([]
            { return !port::is_active(); });
    require(port::get_status().state == port::State::Complete && std::filesystem::path(port::last_saved_path()) != saved,
            "stale capture overwrote the restarted receive operation");
    waitFor([]
            { return !platform::audio::music_blocked(); });
    require(!port::is_active(), "stop left capture active");
    platform::linux_runtime::shutdown_native_sstv();
    require(!port::start(), "shutdown service accepted new capture");
    std::filesystem::remove_all(root);
}
