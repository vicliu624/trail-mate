#include "platform/ui/reticulum_call_runtime.h"
#include <cassert>
#include <condition_variable>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <thread>

namespace platform::ui::screen
{
void wake_for_modal() {}
void disable_sleep() {}
void enable_sleep() {}
void record_activity() {}
} // namespace platform::ui::screen
namespace
{
std::mutex mutex;
std::condition_variable changed;
bool entered = false, released = false, late_result = false;
unsigned calls = 0;
bool start()
{
    std::unique_lock<std::mutex> lock(mutex);
    if (++calls != 1) return true;
    entered = true;
    changed.notify_all();
    changed.wait(lock, []
                 { return released; });
    return late_result;
}
} // namespace
int main()
{
    namespace call = platform::ui::reticulum_call;
    for (const bool result : {false, true})
    {
        entered = released = false;
        calls = 0;
        late_result = result;
        call::MediaHooks hooks;
        hooks.is_supported = []
        { return true; };
        hooks.start = start;
        hooks.stop = [] {};
        call::set_media_hooks(hooks);
        call::Peer first, second;
        first.link_id[0] = 1;
        first.destination_hash[0] = 2;
        second.link_id[0] = 3;
        second.destination_hash[0] = 4;
        assert(call::begin_outgoing(first));
        call::mark_link_active(first.link_id);
        bool prepared = true;
        std::thread pending([&]
                            { prepared = call::prepare_media(first.link_id); });
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, []
                         { return entered; });
        }
        const bool early_active = call::mark_call_active(first.link_id);
        call::hangup();
        uint8_t link[16];
        assert(call::consume_hangup_request(link));
        call::notify_link_closed(first.link_id);
        assert(call::begin_outgoing(second));
        call::mark_link_active(second.link_id);
        {
            std::lock_guard<std::mutex> lock(mutex);
            released = true;
        }
        changed.notify_all();
        pending.join();
        const auto current = call::snapshot();
        const bool second_preserved = current.state == call::State::Outgoing && current.accepted && !std::memcmp(current.link_id, second.link_id, 16);
        if (second_preserved)
        {
            assert(call::prepare_media(second.link_id) && call::mark_call_active(second.link_id));
            call::notify_media_failed_for_link(first.link_id);
            call::notify_link_closed(first.link_id);
            assert(call::state() == call::State::Active);
        }
        call::notify_link_closed(second.link_id);
        assert(!early_active && "Media cannot become active while its open callback is still pending");
        assert(!prepared && second_preserved && "A cancelled media open cannot fail or ready the next call");
    }
    call::set_media_hooks({});
    for (const bool result : {false, true})
    {
        entered = released = false;
        calls = 0; late_result = result;
        call::MediaHooks hooks;
        hooks.is_supported = [] { return true; };
        hooks.start = [] { return true; }; hooks.stop = [] {};
        call::set_media_hooks(hooks);
        call::RealtimeHooks realtime;
        realtime.begin_soft_preempt = [](const uint8_t*) { return start(); };
        call::set_realtime_hooks(realtime);
        call::Peer first, second;
        first.link_id[0] = 5; first.destination_hash[0] = 6;
        second.link_id[0] = 7; second.destination_hash[0] = 8;
        bool begun = true;
        std::thread pending([&] { begun = call::begin_incoming_identifying(first); });
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [] { return entered; });
        }
        call::reject(); uint8_t link[16]; assert(call::consume_hangup_request(link));
        call::notify_link_closed(first.link_id);
        assert(call::begin_outgoing(second));
        { std::lock_guard<std::mutex> lock(mutex); released = true; }
        changed.notify_all(); pending.join();
        const auto current = call::snapshot();
        const bool preserved = current.state == call::State::Outgoing && current.accepted && !std::memcmp(current.link_id, second.link_id, 16);
        call::notify_link_closed(second.link_id);
        assert(!begun && preserved && "Late focus admission cannot resurrect or fail a replaced incoming call");
    }
    call::set_media_hooks({}); call::set_realtime_hooks({});
}
