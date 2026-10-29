#include "app/linux_app_facade.h"
#include "call_pcm_fixture.h"
#include "chat/linux_reticulum_network_store.h"
#include "platform/audio/audio_focus.h"
#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/linux/music_service.h"
#include "platform/linux/native_audio_service.h"
#include "ui/app_catalog_builder.h"
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/presentation_sources/runtime_mesh_status_source.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/startup_ui_shell.h"
#include "ui/widgets/reticulum_call_overlay.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace call = platform::ui::reticulum_call;
namespace overlay = ui::widgets::reticulum_call_overlay;
namespace fs = std::filesystem;
uint32_t ticks() { return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
lv_obj_t* label(lv_obj_t* root, const char* text)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(root, &lv_label_class) && !std::strcmp(lv_label_get_text(root), text)) return root;
    for (unsigned index = 0; index < lv_obj_get_child_count(root); ++index)
        if (auto* found = label(lv_obj_get_child(root, index), text)) return found;
    return nullptr;
}
bool recentCallDuration(lv_obj_t* root)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return false;
    if (lv_obj_check_type(root, &lv_label_class))
    {
        const auto* text = lv_label_get_text(root);
        unsigned minutes, seconds;
        if (std::strstr(text, " RX ") && std::sscanf(text, "%u:%u", &minutes, &seconds) == 2)
            return minutes == 0 && seconds < 10;
    }
    for (unsigned index = 0; index < lv_obj_get_child_count(root); ++index)
        if (recentCallDuration(lv_obj_get_child(root, index))) return true;
    return false;
}
int main(int argc, char** argv)
{
    char pattern[] = "/tmp/trailmate-call-ui-XXXXXX";
    const auto* temporary = mkdtemp(pattern);
    assert(temporary);
    const fs::path root(temporary);
    setenv("TRAIL_MATE_SD_ROOT", temporary, 1);
    setenv("TRAIL_MATE_SETTINGS_ROOT", temporary, 1);
    setenv("TRAIL_MATE_CACHE_ROOT", temporary, 1);
    setenv("TRAIL_MATE_GPS_AUTO_SERIAL", "0", 1);
    platform::linux_runtime::PacketRadioHardware radio;
    radio.driver = "none";
    assert(platform::linux_runtime::configure_packet_radio(radio));
    // This fixture injects signalling and must not bootstrap a public TCP
    // gateway while asserting the unavailable-transport presentation.
    assert(platform::linux_runtime::LinuxReticulumNetworkStore::save(
        chat::reticulum::ReticulumNetworkConfig{}, app::AppConfig{}));
    lv_init();
    lv_tick_set_cb(ticks);
    {
        platform::desktop::DeviceOptions options;
        options.title = "TrailMate-CM0-Call-UI-Validation";
        options.ux_pack = "t_display_p4_touch";
        options.width = 1232;
        options.height = 568;
        options.fullscreen = std::getenv("TRAIL_MATE_TEST_FULLSCREEN") != nullptr;
        options.hardware_keyboard = true;
        platform::desktop::SdlLvglHost host(options);
        auto pcm = std::make_shared<call_audio_test::State>();
        platform::linux_runtime::NativeCallAudioBinding binding(call_audio_test::options(pcm));
        assert(binding.snapshot().installed);
        trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade facade({.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceLocal});
        assert(facade.initialize());
        ui::app_catalog_builder::FeatureFlags flags;
        flags.profile = ui::app_catalog_builder::CatalogProfile::IdfDefault;
        flags.include_energy_sweep = flags.include_walkie_talkie = flags.include_sstv = false;
        ui::startup_ui_shell::Hooks hooks;
        hooks.apps = ui::app_catalog_builder::build(flags);
        hooks.ux_pack_id = "t_display_p4_touch";
        assert(ui::startup_ui_shell::beginBootUi(hooks, false, "Call fixture"));
        ui::startup_ui_shell::prepareBootResources();
        assert(ui::startup_ui_shell::initializeMenuSkeleton(hooks) && ui::startup_ui_shell::finalizeStartup(hooks, false));
        const auto step = [&]
        {
            assert(host.pump());
            facade.updateCoreServices();
            facade.tickEventRuntime();
            facade.dispatchPendingEvents();
            call::set_wifi_ready(true); // Injected transport fact, not a real network/peer claim.
            overlay::tick();
            lv_timer_handler();
            ui::i18n::on_lvgl_frame_completed();
            host.present();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        };
        const auto until = [&](auto ready)
        {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            do
            {
                step();
                assert(std::chrono::steady_clock::now() < end);
            } while (!ready());
        };
        const auto capture = [&](const char* name)
        {
            if (argc > 1)
            {
                fs::create_directories(argv[1]);
                lv_refr_now(nullptr);
                host.present();
                host.saveScreenshot((fs::path(argv[1]) / name).string());
            }
        };
        const auto click = [&](const char* text)
        {
            auto* object = label(lv_screen_active(), text);
            assert(object);
            while (object && !lv_obj_check_type(object, &lv_button_class)) object = lv_obj_get_parent(object);
            assert(object && !lv_obj_has_state(object, LV_STATE_DISABLED));
            lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr);
        };
        auto* base = ui::catalogAt(hooks.apps, 0);
        assert(base && ui_open_app(base));
        until([&]
              { return ui_get_active_app() == base; });
        ui::mesh::MeshStatusSnapshot mesh;
        assert(ui::presentation_sources::runtime_mesh_status_source().buildMeshStatusSnapshot(mesh));
        assert(mesh.state == ui::mesh::MeshDisplayState::Off && !mesh.radio_ready && !std::strcmp(mesh.radio_label.c_str(), "Transport unavailable"));
        call::Peer failed;
        failed.destination_hash[0] = 99;
        assert(call::report_dial_failure(failed));
        until([&]
              { return label(lv_layer_top(), "Call could not start") != nullptr; });
        assert(ui_get_active_app() == base && !overlay::visible() && pcm->live == 0);
        capture("dial-failed.bmp");
        ui::feedback::hide_notice();
        until([&]
              { return label(lv_layer_top(), "Call could not start") == nullptr; });
        for (unsigned frame = 0; frame < 10; ++frame) step();
        assert(!label(lv_layer_top(), "Call could not start"));
        for (unsigned iteration = 0; iteration < 2; ++iteration)
        {
            call::Peer peer;
            peer.link_id[0] = uint8_t(20 + iteration);
            peer.destination_hash[0] = 40;
            peer.identity_hash[0] = 60;
            peer.display_name = "Incoming call fixture";
            peer.incoming = true;
            assert(call::begin_incoming(peer));
            call::mark_link_active(peer.link_id);
            until([&]
                  { return overlay::visible() && ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "reticulum_call"); });
            const auto guarded = ticks();
            until([&]
                  { return uint32_t(ticks() - guarded) > 450; });
            capture("incoming.bmp");
            click("Answer");
            assert(call::snapshot().accepted);
            bool opened = false;
            std::thread protocol([&]
                                 { opened = call::prepare_media(peer.link_id) && call::mark_call_active(peer.link_id); });
            until([&]
                  { return call::state() == call::State::Active; });
            protocol.join();
            assert(opened);
            until([&]
                  { return binding.snapshot().statistics.queued_packets > 0; });
            assert(recentCallDuration(lv_screen_active()) && "Call duration must use the runtime clock, independent of the LVGL tick epoch");
            capture("active.bmp");
            click("Mic on");
            step();
            assert(call::snapshot().microphone_muted);
            click("Spk on");
            step();
            assert(call::snapshot().speaker_muted);
            click("FDX");
            step();
            assert(call::snapshot().duplex_mode == call::DuplexMode::Half);
            click("PTT");
            step();
            assert(call::snapshot().ptt_pressed);
            if (!iteration)
            {
                click("Hang up");
                uint8_t link[16];
                assert(call::consume_hangup_request(link));
                call::notify_link_closed(link);
                until([&]
                      { return !overlay::visible() && ui_get_active_app() == base && pcm->live == 0 && !platform::audio::music_blocked(); });
                capture("restored.bmp");
            }
            else
            {
                // The production host dismisses its interruption before the
                // protocol/audio owners and normal page are torn down.
                call::hangup();
                overlay::release();
                assert(!overlay::visible() && ui_get_active_app() == base && !ui_is_interruption_app_active());
                binding.shutdown();
                assert(pcm->live == 0 && !platform::audio::music_blocked());
            }
        }
        ui_exit_active_app(lv_obj_get_child(main_screen, 1));
        facade.shutdown();
        platform::linux_runtime::shutdown_native_audio();
        platform::linux_runtime::shutdown_linux_music();
    }
    lv_deinit();
    fs::remove_all(root);
    std::cout << "Shared P4 call interruption, answer, controls, hangup restoration and active UI shutdown passed with injected signalling/audio\n";
}
