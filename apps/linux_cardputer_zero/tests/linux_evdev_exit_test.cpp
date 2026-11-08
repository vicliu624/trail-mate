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
    for (const auto code : {KEY_ESC, KEY_HOME})
    {
        assert(ftruncate(file, 0) == 0 && lseek(file, 0, SEEK_SET) == 0);
        trailmate::cardputer_zero::platform::device::EvdevInput input(path);
        assert(input.isOpen() && !input.quitRequested());
        event(EV_KEY, code, 1);
        auto keys = input.drainInput();
        assert(keys.size() == 1 && keys[0].key == (code == KEY_ESC ? trailmate::cardputer_zero::app::InputKey::Power : trailmate::cardputer_zero::app::InputKey::CursorHome));
        event(EV_KEY, code, 0);
        assert(input.drainInput().empty());
        std::this_thread::sleep_for(1600ms);
        input.drainInput();
        assert(!input.quitRequested());

        event(EV_KEY, code, 1);
        input.drainInput();
        event(EV_SYN, SYN_DROPPED, 0);
        input.drainInput();
        std::this_thread::sleep_for(1600ms);
        input.drainInput();
        assert(!input.quitRequested());

        event(EV_KEY, code, 1);
        input.drainInput();
        std::this_thread::sleep_for(1600ms);
        event(EV_KEY, code, 2); // Kernel repeats do not restart the hold timer.
        assert(input.drainInput().empty());
        assert(input.quitRequested());
        assert(input.reopen(path) && !input.quitRequested());
    }
    // Releasing Home cannot cancel an independent Esc hold (or vice versa).
    assert(ftruncate(file, 0) == 0 && lseek(file, 0, SEEK_SET) == 0);
    {
        trailmate::cardputer_zero::platform::device::EvdevInput input(path);
        event(EV_KEY, KEY_HOME, 1);
        event(EV_KEY, KEY_ESC, 1);
        event(EV_KEY, KEY_ESC, 0);
        input.drainInput();
        std::this_thread::sleep_for(1600ms);
        event(EV_KEY, KEY_HOME, 0);
        input.drainInput();
        assert(input.quitRequested());
    }
    assert(ftruncate(file, 0) == 0 && lseek(file, 0, SEEK_SET) == 0);
    {
        trailmate::cardputer_zero::platform::device::EvdevInput input(path);
        event(EV_KEY, KEY_LEFTSHIFT, 1);
        event(EV_KEY, KEY_TAB, 1);
        event(EV_KEY, KEY_LEFTSHIFT, 0);
        event(EV_KEY, KEY_LEFTCTRL, 1);
        event(EV_KEY, KEY_ENTER, 1);
        event(EV_KEY, KEY_LEFTCTRL, 0);
        auto keys = input.drainInput();
        bool shift_tab = false, control_enter = false;
        for (const auto& key : keys)
        {
            if (key.key == trailmate::cardputer_zero::app::InputKey::Tab) shift_tab = key.shift && !key.control;
            if (key.key == trailmate::cardputer_zero::app::InputKey::Enter) control_enter = key.control && !key.shift;
        }
        assert(shift_tab && control_enter);
    }
    close(file);
    unlink(path);
}
