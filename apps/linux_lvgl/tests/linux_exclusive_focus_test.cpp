#include "platform/audio/audio_focus.h"
#include <array>
#include <atomic>
#include <stdexcept>
#include <thread>
using namespace platform::audio;
void require(bool value)
{
    if (!value) throw std::runtime_error("exclusive audio focus contract failed");
}
int main()
{
    auto first = request_focus(FocusOwner::VoiceMessage), second = request_focus(FocusOwner::Call);
    require(first && second && music_blocked());
    require(!request_exclusive_focus(FocusOwner::Walkie));
    release_focus(first);
    release_focus(second);
    std::atomic<unsigned> attempted{0}, granted{0};
    std::atomic_bool start{false}, release{false};
    std::array<std::thread, 2> workers;
    for (auto& worker : workers) worker = std::thread([&]
                                                      {
        while(!start) std::this_thread::yield();
        auto token=request_exclusive_focus(FocusOwner::Walkie);
        if(token) ++granted;
        ++attempted;
        while(!release) std::this_thread::yield();
        release_focus(token); });
    start = true;
    while (attempted != 2) std::this_thread::yield();
    const bool valid = granted == 1 && !request_focus(FocusOwner::Notification);
    release = true;
    for (auto& worker : workers) worker.join();
    require(valid && !music_blocked());
    {
        FocusScope scope(FocusOwner::Sstv, true);
        require(bool(scope));
        require(!request_exclusive_focus(FocusOwner::Call));
    }
    require(!music_blocked());
}
