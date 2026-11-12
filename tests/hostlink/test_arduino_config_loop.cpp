#include "ui/loop_shell.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
uint32_t clock_now = 100;
bool overlay_active = false, saver_active = false;
std::vector<char> trace;
unsigned sync_count = 0;
bool sync_success = true;
struct Restarted
{
};
} // namespace

uint32_t millis() { return clock_now; }
bool ui_is_overlay_active() { return overlay_active; }
void yield() { trace.push_back('y'); }
void delay(uint32_t) {}

namespace platform::ui::device
{
void restart()
{
    trace.push_back('r');
    throw Restarted{};
}
} // namespace platform::ui::device

namespace app::sd_tms
{
uint32_t s_web_restart_at = 0, s_next_sync_attempt_ms = 0;
bool s_sync_pending = false, s_repair_required = false;
int config = 0;
int* s_working_config = &config;
enum class WorkingConfigSyncResult
{
    Synchronized,
    Failed
};
WorkingConfigSyncResult syncPendingWorkingConfig()
{
    ++sync_count;
    trace.push_back('s');
    if (sync_success)
    {
        s_sync_pending = false;
        return WorkingConfigSyncResult::Synchronized;
    }
    return WorkingConfigSyncResult::Failed;
}
#include "arduino_service_actual.inc"
#include "identity_restart_schedule_actual.inc"
} // namespace app::sd_tms

namespace app
{
struct IAppLifecycleFacade
{
    void updateCoreServices() { trace.push_back('c'); }
    void tickEventRuntime() { trace.push_back('e'); }
    void dispatchPendingEvents(std::size_t) { trace.push_back('d'); }
};
IAppLifecycleFacade& lifecycleFacade()
{
    static IAppLifecycleFacade facade;
    return facade;
}
} // namespace app

namespace platform::esp::arduino_common
{
void tickBoundLifecycle(std::size_t max_events = 4);
// Compile the actual lifecycle too, so moving service delivery cannot leave a
// duplicate call that advances SD synchronization twice in one main-loop turn.
#include "arduino_lifecycle_actual.inc"
namespace storage
{
void tick_deferred_storage() { trace.push_back('t'); }
} // namespace storage
namespace display_runtime
{
void tickIfDue(uint32_t) { trace.push_back('v'); }
} // namespace display_runtime
} // namespace platform::esp::arduino_common

namespace platform::ui::screen
{
bool is_saver_active() { return saver_active; }
} // namespace platform::ui::screen
namespace platform::esp::boards
{
struct Board
{
    void handlePowerButton() {}
};
struct Handles
{
    Board* board;
};
Handles resolveAppContextInitHandles() { return {nullptr}; }
} // namespace platform::esp::boards

namespace usb_cdc
{
bool connected = true;
bool start() { return true; }
bool is_connected() { return connected; }
} // namespace usb_cdc
namespace hostlink
{
bool active = true, schedule_next = false;
void tick_publication_tasks() { trace.push_back('p'); }
bool is_active() { return active; }
void start() { active = true; }
void stop() { active = false; }
void process_pending_commands()
{
    trace.push_back('h');
    if (schedule_next)
    {
        schedule_next = false;
        app::sd_tms::scheduleWebManagementRestart();
    }
}
} // namespace hostlink

#if defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
namespace hostlink::download_mode
{
void service() { trace.push_back('b'); }
} // namespace hostlink::download_mode
#endif

namespace ui::widgets::reticulum_call_overlay
{
void tick() {}
} // namespace ui::widgets::reticulum_call_overlay
namespace trailmate::apps::esp32_lvgl::arduino_agenda
{
void tick() { trace.push_back('a'); }
} // namespace trailmate::apps::esp32_lvgl::arduino_agenda
namespace trailmate::apps::esp32_lvgl::arduino_app_runtime_access
{
struct Status
{
    bool app_context_bound = true;
} s_status;
bool s_was_screen_saver_active = false;
uint32_t s_wake_gate_until_ms = 0;
bool wake_gate_active(uint32_t now_ms)
{
    return s_wake_gate_until_ms != 0 &&
           static_cast<int32_t>(s_wake_gate_until_ms - now_ms) > 0;
}
#include "arduino_app_actual.inc"
} // namespace trailmate::apps::esp32_lvgl::arduino_app_runtime_access
namespace trailmate::apps::esp32_lvgl::arduino_loop_runtime
{
void log_loop_interval(uint32_t) {}
#include "arduino_loop_actual.inc"
} // namespace trailmate::apps::esp32_lvgl::arduino_loop_runtime

