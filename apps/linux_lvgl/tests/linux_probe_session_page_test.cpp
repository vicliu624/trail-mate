#include "app/app_facade_access.h"
#include "app/linux_app_facade.h"
#include "chat/linux_reticulum_network_store.h"
#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/ui/lora_receive_session.h"
#include "platform/ui/radio_iq_runtime.h"
#include "sys/clock.h"
#include "ui/screens/energy_sweep/energy_sweep_page_runtime.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace
{
uint32_t clock_ms = 1000;
uint32_t now() { return clock_ms; }
platform::ui::lora::ReceiveSessionSnapshot receiver;
unsigned configurations = 0, transmissions = 0, releases = 0;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
void synchronousIo() { throw std::runtime_error("Protocol Probe performed synchronous radio I/O"); }
void pump(uint32_t elapsed)
{
    clock_ms += elapsed;
    lv_tick_inc(elapsed);
    lv_timer_handler();
}
} // namespace
namespace platform::ui::lora
{
bool is_supported() { return true; }
CapabilityStatus capability_status() { return {CapabilityState::Available, "fake receiver"}; }
bool acquire()
{
    synchronousIo();
    return false;
}
bool is_online()
{
    synchronousIo();
    return false;
}
bool configure_receive(float, const ReceiveConfig&)
{
    synchronousIo();
    return false;
}
bool transmit_packet(const uint8_t*, size_t)
{
    synchronousIo();
    return false;
}
bool poll_received_packet(uint8_t*, size_t, ReceivedPacket*)
{
    synchronousIo();
    return false;
}
float read_instant_rssi()
{
    synchronousIo();
    return 0;
}
void release() { synchronousIo(); }
uint32_t request_receive_session(float, const ReceiveConfig&)
{
    ++configurations;
    ++receiver.generation;
    receiver.state = ReceiveSessionState::Configuring;
    receiver.transmit_pending = false;
    return receiver.generation;
}
ReceiveSessionSnapshot receive_session_snapshot() { return receiver; }
bool poll_receive_session(uint32_t generation, uint8_t*, size_t, ReceivedPacket*)
{
    require(generation == receiver.generation && receiver.state == ReceiveSessionState::Ready, "page polled an unready/obsolete receiver");
    return false;
}
bool transmit_receive_session(uint32_t generation, const uint8_t*, size_t size)
{
    require(generation == receiver.generation && receiver.state == ReceiveSessionState::Ready && size, "page transmitted in wrong receiver context");
    ++transmissions;
    receiver.transmit_pending = true;
    return true;
}
void release_receive_session(uint32_t generation)
{
    require(generation == receiver.generation, "page released obsolete receiver");
    ++releases;
    receiver.state = ReceiveSessionState::Idle;
}
bool wait_receive_session_released() { return receiver.state == ReceiveSessionState::Idle; }
namespace detail
{
void prepare_receive_session_worker() {}
} // namespace detail
} // namespace platform::ui::lora

