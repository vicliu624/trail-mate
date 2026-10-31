#include "platform/esp/common/walkie_runtime.h"
#include <atomic>
#include <cassert>
#include <mutex>
#include <thread>

static std::mutex s_foreground_mutex;
static bool s_foreground_acquired = false, s_radio_tasks_paused_by_probe = false;
static bool supported = true, paused = false, pause_ok = true, radio_ok = true;
static unsigned pauses = 0, resumes = 0, acquisitions = 0, releases = 0, configurations = 0;
namespace app
{
struct AppTasks
{
    static bool areRadioTasksPaused() { return paused; }
    static bool pauseRadioTasks()
    {
        ++pauses;
        if (!pause_ok) return false;
        paused = true;
        return true;
    }
    static void resumeRadioTasks()
    {
        ++resumes;
        paused = false;
    }
};
struct Config
{
    void applyMeshConfig() { ++configurations; }
};
Config& configFacade()
{
    static Config config;
    return config;
}
} // namespace app
static bool is_supported() { return supported; }
struct Radio
{
    bool acquire()
    {
        ++acquisitions;
        return radio_ok;
    }
    void release()
    {
        assert(configurations == releases + 1);
        ++releases;
    }
};
static Radio& radio()
{
    static Radio device;
    return device;
}
#include "p4_lora_lease.inc"
namespace platform::ui::lora
{
bool acquire() { return ::acquire(); }
void release() { ::release(); }
} // namespace platform::ui::lora
namespace platform::esp::common::walkie_runtime
{
int s_backend_state;
unsigned codec_closes = 0, modem_restores = 0;
bool isSupported() { return supported; }
void* resolve_state(const Session* session) { return session && session->impl == &s_backend_state ? session->impl : nullptr; }
void codecClose(Session* session)
{
    if (resolve_state(session)) ++codec_closes;
}
bool restoreLora(Session* session, char*, size_t)
{
    if (!resolve_state(session)) return false;
    ++modem_restores;
    return true;
}
#include "p4_walkie_lease.inc"
} // namespace platform::esp::common::walkie_runtime
int main()
{
    supported = false;
    assert(!acquire() && !pauses && !acquisitions);
    supported = true;
    pause_ok = false;
    assert(!acquire() && !paused && !s_foreground_acquired);
    pause_ok = true;
    radio_ok = false;
    assert(!acquire() && !paused && !s_radio_tasks_paused_by_probe);
    assert(resumes == 1);
    radio_ok = true;
    assert(acquire() && paused && s_foreground_acquired);
    const auto acquired = acquisitions;
    assert(!acquire() && acquisitions == acquired);
    release();
    assert(!paused && !s_foreground_acquired && configurations == 1 && releases == 1);
    release();
    assert(configurations == 1 && releases == 1);
    // Externally paused tasks remain paused when our own foreground use ends.
    paused = true;
    const auto resumed = resumes;
    assert(acquire());
    release();
    assert(paused && resumes == resumed);
    paused = false;
    // An acquire during physical cleanup must fail immediately, not block UI.
    {
        std::lock_guard<std::mutex> held(s_foreground_mutex);
        std::thread competing([]
                              { assert(!acquire()); });
        competing.join();
    }
    std::atomic<unsigned> winners{0};
    std::thread first([&]
                      { if (acquire()) ++winners; });
    std::thread second([&]
                       { if (acquire()) ++winners; });
    first.join();
    second.join();
    assert(winners == 1);
    release();
    assert(!paused && !s_foreground_acquired && configurations == releases);
    namespace walkie = platform::esp::common::walkie_runtime;
    assert(acquire()); // FM/Probe owns the actual shared foreground path.
    walkie::Session blocked;
    assert(!walkie::tryAcquire(&blocked) && !blocked.impl);
    walkie::release(&blocked);
    assert(s_foreground_acquired); // Failed acquisition cannot release FM.
    release();
    walkie::Session session;
    assert(walkie::tryAcquire(&session) && session.impl);
    assert(!acquire()); // Walkie's production method uses the same lease.
    walkie::release(&session);
    assert(!session.impl && !s_foreground_acquired && !paused);
    assert(walkie::codec_closes == 1 && walkie::modem_restores == 1);
}
