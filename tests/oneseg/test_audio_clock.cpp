#include "platform/linux/oneseg_audio_clock.h"
#include <atomic>
#include <cassert>
#include <thread>
using Clock = platform::linux_runtime::OneSegAudioClock;
int main()
{
    Clock clock;
    assert(!clock.snapshot(0).valid);
    clock.reset(8);
    clock.submit(7, 9000000, 100000, 1000000);
    assert(!clock.snapshot(1000000).valid);
    clock.submit(8, 9000000, 100000, 1000000);
    auto value = clock.snapshot(1000000);
    assert(value.valid && value.pts_us == 8900000 && value.queued_us == 100000);
    value = clock.snapshot(1070000);
    assert(value.valid && value.pts_us == 8970000 && value.queued_us == 30000);
    value = clock.snapshot(1200000);
    assert(!value.valid && value.starved && value.pts_us == 9000000); // Never extrapolate through underrun.
    assert(!clock.snapshot(999999).valid);
    clock.submit(8, INT64_MIN, 100000, 1300000);
    assert(!clock.snapshot(1300000).valid);
    clock.submit(8, INT64_MAX, 100000, 1300000);
    assert(!clock.snapshot(1300000).valid);
    clock.submit(8, -20000, 10000, 1400000);
    assert(clock.snapshot(1405000).pts_us == -25000);
    clock.reset(9);
    assert(!clock.snapshot(1405000).valid);
    clock.submit(8, 123456, 10000, 1405000);
    assert(!clock.snapshot(1405000).valid);
    std::atomic_bool finished{false};
    std::thread producer([&]
                         {
        for (int i = 0; i < 10000; ++i) clock.submit(9, 2000000 + i, 100000, 1000000);
        finished = true; });
    do
    {
        value = clock.snapshot(1000000);
        if (value.valid) assert(value.generation == 9 && value.pts_us >= 1900000 && value.pts_us < 1910000 && value.queued_us == 100000);
    } while (!finished);
    producer.join();
    clock.reset(0);
    assert(!clock.snapshot(2000000).valid);
}