#if defined(TRAIL_MATE_EXTERNAL_RADIO_IQ_RUNTIME)
namespace platform::ui::radio_iq
{
Snapshot fixture;
unsigned starts = 0, joins = 0;
bool is_supported() { return true; }
bool start()
{
    ++starts;
    fixture = {};
    fixture.phase = Phase::Capturing;
    return true;
}
Snapshot snapshot() { return fixture; }
void wait_until_finished()
{
    ++joins;
    fixture.phase = Phase::Idle;
}
} // namespace platform::ui::radio_iq
lv_obj_t* label_containing(lv_obj_t* root, const char* text)
{
    if (lv_obj_check_type(root, &lv_label_class) && std::strstr(lv_label_get_text(root), text)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* found = label_containing(lv_obj_get_child(root, i), text)) return found;
    return nullptr;
}
void check_iq_ui(bool compact, platform::desktop::SdlLvglHost& host)
{
    namespace iq = platform::ui::radio_iq;
    ui_energy_sweep_enter(lv_scr_act());
    auto* label = label_containing(lv_scr_act(), "I  IQ");
    require(label, "IQ entry missing with a supplied runtime");
    auto* button = lv_obj_get_parent(label);
    auto* page = lv_obj_get_parent(lv_obj_get_parent(button));
    const auto capture_fixture = [&](const char* phase)
    {
        const char* directory = std::getenv("TRAIL_MATE_IQ_FIXTURE_SCREENSHOT_DIR");
        if (!directory || !*directory) return;
        std::filesystem::create_directories(directory);
        // Export settled native widget transitions, not the disabled-to-ready
        // animation's first frame. Regular CI assertions do not sleep here.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        pump(200);
        auto* heading = label_containing(page, "IQ DIAGNOSTIC");
        const std::string saved = heading ? lv_label_get_text(heading) : "";
        if (heading) lv_label_set_text(heading, "TEST FIXTURE - NO RF");
        lv_refr_now(nullptr);
        host.saveScreenshot((std::filesystem::path(directory) / (std::string(compact ? "compact-iq-" : "wide-iq-") + phase + ".bmp")).string());
        if (heading) lv_label_set_text(heading, saved.c_str());
    };
    require(lv_obj_get_width(page) == (compact ? 320 : 1232) && lv_obj_get_height(page) == (compact ? 170 : 568),
            "IQ page did not use the requested display dimensions");
    lv_obj_update_layout(page);
    lv_area_t caption, control;
    lv_obj_get_coords(label, &caption);
    lv_obj_get_coords(button, &control);
    require(caption.x1 >= control.x1 && caption.x2 <= control.x2, "IQ entry caption clips outside button");
    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
    require(iq::starts == 1, "IQ button did not reach runtime");
    require(lv_obj_has_state(button, LV_STATE_DISABLED), "capturing IQ can be started twice");
    const auto configured = configurations;
    uint32_t key = 'S';
    lv_obj_send_event(page, LV_EVENT_KEY, &key);
    require(configurations == configured, "probe started while IQ owns radio");
    pump(35);
    require(label_containing(page, "Capturing IQ"), "missing capture progress");
    iq::fixture.phase = iq::Phase::Ready;
    iq::fixture.pairs = 256;
    iq::fixture.rms = 0.5f;
    pump(35);
    auto* result = label_containing(page, "256 IQ pairs");
    require(!lv_obj_has_state(button, LV_STATE_DISABLED), "IQ entry stayed disabled after recovery");
    require(result && std::strstr(lv_label_get_text(result), "rate: unqualified"), "unknown IQ clock fabricated");
    require(std::strstr(lv_label_get_text(result), "no protocol classification"), "IQ presented as protocol evidence");
    lv_obj_update_layout(page);
    auto* viewport = lv_obj_get_parent(result);
    lv_area_t bounds, content;
    lv_obj_get_coords(viewport, &bounds);
    lv_obj_get_coords(lv_obj_get_parent(viewport), &content);
    require(bounds.x1 >= content.x1 && bounds.x2 <= content.x2 && bounds.y1 >= content.y1 && bounds.y2 <= content.y2,
            "IQ result clips outside content");
    auto* progress = label_containing(page, "S: RETURN TO PROBE");
    require(progress, "missing IQ return hint");
    lv_area_t footer;
    lv_obj_get_coords(progress, &footer);
    require(bounds.y2 < footer.y1 && lv_obj_get_width(result) <= lv_obj_get_width(viewport), "IQ overlaps footer or clips horizontally");
    capture_fixture("ready");
    for (unsigned n = 0; n < 20 && lv_obj_get_scroll_bottom(viewport) > 0; ++n)
    {
        key = LV_KEY_DOWN;
        lv_obj_send_event(page, LV_EVENT_KEY, &key);
        lv_obj_update_layout(page);
    }
    require(lv_obj_get_scroll_bottom(viewport) <= 0, "IQ diagnostic tail cannot be reached with Down");
    iq::fixture.phase = iq::Phase::Error;
    std::snprintf(iq::fixture.error, sizeof(iq::fixture.error), "Fixture error: simulated failure while restoring the radio after IQ capture. No RF hardware used.");
    pump(35);
    require(label_containing(page, "No valid IQ published"), "failure retains success report");
    capture_fixture("error");
    lv_obj_update_layout(page);
    for (unsigned n = 0; n < 32 && lv_obj_get_scroll_bottom(viewport) > 0; ++n)
    {
        key = LV_KEY_DOWN;
        lv_obj_send_event(page, LV_EVENT_KEY, &key);
        lv_obj_update_layout(page);
    }
    require(lv_obj_get_scroll_bottom(viewport) <= 0, "long IQ failure cannot be reached with Down");
    key = 'I';
    lv_obj_send_event(page, LV_EVENT_KEY, &key);
    require(iq::starts == 2, "IQ keyboard shortcut missing");
    lv_obj_update_layout(page);
    require(lv_obj_get_scroll_y(viewport) == 0, "new IQ capture retained the previous scroll offset");
    ui_energy_sweep_exit(lv_scr_act());
    require(iq::joins == 1, "page exit did not join radio restoration");
}
#endif

