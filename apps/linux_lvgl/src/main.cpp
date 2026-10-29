#include "platform/linux/tmap_creation.h"
#include "ui/input/pager_navigation.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

#include "app/linux_app_facade.h"
#include "lvgl.h"
#include "platform/desktop/device_options.h"
#include "platform/desktop/screen_input_router.h"
#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/linux/application_lifecycle.h"
#include "platform/linux/backlight.h"
#include "platform/linux/keyboard_backlight.h"
#include "platform/linux/music_service.h"
#include "platform/linux/native_audio_service.h"
#include "platform/linux/resource_pack_policy.h"
#include "platform/linux/screen_service.h"
#include "platform/linux/sstv_service.h"
#include "platform/linux/walkie_service.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/lora_runtime.h"
#include "platform/ui/quick_controls_runtime.h"
#include "platform/ui/reticulum_call_runtime.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/sstv_runtime.h"
#include "platform/ui/walkie_runtime.h"
#include "ui/agenda_runtime_binding.h"
#include "ui/app_catalog_builder.h"
#include "ui/app_runtime.h"
#include "ui/components/screen_saver_overlay.h"
#include "ui/localization.h"
#include "ui/lvgl_text_input.h"
#include "ui/runtime/memory_profile.h"
#include "ui/screens/geocaching/geocaching_page_shell.h"
#include "ui/screens/team/team_page_shell.h"
#include "ui/startup_ui_shell.h"
#include "ui/voice_runtime_binding.h"
#include "ui/widgets/reticulum_call_overlay.h"

namespace
{
using Clock = std::chrono::steady_clock;
const auto started = Clock::now();

uint32_t tick()
{
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     Clock::now() - started)
                                     .count());
}

bool dispatchTeam(sys::Event* event)
{
    return team::ui::shell::handle_event(nullptr, event);
}

ui::startup_ui_shell::Hooks compose(const platform::desktop::DeviceOptions& options, ::trailmate::linux_ui::AgendaRuntimeBinding& agenda)
{
    // Use the same catalog and shared page implementations as ESP-IDF/P4.
    // Live Linux capabilities filter unavailable peripheral actions.
    ui::app_catalog_builder::FeatureFlags flags;
    flags.profile = ui::app_catalog_builder::CatalogProfile::IdfDefault;
    flags.include_energy_sweep = platform::ui::lora::is_supported();
    flags.include_sstv = platform::ui::sstv::is_supported();
    flags.include_walkie_talkie = platform::ui::walkie::is_supported();
    flags.calendar_app = agenda.application();
    flags.map_markers = agenda.markers();
    ui::startup_ui_shell::Hooks hooks;
    hooks.apps = ui::app_catalog_builder::build(flags);
    hooks.ux_pack_id = options.ux_pack.c_str();
    hooks.status_refresh_interval_ms = 1000;
    return hooks;
}

struct LvglLifetime
{
    LvglLifetime()
    {
        lv_init();
        lv_tick_set_cb(tick);
    }
    ~LvglLifetime() { lv_deinit(); }
};
struct AppTextRouter
{
    trailmate::cardputer_zero::linux_ui::LvglTextInput input;
    static bool enabled(void* context)
    {
        return static_cast<AppTextRouter*>(context)->input.state().enabled;
    }
    static void commit(void* context, const char* text)
    {
        auto& input = static_cast<AppTextRouter*>(context)->input;
        trailmate::cardputer_zero::app::TextEdit edit;
        edit.focus = input.state().focus;
        edit.commit = text;
        (void)input.apply(edit);
    }
    static void composition(void* context, const char* text, int start, int length)
    {
        namespace text_app = trailmate::cardputer_zero::app;
        const std::string value(text);
        if (value.size() > 4000 || !text_app::validUtf8(value) || start < 0 || length < 0) return;
        const auto characters = text_app::utf8Length(value);
        if (static_cast<uint32_t>(start) > characters || static_cast<uint32_t>(length) > characters - start) return;
        auto& input = static_cast<AppTextRouter*>(context)->input;
        text_app::TextEdit edit;
        edit.focus = input.state().focus;
        edit.preedit = value;
        edit.preedit_begin = static_cast<int32_t>(text_app::utf8ByteAt(value, start));
        edit.preedit_end = static_cast<int32_t>(text_app::utf8ByteAt(value, start + length));
        (void)input.apply(edit);
    }
};
struct AppKeyRouter
{
    bool space_held = false;
    static bool observe(void* context, uint32_t key, bool pressed)
    {
        auto& self = *static_cast<AppKeyRouter*>(context);
        if (key == 0 || (key == ' ' && !pressed))
        {
            const bool consumed = self.space_held;
            if (consumed) platform::ui::walkie::set_ptt(false);
            self.space_held = false;
            return consumed;
        }
        auto* app = ui_get_active_app();
        if (key == ' ' && pressed && app && std::strcmp(app->stable_id(), "walkie_talkie") == 0)
        {
            self.space_held = true;
            platform::ui::walkie::set_ptt(true);
            return true;
        }
        auto* group = lv_group_get_default();
        if (pressed) ::ui::input::pager::prepare_group(group);
        return ::ui::input::pager::handle_key(key, pressed, group);
    }
};
struct MediaLifetime
{
    ~MediaLifetime()
    {
        platform::linux_runtime::shutdown_screen_runtime();
        platform::linux_runtime::shutdown_native_walkie();
        platform::linux_runtime::shutdown_native_sstv();
        platform::linux_runtime::shutdown_backlight();
        platform::linux_runtime::shutdown_keyboard_backlight();
        platform::linux_runtime::shutdown_native_audio();
        platform::linux_runtime::shutdown_linux_music();
        platform::linux_runtime::shutdown_iio_sensors();
    }
};
struct CallUiLifetime
{
    bool released = false;
    void release()
    {
        if (released) return;
        released = true;
        if (::platform::ui::reticulum_call::modal_active()) ::platform::ui::reticulum_call::hangup();
        ::ui::widgets::reticulum_call_overlay::release();
    }
    ~CallUiLifetime() { release(); }
};
} // namespace

