#include "platform/linux/phone_dispatch_queue.h"
#include <atomic>
#include <cassert>
#include <future>
#include <thread>

using platform::linux_runtime::PhoneDispatchQueue;
using namespace std::chrono_literals;
int main()
{
    PhoneDispatchQueue queue;
    std::atomic<unsigned> mutations{0};
    auto expired = std::async(std::launch::async, [&]
                              { return queue.call([&]
                                                  { ++mutations; return true; },
                                                  10ms); });
    assert(!expired.get());
    queue.pump();
    assert(mutations == 0); // Expired writes cannot mutate config after a failed reply.
    const auto application = std::this_thread::get_id();
    auto action = std::async(std::launch::async, [&]
                             { return queue.call([&]
                                                 { assert(std::this_thread::get_id() == application); ++mutations; return true; }); });
    while (action.wait_for(1ms) != std::future_status::ready) queue.pump();
    assert(action.get() && mutations == 1);
    auto throws = std::async(std::launch::async, [&]
                             { return queue.call([]() -> bool
                                                 { throw 1; }); });
    while (throws.wait_for(1ms) != std::future_status::ready) queue.pump();
    assert(!throws.get());
    // A running operation keeps its caller's storage alive past the queue deadline.
    std::promise<void> entered;
    auto began = entered.get_future();
    auto running = std::async(std::launch::async, [&]
                              { return queue.call([&]
                                                  { entered.set_value(); std::this_thread::sleep_for(150ms); ++mutations; return true; },
                                                  100ms); });
    while (began.wait_for(1ms) != std::future_status::ready)
    {
        queue.pump();
    }
    while (running.wait_for(1ms) != std::future_status::ready) queue.pump();
    assert(running.get() && mutations == 2);
    PhoneDispatchQueue stopped;
    auto pending = std::async(std::launch::async, [&]
                              { return stopped.call([&]
                                                    { ++mutations; return true; }); });
    stopped.stop();
    assert(!pending.get());
    assert(!stopped.call([]
                         { return true; }) &&
           stopped.pump() == 0 && mutations == 2);
}
