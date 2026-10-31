#include "app/linux_app_facade.h"
#include "platform/audio/audio_focus.h"
#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/linux/oneseg_channel_store.h"
#include "platform/linux/packet_radio.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/bundled_font_root.h"
#include "ui/localization.h"
#include "ui/oneseg_tv_app.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <sys/wait.h>
#include <thread>

namespace rt = platform::linux_runtime;
using Clock = std::chrono::steady_clock;
struct RecordedRadio final : rt::IPacketRadio
{
    const std::thread::id ui = std::this_thread::get_id();
    std::atomic_bool present{false};
    std::atomic_bool restore_ok{true};
    std::atomic_bool synthetic_iq{false};
    std::atomic_uint acquired{0}, restored{0};
    std::atomic_uint64_t scanned_channels{0};
    std::string path;
    const char* name() const override { return "TEST FIXTURE - NO RF"; }
    bool supportsOneSegIq() const override { return true; }
    bool hardwareCandidatePresent() const override
    {
        assert(std::this_thread::get_id() != ui);
        return present;
    }
    bool acquire() override
    {
        assert(std::this_thread::get_id() != ui);
        ++acquired;
        return true;
    }
    void release() override { (void)releaseWithStatus(); }
    bool releaseWithStatus() override
    {
        ++restored;
        return restore_ok;
    }
    bool configureLoRa(const rt::RadioSettings&) override { return true; }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t*, size_t) override
    {
        assert(false);
        return false;
    }
    bool pollReceive(rt::RadioPacket*) override { return false; }
    float readRssi() override { return 0; }
    bool isOnline() const override { return true; }
    const char* lastError() const override { return "recorded input fixture"; }
    rt::RadioSettings appliedLoRaConfig() const override { return {}; }
    rt::RadioStatistics stats() const override { return {}; }
    bool streamOneSegIq(const rt::OneSegIqRequest& request, rt::OneSegIqResult& result) override
    {
        assert(request.channel >= 13 && request.channel <= 62);
        scanned_channels.fetch_or(uint64_t{1} << (request.channel - 13));
        if (synthetic_iq)
        {
            // TEST FIXTURE ONLY: the synthetic TS/SI decoder ignores IQ. Keep
            // its input continuous, as a running receiver would, rather than
            // exhausting the separate five-second recorded waveform fixture.
            std::memset(request.staging, 0, 1280);
            while (!request.cancelled(request.context))
            {
                if (!request.accept(request.context, request.staging, 1280, result.validated_bytes / 2)) break;
                result.validated_bytes += 1280;
                std::this_thread::sleep_for(std::chrono::microseconds(1100));
            }
            result.cancelled = true;
            return true;
        }
        std::ifstream input(path, std::ios::binary);
        assert(input);
        while (!request.cancelled(request.context) &&
               (input.read(reinterpret_cast<char*>(request.staging), 1280) || input.gcount()))
        {
            const size_t bytes = static_cast<size_t>(input.gcount());
            if (!request.accept(request.context, request.staging, bytes, result.validated_bytes / 2)) break;
            result.validated_bytes += bytes;
            std::this_thread::sleep_for(std::chrono::microseconds(1100));
        }
        while (!request.cancelled(request.context)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        result.cancelled = true;
        return true;
    }
};
static lv_obj_t* label(lv_obj_t* root, const char* caption)
{
    if (lv_obj_check_type(root, &lv_label_class) && !std::strcmp(lv_label_get_text(root), caption)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* result = label(lv_obj_get_child(root, i), caption)) return result;
    return nullptr;
}
static lv_obj_t* picture(lv_obj_t* root)
{
    if (lv_obj_check_type(root, &lv_image_class)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* result = picture(lv_obj_get_child(root, i))) return result;
    return nullptr;
}
static bool label_contains(lv_obj_t* root, const char* text)
{
    if (lv_obj_check_type(root, &lv_label_class) && std::strstr(lv_label_get_text(root), text)) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (label_contains(lv_obj_get_child(root, i), text)) return true;
    return false;
}
static void click(const char* caption)
{
    auto* found = label(lv_screen_active(), caption);
    assert(found);
    const auto before = Clock::now();
    lv_obj_send_event(lv_obj_get_parent(found), LV_EVENT_CLICKED, nullptr);
    assert(Clock::now() - before < std::chrono::milliseconds(100));
}
static lv_obj_t* channel_selector(lv_obj_t* root)
{
    if (lv_obj_check_type(root, &lv_dropdown_class)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* value = channel_selector(lv_obj_get_child(root, i))) return value;
    return nullptr;
}
static void pump()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    lv_tick_inc(5);
    lv_timer_handler();
}
template <typename Predicate>
static void until(Predicate predicate, const std::source_location where = std::source_location::current())
{
    const auto deadline = Clock::now() + std::chrono::seconds(20);
    while (!predicate() && Clock::now() < deadline) pump();
    if (!predicate()) std::fprintf(stderr, "Timed out at %s:%u\n", where.file_name(), where.line());
    assert(predicate());
}
static void capture(platform::desktop::SdlLvglHost& host, const std::filesystem::path& root, const char* name,
                    const char* provenance = "TEST FIXTURE - NO RF - RECORDED IQ / DUMMY AUDIO")
{
    auto* warning = lv_label_create(lv_layer_top());
    lv_label_set_text(warning, provenance);
    lv_obj_set_style_bg_color(warning, lv_color_hex(0xffe040), 0);
    lv_obj_set_style_bg_opa(warning, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(warning, lv_color_black(), 0);
    lv_obj_align(warning, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_refr_now(nullptr);
    host.present();
    host.saveScreenshot((root / name).string());
    lv_obj_delete(warning);
}
int main(int argc, char** argv)
{
    assert(argc == 5 || (argc == 6 && !std::strcmp(argv[5], "--full-scan")));
    const bool full_scan = argc == 6;
    const std::filesystem::path root = std::filesystem::absolute(argv[4]);
    std::filesystem::create_directories(root);
    const auto saved_channels = root / "settings" / "oneseg-channels.txt";
    std::filesystem::remove(saved_channels); // This test owns the explicit fixture output directory.
    for (const auto* name : {"TRAIL_MATE_SETTINGS_ROOT", "TRAIL_MATE_SD_ROOT", "TRAIL_MATE_CACHE_ROOT"}) setenv(name, root.c_str(), 1);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    rt::PacketRadioHardware isolated;
    isolated.driver = "none";
    assert(rt::configure_packet_radio(isolated));
    lv_init();
    if (const char* pack_root = std::getenv("TRAIL_MATE_RESOURCE_PACK_ROOT"))
        assert(::ui::i18n::configure_external_pack_root(pack_root));
    if (const char* pack_root = std::getenv("TRAIL_MATE_TEST_BUNDLED_FONT_ROOT"))
        assert(::ui::i18n::configure_bundled_font_root(pack_root));
    {
        platform::desktop::DeviceOptions options;
        options.ux_pack = "t_display_p4_touch";
        options.width = 1232;
        options.height = 568;
        options.fullscreen = false;
        platform::desktop::SdlLvglHost host(options);
        trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade facade({.runtime_mode = rt::LinuxRuntimeMode::DeviceLocal});
        assert(facade.initialize());
        if (!app_g) app_g = lv_group_create();
        rt::OneSegTvSession::Config config;
        config.decoder = argv[1];
        std::string python, libraries;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[2]))
        {
            const auto name = entry.path().filename().string();
            if (entry.is_directory() && (name == "site-packages" || name == "dist-packages")) python += entry.path().string() + ":";
            if (entry.is_regular_file() && name.find("libgnuradio-isdbt.so") == 0) libraries += entry.path().parent_path().string() + ":";
        }
        config.environment = {"PYTHONPATH=" + python, "LD_LIBRARY_PATH=" + libraries};
        RecordedRadio radio;
        radio.path = argv[3];
        auto session = std::make_unique<rt::OneSegTvSession>(radio, config, []
                                                             { return true; });
        auto* state = session.get();
        {
            rt::OneSegTvApp page(std::move(session));
            page.enter(lv_screen_active());
            click("CH +");
            assert(label(lv_screen_active(), "CH 14"));
            click("Vol +");
            assert(state->snapshot().volume == 75);
            click("Vol -");
            assert(state->snapshot().volume == 70);
            click("Start");
            assert(state->snapshot().channel == 14);
            until([&]
                  { return !state->snapshot().active; });
            assert(state->snapshot().error == rt::OneSegTvSession::Error::Radio && radio.acquired == 0);
            until([&]
                  { return label(lv_screen_active(), "Start") != nullptr; });
            capture(host, root, "tv-missing-radio.bmp");
            radio.present = true;
            click("Start");
            until([&]
                  { return state->snapshot().presented_frames > 0 || !state->snapshot().active; });
            const auto first_picture = state->snapshot();
            if (!first_picture.active || !first_picture.presented_frames)
                std::fprintf(stderr, "TV automatic first picture: phase=%u error=%u message=%s rxerror=%u rxmessage=%s mediaerror=%u mediamessage=%s exit=%d signal=%d decoder=%s stderr=%s\n",
                             static_cast<unsigned>(first_picture.phase), static_cast<unsigned>(first_picture.error), first_picture.message,
                             static_cast<unsigned>(first_picture.receive.error), first_picture.receive.message,
                             static_cast<unsigned>(first_picture.media.error), first_picture.media.message,
                             first_picture.decoder.child_exit_code, first_picture.decoder.child_signal, first_picture.decoder.message, first_picture.decoder.stderr_tail);
            assert(first_picture.active && first_picture.presented_frames > 0);
            assert(state->snapshot().timing == rt::OneSegTvSession::Timing::AudioQueueEstimate);
            assert(state->snapshot().av_delta_us >= -250000 && state->snapshot().av_delta_us <= 30000);
            until([&]
                  { return state->snapshot().media.pcm_samples > 0; });
            auto* image = picture(lv_screen_active());
            assert(image && !lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN));
            auto* pixels = static_cast<const lv_image_dsc_t*>(lv_image_get_src(image));
            assert(lv_obj_get_width(lv_obj_get_parent(image)) == 320 && lv_obj_get_height(lv_obj_get_parent(image)) == 240);
            assert(lv_image_get_scale(image) <= 256);
            assert(pixels && pixels->data_size && pixels->data && pixels->header.cf == LV_COLOR_FORMAT_RGB565);
            bool varied = false;
            for (size_t i = 2; i < pixels->data_size; ++i)
                if (pixels->data[i] != pixels->data[i % 2])
                {
                    varied = true;
                    break;
                }
            assert(varied); // decoded picture, not a blank substitute
            assert(!lv_obj_has_state(lv_obj_get_parent(label(lv_screen_active(), "CH +")), LV_STATE_DISABLED));
            rt::OneSegChannelStore saved(saved_channels);
            std::string storage_error;
            assert(saved.load(storage_error) && saved.lastChannel() && saved.lastChannel()->channel == 14);
            const auto generation = state->snapshot().generation;
            click("CH +");
            until([&]
                  { const auto current = state->snapshot(); return current.generation > generation && current.channel == 15 && current.presented_frames > 0; });
            assert(radio.restored == 1);
            const auto next_generation = state->snapshot().generation;
            click("CH -");
            until([&]
                  { const auto current = state->snapshot(); return current.generation > next_generation && current.channel == 14 && current.presented_frames > 0; });
            assert(radio.restored == 2);
            assert(label(lv_screen_active(), "CH 14"));
            const auto station_generation = state->snapshot().generation;
            click("Station +");
            until([&]
                  { const auto current = state->snapshot(); return current.generation > station_generation && current.channel == 15 && current.presented_frames > 0; });
            assert(radio.restored == 3);
            const auto return_generation = state->snapshot().generation;
            click("Station -");
            until([&]
                  { const auto current = state->snapshot(); return current.generation > return_generation && current.channel == 14 && current.presented_frames > 0; });
            assert(radio.restored == 4);
            if (state->snapshot().decoder.programmes.program_count < 2)
                assert(lv_obj_has_state(lv_obj_get_parent(label(lv_screen_active(), "Next service")), LV_STATE_DISABLED));
            if (std::getenv("TRAIL_MATE_RESOURCE_PACK_ROOT"))
            {
                until([&]
                      { return state->snapshot().service_text.service_valid; });
                const auto received = state->snapshot().service_text;
                assert(received.name && !received.name.text.empty());
                until([&]
                      { return label_contains(lv_screen_active(), received.name.text.c_str()); });
                assert(!label_contains(lv_screen_active(), "Japanese content font unavailable"));
                until([&]
                      {
                          rt::OneSegChannelStore persisted(saved_channels);
                          std::string error;
                          return persisted.load(error) && persisted.lastChannel() &&
                                 persisted.lastChannel()->channel == state->snapshot().channel &&
                                 persisted.lastChannel()->name == received.name.text; });
                auto* selector = channel_selector(lv_screen_active());
                assert(selector && std::strstr(lv_dropdown_get_options(selector), received.name.text.c_str()));
                click("Menu");
                lv_dropdown_open(selector);
                capture(host, root, "tv-saved-names.bmp");
                lv_dropdown_close(selector);
                click("Menu");
            }
            capture(host, root, "tv-decoded.bmp");
            click("Fullscreen");
            const auto ordinal = state->snapshot().presented_frames;
            until([&]
                  { return state->snapshot().presented_frames > ordinal; });
            lv_obj_update_layout(lv_screen_active());
            auto* viewport = lv_obj_get_parent(image);
            assert(lv_obj_get_width(viewport) == 320 && lv_obj_get_height(viewport) == 240);
            assert(lv_image_get_scale(image) <= 256);
            assert(pixels->header.w * lv_image_get_scale(image) / 256 <= lv_obj_get_content_width(viewport));
            assert(pixels->header.h * lv_image_get_scale(image) / 256 <= lv_obj_get_content_height(viewport));
            capture(host, root, "tv-fullscreen.bmp");
            click("Fullscreen");
            until([&]
                  { return label(lv_screen_active(), "Stop") != nullptr; });
            until([&]
                  { return state->snapshot().audio_clock.starved && state->snapshot().iq_age_ms > 2100; });
            until([&]
                  { return lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN); });
            assert(state->snapshot().timing == rt::OneSegTvSession::Timing::AudioStarved);
            assert(state->snapshot().phase != rt::OneSegTvSession::Phase::FramePresented);
            until([&]
                  { return label(lv_screen_active(), "Signal interrupted\nRetry / R to reacquire") != nullptr; });
            capture(host, root, "tv-starved.bmp");
            const auto lost_generation = state->snapshot().generation;
            click("Retry");
            until([&]
                  { const auto current = state->snapshot(); return current.generation > lost_generation && current.channel == 14 && current.presented_frames > 0; });
            assert(state->snapshot().decoder.programmes.selected_program == 8);
            assert(state->snapshot().volume == 70 && radio.restored == 5);
            assert(!lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN));
            until([&]
                  { return label_contains(lv_screen_active(), "Reception recovered"); });
            assert(!label_contains(lv_screen_active(), "Retrying selected channel"));
            capture(host, root, "tv-retry-recovered.bmp");
            click("Stop");
            until([&]
                  { return !state->snapshot().active; });
            until([&]
                  { return lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN); });
            assert(state->snapshot().error == rt::OneSegTvSession::Error::None && radio.restored == 6);
            until([&]
                  { return label(lv_screen_active(), "Start") != nullptr; });
            click("Start");
            const auto before = Clock::now();
            page.exit(lv_screen_active());
            assert(Clock::now() - before < std::chrono::milliseconds(100));
            until([&]
                  { return !state->snapshot().active; });
            assert(!platform::audio::music_blocked());
            int status;
            assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
        }
        // A new app/session instance must restore the persisted station, not
        // rely on the previous page object's in-memory channel field.
        {
            auto resumed_session = std::make_unique<rt::OneSegTvSession>(radio, config, []
                                                                         { return true; });
            auto* resumed = resumed_session.get();
            rt::OneSegTvApp page(std::move(resumed_session));
            page.enter(lv_screen_active());
            until([&]
                  { return resumed->snapshot().presented_frames > 0 || !resumed->snapshot().active; });
            assert(resumed->snapshot().active && resumed->snapshot().channel == 14 && resumed->snapshot().presented_frames > 0);
            // The actual menu drives the real session/decoder with recorded IQ.
            // Cancel before a full 50-channel scan; the saved list must survive.
            rt::OneSegChannelStore before_scan(saved_channels);
            std::string scan_error;
            assert(before_scan.load(scan_error));
            const auto previous_count = before_scan.channels().size();
            click("Menu");
            click("Scan channels");
            assert(resumed->snapshot().volume == 0);
            click("Vol +");
            assert(resumed->snapshot().volume == 0);
            until([&]
                  { const auto current = resumed->snapshot(); return current.active && current.channel == 13; });
            assert(label(lv_screen_active(), "Cancel scan"));
            assert(lv_obj_has_flag(picture(lv_screen_active()), LV_OBJ_FLAG_HIDDEN));
            capture(host, root, "tv-scan-progress.bmp");
            click("Cancel scan");
            until([&]
                  { const auto current = resumed->snapshot(); return current.active && current.channel == 14 && current.presented_frames > 0; });
            assert(resumed->snapshot().volume == 70);
            rt::OneSegChannelStore after_scan(saved_channels);
            assert(after_scan.load(scan_error) && after_scan.channels().size() == previous_count);
            assert(after_scan.lastChannel() && after_scan.lastChannel()->channel == 14);
            assert(label(lv_screen_active(), "Scan channels"));
            click("Menu");
            auto* selector = channel_selector(lv_screen_active());
            assert(selector && lv_dropdown_get_selected(selector) == 0);
            assert(lv_dropdown_get_option_count(selector) == after_scan.channels().size());
            const auto selected_generation = resumed->snapshot().generation;
            lv_dropdown_set_selected(selector, 1);
            lv_obj_send_event(selector, LV_EVENT_VALUE_CHANGED, nullptr);
            until([&]
                  { const auto current = resumed->snapshot(); return current.generation > selected_generation && current.channel == 15 && current.presented_frames > 0; });
            assert(lv_obj_has_flag(lv_obj_get_parent(selector), LV_OBJ_FLAG_HIDDEN));
            click("Menu");
            assert(lv_dropdown_get_selected(selector) == 1);
            lv_dropdown_set_selected(selector, 0);
            lv_obj_send_event(selector, LV_EVENT_VALUE_CHANGED, nullptr);
            until([&]
                  { const auto current = resumed->snapshot(); return current.channel == 14 && current.presented_frames > 0; });
            if (full_scan)
            {
                const auto received_scan_name = resumed->snapshot().service_text.name.text;
                if (std::getenv("TRAIL_MATE_RESOURCE_PACK_ROOT")) assert(!received_scan_name.empty());
                // TEST FIXTURE - NO RF: repeat the same valid recorded waveform
                // at every requested frequency to exercise the full scheduler.
                radio.scanned_channels = 0;
                click("Menu");
                click("Scan channels");
                const auto deadline = Clock::now() + std::chrono::minutes(12);
                unsigned reported = 0;
                while (Clock::now() < deadline)
                {
                    pump();
                    const auto current = resumed->snapshot();
                    if (current.channel != reported)
                    {
                        reported = current.channel;
                        std::fprintf(stderr, "TEST FIXTURE - NO RF full scan channel %u\n", reported);
                    }
                    if (current.active && current.channel == 14 && current.volume == 70 && current.presented_frames &&
                        lv_obj_has_flag(lv_obj_get_parent(selector), LV_OBJ_FLAG_HIDDEN)) break;
                }
                const auto finished = resumed->snapshot();
                assert(finished.active && finished.channel == 14 && finished.volume == 70 && finished.presented_frames);
                assert(radio.scanned_channels == ((uint64_t{1} << 50) - 1));
                rt::OneSegChannelStore complete(saved_channels);
                assert(complete.load(scan_error) && complete.channels().size() == 50);
                for (size_t i = 0; i < complete.channels().size(); ++i)
                {
                    assert(complete.channels()[i].channel == 13 + i && complete.channels()[i].service == 8);
                    if (std::getenv("TRAIL_MATE_RESOURCE_PACK_ROOT")) assert(complete.channels()[i].name == received_scan_name);
                }
                assert(complete.lastChannel() && complete.lastChannel()->channel == 14);
                std::fprintf(stderr, "TEST FIXTURE - NO RF full 50-channel scan saved and resumed\n");
                capture(host, root, "tv-scan-completed.bmp");
            }
            // Cleanup failure must prevent an automatic replacement receiver.
            radio.restore_ok = false;
            const auto old_generation = resumed->snapshot().generation;
            const auto old_acquired = radio.acquired.load();
            click("CH +");
            until([&]
                  { return !resumed->snapshot().active; });
            const auto settle = Clock::now() + std::chrono::milliseconds(150);
            while (Clock::now() < settle) pump();
            assert(resumed->snapshot().generation == old_generation && radio.acquired == old_acquired);
            assert(resumed->snapshot().receive.lease_restore_failed || resumed->snapshot().receive.error == rt::OneSegReceiveWorker::Error::RadioRestore);
            click("Retry"); // Even a synthetic click must not bypass the guard.
            const auto retry_settle = Clock::now() + std::chrono::milliseconds(150);
            while (Clock::now() < retry_settle) pump();
            assert(resumed->snapshot().generation == old_generation && radio.acquired == old_acquired);
            page.exit(lv_screen_active());
            until([&]
                  { return !resumed->snapshot().active; });
            assert(!platform::audio::music_blocked());
        }
        if (const char* fixture = std::getenv("TRAIL_MATE_ONESEG_METADATA_FIXTURE"))
        {
            // Own this fixture output directory; preserve the recorded receiver's
            // saved list while proving that text alone cannot save a station.
            const auto preserved = root / "settings" / "recorded-channels.txt";
            std::filesystem::rename(saved_channels, preserved);
            radio.restore_ok = true;
            radio.present = true;
            rt::OneSegTvSession::Config metadata;
            metadata.decoder = fixture;
            const auto time_control = root / "metadata-time-commands";
            std::ofstream(time_control) << "normal\n";
            metadata.environment = {"TRAIL_MATE_ONESEG_TEST_MODE=metadata-japanese",
                                    "TRAIL_MATE_ONESEG_TEST_TIME_CONTROL=" + time_control.string()};
            radio.synthetic_iq = true;
            auto receiver = std::make_unique<rt::OneSegTvSession>(radio, metadata, []
                                                                  { return true; });
            auto* received = receiver.get();
            rt::OneSegTvApp page(std::move(receiver));
            page.enter(lv_screen_active());
            click("Start");
            until([&]
                  { return received->snapshot().service_text.following_valid; });
            until([&]
                  { return label_contains(lv_screen_active(), "日本語ニュース"); });
            assert(label_contains(lv_screen_active(), "次の番組"));
            assert(!received->snapshot().media.video_frames && !received->snapshot().presented_frames);
            assert(!std::filesystem::exists(saved_channels));
            click("Menu");
            click("Programme info");
            until([&]
                  { return label_contains(lv_screen_active(), "現在の番組説明です。"); });
            assert(label_contains(lv_screen_active(), "次の番組説明です。"));
            assert(label_contains(lv_screen_active(), "1993-10-13 12:45:00 JST"));
            assert(label_contains(lv_screen_active(), "Scheduled duration: 01:45:30"));
            assert(label_contains(lv_screen_active(), "Scheduled duration: Not supplied"));
            assert(label_contains(lv_screen_active(), "Scheduled end: 1993-10-13 14:30:30 JST"));
            assert(label_contains(lv_screen_active(), "Broadcast status: Running"));
            assert(label_contains(lv_screen_active(), "Broadcast status: Not running"));
            std::ofstream(time_control, std::ios::app) << "undefined\n";
            until([&]
                  { return label_contains(lv_screen_active(), "Scheduled start: Not supplied"); });
            assert(!label_contains(lv_screen_active(), "1970-01-01"));
            std::ofstream(time_control, std::ios::app) << "invalid\n";
            until([&]
                  { return label_contains(lv_screen_active(), "Invalid broadcast time") && label_contains(lv_screen_active(), "Invalid broadcast duration"); });
            std::ofstream(time_control, std::ios::app) << "paused\n";
            until([&]
                  { return label_contains(lv_screen_active(), "Broadcast status: Paused") && label_contains(lv_screen_active(), "Broadcast status: Starting soon"); });
            assert(received->snapshot().active); // Broadcaster pause never stops reception.
            std::ofstream(time_control, std::ios::app) << "normal\n";
            until([&]
                  { return label_contains(lv_screen_active(), "Broadcast status: Running"); });
            capture(host, root, "tv-programme-info.bmp", "TEST FIXTURE - SYNTHETIC JAPANESE SI - NO RF / NO VIDEO / NO AUDIO");
            auto* close = label(lv_screen_active(), "Close info");
            assert(close);
            auto* info_panel = lv_obj_get_parent(lv_obj_get_parent(close));
            auto* info_body = lv_obj_get_child(info_panel, 1);
            assert(lv_obj_get_scroll_bottom(info_body) > 0);
            uint32_t key = LV_KEY_DOWN;
            lv_obj_send_event(lv_obj_get_parent(close), LV_EVENT_KEY, &key);
            assert(lv_obj_get_scroll_y(info_body) > 0);
            lv_obj_scroll_to_y(info_body, lv_obj_get_scroll_y(info_body) + lv_obj_get_scroll_bottom(info_body), LV_ANIM_OFF);
            capture(host, root, "tv-programme-info-scrolled.bmp", "TEST FIXTURE - SYNTHETIC JAPANESE SI - NO RF / NO VIDEO / NO AUDIO");
            key = LV_KEY_ESC;
            lv_obj_send_event(lv_obj_get_parent(close), LV_EVENT_KEY, &key);
            assert(lv_obj_has_flag(lv_obj_get_parent(lv_obj_get_parent(close)), LV_OBJ_FLAG_HIDDEN));
            click("Stop");
            until([&]
                  { return !received->snapshot().active; });
            click("Menu");
            click("Programme info");
            until([&]
                  { return !label_contains(lv_screen_active(), "日本語ニュース"); });
            click("Close info");
            assert(!std::filesystem::exists(saved_channels));
            page.exit(lv_screen_active());
            std::filesystem::rename(preserved, saved_channels);
        }
        if (const char* fixture = std::getenv("TRAIL_MATE_TEST_TRANSPORT_DECODER"))
        {
            const char* inputs = std::getenv("TRAIL_MATE_TEST_TRANSPORT_FIXTURES");
            assert(inputs);
            std::ofstream(std::filesystem::path(inputs) / "phase", std::ios::app) << "a\n";
            const auto preserved = root / "settings" / "recorded-channels.txt";
            std::filesystem::rename(saved_channels, preserved);
            radio.restore_ok = true;
            radio.present = true;
            rt::OneSegTvSession::Config config;
            config.decoder = fixture;
            config.environment = {std::string("TRAIL_MATE_ONESEG_TRANSPORT_FIXTURES=") + inputs};
            radio.synthetic_iq = true;
            auto receiver = std::make_unique<rt::OneSegTvSession>(radio, config, []
                                                                  { return true; });
            auto* received = receiver.get();
            rt::OneSegTvApp page(std::move(receiver));
            page.enter(lv_screen_active());
            click("Start");
            const auto pixel = [&]() -> uint16_t
            {
                auto* object = picture(lv_screen_active());
                if (!object || !lv_obj_is_visible(object)) return 0;
                const auto* source = static_cast<const lv_image_dsc_t*>(lv_image_get_src(object));
                if (!source || source->data_size < 2) return 0;
                return uint16_t(source->data[0]) | (uint16_t(source->data[1]) << 8);
            };
            until([&]
                  { return received->snapshot().decoder.programmes.selected_program == 8 && (pixel() >> 11) > 20; });
            until([&]
                  { return received->snapshot().decoder.programmes.program_count == 2; });
            const auto epoch = received->snapshot().media_epoch;
            const auto acquired = radio.acquired.load();
            assert(!lv_obj_has_state(lv_obj_get_parent(label(lv_screen_active(), "Next service")), LV_STATE_DISABLED));
            click("Next service");
            until([&]
                  { return received->snapshot().decoder.programmes.selected_program == 9 &&
                           received->snapshot().media_epoch > epoch && (pixel() & 31) > 20 && (pixel() >> 11) < 10; });
            assert(radio.acquired == acquired); // Demux selection, not an RF restart.
            capture(host, root, "tv-second-programme.bmp", "TEST FIXTURE - SYNTHETIC TWO-PROGRAMME TS - NO RF / DUMMY AUDIO");
            click("Stop");
            until([&]
                  { return !received->snapshot().active; });
            page.exit(lv_screen_active());
            assert(std::filesystem::remove(saved_channels));
            std::filesystem::rename(preserved, saved_channels);
        }
        facade.shutdown();
    }
    lv_deinit();
}