int main(int argc, char** argv)
{
    return platform::linux_runtime::run_application(argv, [&]() -> int
                                                    {
    try
    {
        std::string profile;
        std::string screenshot;
        bool windowed = false;
        unsigned long quit_after_ms = 0;
        for (int i = 1; i < argc; ++i)
        {
            const std::string argument = argv[i];
            if (argument == "--windowed") windowed = true;
            else if (argument == "--help")
            {
                std::puts("trailmate-linux --profile FILE [--windowed] [--quit-after-ms N] [--screenshot FILE.bmp]");
                return 0;
            }
            else if (i + 1 < argc && argument == "--profile") profile = argv[++i];
            else if (i + 1 < argc && argument == "--screenshot") screenshot = argv[++i];
            else if (i + 1 < argc && argument == "--quit-after-ms") quit_after_ms = std::stoul(argv[++i]);
            else throw std::runtime_error("Unknown or incomplete argument: " + argument);
        }
        if (profile.empty()) throw std::runtime_error("Select a device with --profile FILE");
        auto options = platform::desktop::loadDeviceOptions(profile);
        if (!platform::linux_runtime::configure_gnss_source(options.gnss))
            throw std::runtime_error("Invalid or already initialized GNSS composition");
        auto paths = platform::linux_runtime::xdg_application_paths();
        if (!options.paths.settings_root.empty()) paths.settings_root = options.paths.settings_root;
        if (!options.paths.sd_root.empty()) paths.sd_root = options.paths.sd_root;
        if (!options.paths.cache_root.empty()) paths.cache_root = options.paths.cache_root;
        if (!platform::linux_runtime::configure_runtime_paths(paths))
            throw std::runtime_error("Invalid or already initialized application directories");
        if (!::ui::i18n::configure_external_pack_root(platform::linux_runtime::resource_pack_root().string()))
            throw std::runtime_error("Language resources were initialized before application directories");
        if (!platform::linux_runtime::configure_resource_pack_policy({platform::ui::device::firmware_version(), ::ui::runtime::current_memory_profile().name}))
            throw std::runtime_error("Resource package policy was initialized with different application facts");
        if (options.auto_keyboard) options.hardware_keyboard = platform::desktop::hardwareKeyboardPresent();
        if (options.ux_pack != TRAIL_MATE_LINUX_UX_PACK)
            throw std::runtime_error("Device UX pack does not match this executable's composition");
        if (windowed) options.fullscreen = false;
        if (!platform::linux_runtime::configure_packet_radio(options.radio))
            throw std::runtime_error("Radio composition already initialized or requested backend not built");
        platform::linux_runtime::set_packet_radio_enabled(platform::ui::settings_store::get_bool("settings", "radio_enabled", true));
        std::unique_ptr<::trailmate::linux_ui::AgendaRuntimeBinding> agenda;
        LvglLifetime lvgl;
        AppKeyRouter key_router;
        AppTextRouter text_router;
        platform::desktop::ScreenInputRouter screen_input(options.height);
        platform::desktop::SdlLvglHost host(options);
        host.setKeyObserver(AppKeyRouter::observe, &key_router);
        host.setInputObserver(platform::desktop::ScreenInputRouter::observe, &screen_input);
        host.setTextObserver(AppTextRouter::enabled, AppTextRouter::commit, &text_router, AppTextRouter::composition);
        MediaLifetime media;
        platform::linux_runtime::ScreenRuntimeOptions screen_options;
        screen_options.resume_method = options.hardware_keyboard ? platform::ui::screen::ResumeMethod::SpaceKey : platform::ui::screen::ResumeMethod::SwipeUp;
        if (options.screen_power == "wayland")
            screen_options.power = std::make_shared<platform::linux_runtime::OutputPowerRuntime>(
                platform::linux_runtime::OutputPowerOptions{options.screen_power_output});
        if (!platform::linux_runtime::configure_screen_runtime(std::move(screen_options)))
            throw std::runtime_error("Screen runtime already initialized");
        if (!platform::linux_runtime::configure_backlight(options.backlight))
            throw std::runtime_error("Invalid or already initialized backlight composition");
        platform::linux_runtime::prepare_backlight();
        if (!platform::linux_runtime::configure_keyboard_backlight(options.keyboard_backlight))
            throw std::runtime_error("Invalid or already initialized keyboard backlight composition");
        platform::linux_runtime::prepare_keyboard_backlight();
        const int brightness = platform::ui::settings_store::get_int("settings", "screen_brightness", -1);
        if (brightness >= 0 && brightness <= 255)
            (void)platform::linux_runtime::request_backlight(static_cast<uint8_t>(brightness));
        platform::ui::device::set_message_tone_volume(static_cast<uint8_t>(std::clamp(
            platform::ui::settings_store::get_int("settings", "speaker_volume", 45), 0, 100)));
        platform::linux_runtime::prepare_native_audio();
        if (!platform::linux_runtime::configure_iio_sensors(options.sensors))
            throw std::runtime_error("Invalid IIO sensor composition");
        auto runtime_mode = options.runtime_mode;
        if (const auto* mode = std::getenv("TRAIL_MATE_RUNTIME_MODE"); mode && *mode)
            runtime_mode = platform::linux_runtime::resolve_runtime_mode();
        trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade facade({.default_node_name = options.title.c_str(), .default_short_name = "TM", .runtime_mode = runtime_mode});
        if (!facade.initialize()) throw std::runtime_error("Cannot initialize Linux application services");
        ::platform::linux_runtime::TmapCreationBinding tmap_creator(options.tmap_creation);
        agenda = std::make_unique<::trailmate::linux_ui::AgendaRuntimeBinding>(
            ::platform::linux_runtime::LinuxAgendaOptions{::platform::linux_runtime::sqlite_database_path(), nullptr, {}});
        struct AgendaUiLifetime
        {
            std::unique_ptr<::trailmate::linux_ui::AgendaRuntimeBinding>& owner;
            ~AgendaUiLifetime() { if (owner) owner->releaseUi(); }
        } agenda_ui{agenda};
        ::trailmate::linux_ui::VoiceRuntimeBinding voice_binding(facade.voiceSource());
        struct GeocachingBinding
        {
            explicit GeocachingBinding(::ui::geocaching::Source* source) { ::geocaching::ui::shell::bind(source); }
            ~GeocachingBinding() { ::geocaching::ui::shell::bind(nullptr); }
        } geocaching_binding(facade.geocachingSource());
        trailmate::cardputer_zero::linux_ui::setTeamUiEventDispatcher(dispatchTeam);
        const auto hooks = compose(options, *agenda);
        if (!ui::startup_ui_shell::beginBootUi(hooks, false, "Loading Trail Mate..."))
            throw std::runtime_error("Cannot initialize boot UI");
        host.present();
        ui::startup_ui_shell::prepareBootResources();
        if (!ui::startup_ui_shell::initializeMenuSkeleton(hooks) ||
            !ui::startup_ui_shell::finalizeStartup(hooks, false))
            throw std::runtime_error("Cannot initialize shared UI");
        platform::ui::screen::Hooks screen_hooks;
        screen_hooks.show_screen_saver = ui::components::screen_saver_overlay::show;
        screen_hooks.hide_screen_saver = ui::components::screen_saver_overlay::hide;
        screen_hooks.present_screen_saver = ui::components::screen_saver_overlay::request_present;
        platform::ui::screen::init(screen_hooks);
        const auto loop_started = tick();
        CallUiLifetime call_ui;
        bool presenting = true;
        while ((presenting = host.pump()) && !platform::linux_runtime::application_restart_requested())
        {
            const auto imu = platform::linux_runtime::linux_imu_snapshot();
            if (imu.ready && imu.rotation_enabled)
                host.setRotation((static_cast<unsigned>(options.rotation) + imu.rotation) % 360);
            facade.updateCoreServices();
            facade.tickEventRuntime();
            facade.dispatchPendingEvents();
            platform::ui::service_quick_controls();
            ::ui::widgets::reticulum_call_overlay::tick();
            agenda->tick();
            platform::linux_runtime::service_screen_runtime();
            lv_timer_handler();
            ::ui::i18n::on_lvgl_frame_completed();
            host.present();
            if (quit_after_ms && tick() - loop_started >= quit_after_ms)
            {
                platform::linux_runtime::request_application_exit();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!presenting) platform::linux_runtime::request_application_exit();
        if (!screenshot.empty()) host.saveScreenshot(screenshot);
        call_ui.release();
        if (ui_get_active_app()) ui_get_active_app()->exit(main_screen);
        trailmate::cardputer_zero::linux_ui::setTeamUiEventDispatcher(nullptr);
        ::geocaching::ui::shell::bind(nullptr);
        voice_binding.release();
        agenda->releaseUi();
        facade.shutdown();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "trailmate-linux: %s\n", error.what());
        return 1;
    } });
}