int main(int argc, char** argv)
{
    namespace runtime = platform::linux_runtime;
    namespace probe = energy_sweep::ui::runtime;
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-probe-session-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    for (const char* key : {"TRAIL_MATE_SETTINGS_ROOT", "TRAIL_MATE_SD_ROOT", "TRAIL_MATE_CACHE_ROOT"}) setenv(key, root.c_str(), 1);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    runtime::PacketRadioHardware hardware;
    hardware.driver = "none";
    require(runtime::configure_packet_radio(hardware), "isolated radio setup failed");
    require(runtime::LinuxReticulumNetworkStore::save(chat::reticulum::ReticulumNetworkConfig{}, app::AppConfig{}), "isolated config setup failed");
    lv_init();
    {
        platform::desktop::DeviceOptions options;
        options.ux_pack = "t_display_p4_touch";
        const bool compact = argc > 1 && std::strcmp(argv[1], "compact") == 0;
        options.width = compact ? 320 : 1232;
        options.height = compact ? 170 : 568;
        options.fullscreen = false;
        platform::desktop::SdlLvglHost host(options);
        trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade facade({.runtime_mode = runtime::LinuxRuntimeMode::DeviceLocal});
        require(facade.initialize(), "facade initialization failed");
        require(app::configFacade().switchMeshProtocol(chat::MeshProtocol::MeshCore, false), "MeshCore selection failed");
        sys::set_millis_provider(now);
        require(probe::text_start(), "probe did not accept asynchronous startup");
        require(configurations == 1, "initial candidate missing");
        pump(60000);
        require(configurations == 1 && !transmissions, "configuration delay consumed dwell/started TX");
        receiver.state = platform::ui::lora::ReceiveSessionState::Ready;
        pump(60);
        pump(1200);
        require(transmissions == 1 && receiver.transmit_pending, "active verification not queued");
        pump(60000);
        require(configurations == 1, "response window expired before TX completed");
        receiver.transmit_pending = false;
        pump(60);
        pump(4500);
        require(configurations == 1, "response window not restarted after TX completion");
        pump(120000);
        require(configurations == 2, "completed candidate did not advance");
        receiver.state = platform::ui::lora::ReceiveSessionState::Failed;
        pump(60);
        probe::TextSnapshot snapshot;
        require(probe::text_snapshot(snapshot) && snapshot.radio_error && !snapshot.scanning, "worker failure did not stop probe");
        require(releases == 1, "failed probe did not release its generation");
        probe::text_stop();
#if defined(TRAIL_MATE_EXTERNAL_RADIO_IQ_RUNTIME)
        check_iq_ui(compact, host);
#endif
        sys::set_millis_provider(nullptr);
        facade.shutdown();
    }
    lv_deinit();
    std::filesystem::remove_all(root);
}