namespace
{
void reset()
{
    clock_now = 100;
    overlay_active = saver_active = false;
    trace.clear();
    sync_count = 0;
    sync_success = true;
    app::sd_tms::s_web_restart_at = app::sd_tms::s_next_sync_attempt_ms = 0;
    app::sd_tms::s_sync_pending = app::sd_tms::s_repair_required = false;
    app::sd_tms::s_working_config = &app::sd_tms::config;
    using namespace trailmate::apps::esp32_lvgl::arduino_app_runtime_access;
    s_status.app_context_bound = true;
    s_was_screen_saver_active = false;
    s_wake_gate_until_ms = 0;
    usb_cdc::connected = hostlink::active = true;
    hostlink::schedule_next = false;
}
void tick() { trailmate::apps::esp32_lvgl::arduino_loop_runtime::tick(); }
bool has(char event)
{
    for (char value : trace)
        if (value == event) return true;
    return false;
}
void expect_download_pump()
{
#if defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    assert(has('b'));
#else
    assert(!has('b'));
#endif
}
void expect_restart()
{
    trace.clear();
    bool restarted = false;
    try
    {
        tick();
    }
    catch (const Restarted&)
    {
        restarted = true;
    }
    assert(restarted && has('r') && !has('a'));
}
} // namespace

int main()
{
    // A save pending before the screen saver must still be synchronized. A
    // failing storage attempt has its existing 1 s retry budget while sleeping.
    reset();
    saver_active = true;
    app::sd_tms::s_sync_pending = true;
    sync_success = false;
    tick();
    assert(sync_count == 1 && !has('c') && has('v'));
    expect_download_pump();
    clock_now = 1099;
    tick();
    assert(sync_count == 1);
    clock_now = 1100;
    sync_success = true;
    tick();
    assert(sync_count == 2 && !app::sd_tms::s_sync_pending);

    // A commit received while sleeping retains the complete 2 s ACK window,
    // then restarts without a physical screen wake or another serial request.
    reset();
    saver_active = true;
#if defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    hostlink::schedule_next = true;
    tick();
    assert(has('h'));
#else
    app::sd_tms::scheduleWebManagementRestart();
    tick();
#endif
    assert(app::sd_tms::s_web_restart_at == 2100 && !has('r'));
    clock_now = 2099;
    tick();
    assert(!has('r'));
    clock_now = 2100;
    expect_restart();
    assert(!has('c') && sync_count == 0);

    // An overlay bypasses the app runtime; neither pending synchronization nor
    // an already accepted restart may depend on dismissing that overlay.
    reset();
    overlay_active = true;
    app::sd_tms::s_sync_pending = true;
    tick();
    assert(sync_count == 1 && !has('t') && !has('c') && has('v'));
    expect_download_pump();
    app::sd_tms::scheduleWebManagementRestart();
    clock_now = 2100;
    expect_restart();

    // The post-saver 750 ms gate still pauses normal core/UI work, but does
    // not postpone configuration service or a due management restart.
    reset();
    saver_active = true;
    tick();
    saver_active = false;
    clock_now = 200;
    tick();
    app::sd_tms::s_sync_pending = true;
    clock_now = 300;
    trace.clear();
    tick();
    assert(sync_count == 1 && !has('c'));
    expect_download_pump();
    app::sd_tms::s_web_restart_at = 301;
    clock_now = 301;
    expect_restart();

    reset();
    saver_active = true;
    app::sd_tms::scheduleWebManagementRestart();
    usb_cdc::connected = false;
    clock_now = 2100;
    expect_restart();
#if defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    assert(!hostlink::active);
#endif

    reset();
    app::sd_tms::s_sync_pending = true;
    tick();
    assert(sync_count == 1 && has('c') && has('e') && has('d'));
    expect_download_pump();

    // Deadline arithmetic remains correct across the 32-bit millisecond wrap.
    reset();
    saver_active = true;
    clock_now = UINT32_MAX - 1999;
    app::sd_tms::scheduleWebManagementRestart();
    assert(app::sd_tms::s_web_restart_at == 1);
    clock_now = 0;
    tick();
    assert(!has('r'));
    clock_now = 1;
    expect_restart();
}
