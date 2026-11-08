#include "platform/device/evdev_input.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <linux/input.h>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;

int main()
{
    char path[] = "/tmp/trailmate-evdev-exit-XXXXXX";
    const int file = mkstemp(path);
    assert(file >= 0);
    const auto event = [&](unsigned short type, unsigned short code, int value)
    {
        input_event raw{};
        raw.type = type;
        raw.code = code;
        raw.value = value;
        assert(write(file, &raw, sizeof(raw)) == sizeof(raw));
    };
    {
        trailmate::cardputer_zero::platform::device::EvdevInput input(path);
        assert(input.isOpen() && !input.quitRequested());
        event(EV_KEY, KEY_ESC, 1);
        auto keys = input.drainInput();
        assert(keys.size() == 1 && keys[0].key == trailmate::cardputer_zero::app::InputKey::Power);
        event(EV_KEY, KEY_ESC, 0);
        assert(input.drainInput().empty());
        std::this_thread::sleep_for(1600ms);
        input.drainInput();
        assert(!input.quitRequested());

        event(EV_KEY, KEY_ESC, 1);
        input.drainInput();
        event(EV_SYN, SYN_DROPPED, 0);
        input.drainInput();
        std::this_thread::sleep_for(1600ms);
        input.drainInput();
        assert(!input.quitRequested());

        event(EV_KEY, KEY_ESC, 1);
        input.drainInput();
        std::this_thread::sleep_for(1600ms);
        event(EV_KEY, KEY_ESC, 2); // Kernel repeats do not restart the hold timer.
        assert(input.drainInput().empty());
        assert(input.quitRequested());
        assert(input.reopen(path) && !input.quitRequested());
    }
    close(file);
    unlink(path);
}
