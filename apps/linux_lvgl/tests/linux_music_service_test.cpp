#include "lvgl.h"
#include "music/service.h"
#include "platform/audio/audio_focus.h"
#include "platform/linux/music_service.h"
#include "platform/linux/music_storage.h"
#include "platform/linux/native_audio_service.h"
#include <chrono>
#include <cstdio>
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
::music::Snapshot waitFor(::music::IService& service, const std::function<bool(const ::music::Snapshot&)>& predicate)
{
    ::music::Snapshot state;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do
    {
        service.snapshot(state);
        if (predicate(state)) return state;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    std::fprintf(stderr, "music timeout: notice=%s state=%u rows=%u total=%u busy=%d\n",
                 state.notice, unsigned(state.playback), state.row_count, state.total, state.busy);
    throw std::runtime_error("music state did not converge");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    char name[] = "/tmp/trailmate-music-test-XXXXXX";
    if (!mkdtemp(name)) return 2;
    const std::filesystem::path base(name), root = base / "media";
    setenv("TRAIL_MATE_SD_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SETTINGS_ROOT", (base / "state").c_str(), 1);
    lv_init();
    int result = 0;
    try
    {
        auto* service = platform::linux_runtime::linux_music_service();
        require(service != nullptr, "music proxy missing");
        ::music::Snapshot idle;
        service->snapshot(idle);
        require(!std::filesystem::exists(root), "resolving/reading proxy performed storage I/O");
        {
            platform::linux_runtime::LinuxMusicStorage storage(root);
            require(storage.mkdir("/Music"), "music directory creation failed");
            auto* file = storage.open("/Music/test.bin", ::music::FileMode::Write);
            require(file && file->write("abc", 3) == 3 && file->size() == 3 && file->flush(), "file write/size/flush failed");
            storage.close(file);
            require(storage.remove("/Music/test.bin"), "file deletion failed");
            std::ofstream(base / "outside") << "preserve";
            std::filesystem::create_symlink(base / "outside", root / "Music" / "escape");
            require(!storage.open("/../outside", ::music::FileMode::Write), "parent traversal accepted");
            require(!storage.remove("/Music/escape") && std::filesystem::exists(base / "outside"), "symlink escaped root");
        }
        std::filesystem::copy_file(argv[1], root / "Music" / "first.mp3");
        std::filesystem::copy_file(argv[1], root / "Music" / "second.mp3");
        service->attach(8);
        auto state = waitFor(*service, [](const auto& value)
                             { return !value.busy && value.total == 2 && value.row_count == 2; });
        require(state.audio_supported && state.storage_ready, "dummy PCM output/storage unavailable");
        ::music::Request command;
        command.action = ::music::Action::Play;
        command.id = state.rows[0].id;
        const auto first_id = command.id;
        require(service->send(command), "play queue rejected");
        state = waitFor(*service, [](const auto& value)
                        { return value.playback == ::music::Playback::Playing; });
        require(state.current.id == first_id, "wrong selected song");
        require(platform::linux_runtime::queue_message_tone(45), "notification audio queue rejected");
        waitFor(*service, [](const auto& value)
                { return value.playback == ::music::Playback::Interrupted; });
        waitFor(*service, [](const auto& value)
                { return platform::linux_runtime::native_audio_snapshot().completed_tones == 1 &&
                         value.playback == ::music::Playback::Playing; });
        require(platform::linux_runtime::native_audio_snapshot().failed_tones == 0,
                "notification could not take the physical PCM device from music");
        const auto token = platform::audio::request_focus(platform::audio::FocusOwner::Notification);
        require(bool(token), "music did not release audio focus");
        waitFor(*service, [](const auto& value)
                { return value.playback == ::music::Playback::Interrupted; });
        platform::audio::release_focus(token);
        waitFor(*service, [](const auto& value)
                { return value.playback == ::music::Playback::Playing; });
        command = {};
        command.action = ::music::Action::TogglePause;
        service->send(command);
        waitFor(*service, [](const auto& value)
                { return value.playback == ::music::Playback::Paused; });
        const auto paused_token = platform::audio::request_focus(platform::audio::FocusOwner::Call);
        require(bool(paused_token), "paused focus failed");
        platform::audio::release_focus(paused_token);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        service->snapshot(state);
        require(state.playback == ::music::Playback::Paused, "focus release resumed user-paused music");
        command.action = ::music::Action::Volume;
        command.value = 73;
        service->send(command);
        waitFor(*service, [](const auto& value)
                { return value.volume == 73; });
        require(service->volume() == 73, "volume proxy did not update");
        command = {};
        command.action = ::music::Action::CreatePlaylist;
        command.id = first_id;
        ::music::copy_text(command.text, sizeof(command.text), "Linux playlist");
        service->send(command);
        command = {};
        command.action = ::music::Action::Browse;
        command.browse = ::music::Browse::Playlists;
        service->send(command);
        state = waitFor(*service, [](const auto& value)
                        { return value.browse == ::music::Browse::Playlists && value.total == 1; });
        require(state.row_count == 1 && state.rows[0].count == 1, "playlist did not persist its track");
        command = {};
        command.action = ::music::Action::Stop;
        service->send(command);
        waitFor(*service, [](const auto& value)
                { return value.playback == ::music::Playback::Stopped && value.current.id == 0; });
        service->detach();
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        result = 1;
    }
    platform::linux_runtime::shutdown_native_audio();
    platform::linux_runtime::shutdown_linux_music();
    lv_deinit();
    std::filesystem::remove_all(base);
    return result;
}
