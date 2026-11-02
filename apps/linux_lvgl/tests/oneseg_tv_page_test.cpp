#include "app/linux_app_facade.h"
#include "platform/audio/audio_focus.h"
#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/linux/oneseg_channel_store.h"
#include "platform/linux/oneseg_ofdm_controls.h"
#include "platform/linux/oneseg_ofdm_profiles.h"
#include "platform/linux/packet_radio.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/bundled_font_root.h"
#include "ui/localization.h"
#include "ui/oneseg_ofdm_workbench.h"
#include "ui/oneseg_tv_app.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <sstream>
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
    std::atomic_bool pause_iq{false}; // Explicit outage injection, independent of fixture duration.
    std::atomic_uint acquired{0}, restored{0};
    std::atomic_uint sample_rate{0}, spi_clock{0}, gain_step{0};
    std::atomic_uint64_t scanned_channels{0};
    std::string path;
    uint32_t pacing_rate = 576000;
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
        sample_rate = request.reception.sample_rate_hz;
        spi_clock = request.reception.spi_clock_hz;
        gain_step = request.reception.gain_step;
        result.nominal_rate_hz = pacing_rate;
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
            while (pause_iq && !request.cancelled(request.context)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (request.cancelled(request.context)) break;
            const size_t bytes = static_cast<size_t>(input.gcount());
            if (!request.accept(request.context, request.staging, bytes, result.validated_bytes / 2)) break;
            result.validated_bytes += bytes;
            std::this_thread::sleep_for(std::chrono::microseconds(bytes * 1000000ULL / (2 * pacing_rate)));
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
    if (lv_obj_check_type(root, &lv_dropdown_class) &&
        (!std::strncmp(lv_dropdown_get_options(root), "CH ", 3) || !std::strncmp(lv_dropdown_get_options(root), "No saved channels", 17))) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* value = channel_selector(lv_obj_get_child(root, i))) return value;
    return nullptr;
}
static lv_obj_t* tuning_choice(lv_obj_t* root, const char* caption)
{
    const char* marker = std::strcmp(caption, "IQ sample rate") == 0 ? "450 kpair/s" : std::strcmp(caption, "SPI clock request") == 0 ? "8.35 MHz"
                                                                                                                                      : "Gain 13";
    if (lv_obj_check_type(root, &lv_dropdown_class) && std::strstr(lv_dropdown_get_options(root), marker)) return root;
    for (uint32_t index = 0; index < lv_obj_get_child_count(root); ++index)
        if (auto* value = tuning_choice(lv_obj_get_child(root, index), caption)) return value;
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
    if (std::getenv("TRAIL_MATE_TEST_COMPARE_REDRAW"))
    {
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(nullptr);
        host.present();
        const auto full_path = root / (std::string(name) + "-full.bmp");
        host.saveScreenshot(full_path.string());
        std::ifstream partial(root / name, std::ios::binary), full(full_path, std::ios::binary);
        const std::string partial_bytes{std::istreambuf_iterator<char>(partial), std::istreambuf_iterator<char>()};
        const std::string full_bytes{std::istreambuf_iterator<char>(full), std::istreambuf_iterator<char>()};
        assert(!partial_bytes.empty() && partial_bytes == full_bytes);
    }
    lv_obj_delete(warning);
}
static void workbench_redraw_fixture(const std::filesystem::path& root)
{
    platform::desktop::DeviceOptions options;
    options.ux_pack = "t_display_p4_touch";
    options.width = 1232;
    options.height = 568;
    platform::desktop::SdlLvglHost host(options);
    auto* parent = lv_obj_create(lv_screen_active());
    lv_obj_set_size(parent, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    auto* group = lv_group_create();
    {
        rt::OneSegOfdmExperiment applied;
        unsigned applies = 0;
        rt::OneSegOfdmWorkbench::Callbacks callbacks;
        callbacks.apply = [&](const auto&, const auto& value)
        { applied = value; ++applies; };
        rt::OneSegOfdmWorkbench workbench(parent, group, callbacks);
        auto* video = lv_obj_create(workbench.videoHost());
        lv_obj_set_size(video, 320, 240);
        rt::OneSegTvSession::Snapshot state;
        state.active = true;
        for (unsigned i = 1; i < 100; ++i)
        {
            rt::OneSegPhaseObservation observation;
            observation.symbol = i;
            observation.prior = i * .01;
            observation.observed = observation.prior + .05 * std::sin(i * .25);
            observation.corrected = observation.prior + .6 * (observation.observed - observation.prior);
            observation.correction = observation.corrected - observation.prior;
            observation.residual = observation.observed - observation.corrected;
            assert(state.decoder.ofdm_phase.append(observation, rt::OneSegLabSource::Synthetic, 0));
        }
        for (unsigned round = 0; round < 3; ++round)
            for (unsigned stage = 0; stage < 6; ++stage)
            {
                rt::OneSegOfdmExperiment experiment;
                experiment.digital_gain = 1 + round * .2;
                experiment.iq_phase_degrees = round * 5;
                workbench.setWorking({}, experiment);
                workbench.selectStage(static_cast<rt::OneSegOfdmStage>(stage));
                for (unsigned tick = 0; tick < 50; ++tick)
                {
                    lv_obj_set_style_bg_color(video, lv_color_hex(tick % 2 ? 0x204040 : 0x406060), 0);
                    workbench.update(state);
                    pump();
                }
                const auto name = "redraw-" + std::to_string(round) + "-" + std::to_string(stage) + ".bmp";
                capture(host, root, name.c_str(), "SYNTHETIC RENDER FIXTURE / NO RF");
            }
        const auto input = [&](const char* caption)
        {
            auto* heading = label(lv_screen_active(), caption);
            assert(heading);
            auto* cell = lv_obj_get_parent(heading);
            for (uint32_t i = 0; i < lv_obj_get_child_count(cell); ++i)
            {
                auto* object = lv_obj_get_child(cell, i);
                if (lv_obj_check_type(object, &lv_spinbox_class) || lv_obj_check_type(object, &lv_dropdown_class)) return object;
            }
            assert(false);
            return static_cast<lv_obj_t*>(nullptr);
        };
        unsigned lesson_index = 0;
        for (const auto& control : rt::oneseg_ofdm_controls())
        {
            if (control.synthetic_only) continue;
            rt::OneSegOfdmExperiment experiment;
            experiment.automatic_waveform = false;
            workbench.setWorking({}, experiment);
            workbench.selectStage(control.stage);
            auto* object = input(control.label);
            assert(!lv_obj_has_state(object, LV_STATE_DISABLED));
            lv_group_focus_obj(object);
            assert(label_contains(lv_screen_active(), "PREVIEW"));
            for (unsigned tick = 0; tick < 10; ++tick)
            {
                workbench.update(state);
                pump();
            }
            if (control.real == &rt::OneSegOfdmExperiment::sample_clock_ppm || control.real == &rt::OneSegOfdmExperiment::timing_bandwidth)
                assert(label_contains(lv_screen_active(), "Clock loop: symbol / ppm"));
            lv_obj_update_layout(parent);
            auto* graph = lv_obj_get_parent(label(lv_screen_active(), "No measured samples"));
            lv_area_t graph_area{}, panel_area{};
            lv_obj_get_coords(graph, &graph_area);
            lv_obj_get_coords(lv_obj_get_parent(graph), &panel_area);
            assert(graph_area.y2 <= panel_area.y2);
            if (control.real == &rt::OneSegOfdmExperiment::digital_gain)
            {
                lv_spinbox_set_value(object, 20000);
                lv_obj_send_event(object, LV_EVENT_VALUE_CHANGED, nullptr);
                assert(label_contains(lv_screen_active(), "+6.02 dB"));
                click("Apply experiment");
                assert(applied.digital_gain == 2 && applies == 1);
            }
            const auto name = "lesson-" + std::to_string(lesson_index++) + ".bmp";
            capture(host, root, name.c_str(), "SYNTHETIC RENDER FIXTURE / WORKING MATH PREVIEW");
        }
        rt::OneSegOfdmExperiment manual;
        manual.automatic_waveform = false;
        manual.time_interleaving = 16; // Invalid for Mode3; UI must retain/show it.
        workbench.setWorking({}, manual);
        workbench.selectStage(rt::OneSegOfdmStage::Decoder);
        auto* depth = input("Time interleaving");
        assert(std::strstr(lv_dropdown_get_options(depth), "Invalid: 16"));
        const auto before = applies;
        click("Apply experiment");
        assert(applies == before);
        lv_dropdown_set_selected(depth, 3); // Valid Mode3 depth4.
        lv_obj_send_event(depth, LV_EVENT_VALUE_CHANGED, nullptr);
        click("Apply experiment");
        assert(applies == before + 1 && applied.time_interleaving == 4);
        workbench.selectStage(rt::OneSegOfdmStage::Symbol);
        auto* automatic = input("Automatic waveform");
        lv_dropdown_set_selected(automatic, 1);
        lv_obj_send_event(automatic, LV_EVENT_VALUE_CHANGED, nullptr);
        assert(lv_obj_has_state(input("Transmission mode"), LV_STATE_DISABLED));
        assert(lv_obj_has_state(input("Guard denominator"), LV_STATE_DISABLED));
        state.decoder.ofdm_phase = {};
        state.decoder.native_configuration.available = true;
        state.decoder.native_configuration.source = rt::OneSegLabSource::Synthetic;
        state.decoder.native_configuration.mode = 3;
        state.decoder.native_configuration.fft_length = 1024;
        state.decoder.native_configuration.cp_length = 64;
        state.decoder.native_configuration.carriers = 432;
        state.decoder.native_configuration.sample_rate = 64000000. / 63;
        state.decoder.native_configuration.alpha = .2;
        state.decoder.native_configuration.beta = .1;
        state.decoder.native_configuration.integer_search = 10;
        state.decoder.native_configuration.offset_confirmation = 4;
        state.decoder.native_configuration.channel_average = 1;
        assert(state.decoder.native_configuration.valid());
        for (unsigned i = 1; i < 100; ++i)
        {
            rt::OneSegPhaseObservation observation;
            observation.symbol = i;
            observation.has_prediction = true;
            observation.prior = i * .01;
            observation.integrator = .005;
            observation.prediction = observation.prior + observation.integrator;
            observation.observed = observation.prior + .03 * std::sin(i * .3);
            observation.alpha = .2;
            observation.beta = .1;
            observation.corrected = observation.prediction + .3 * (observation.observed - observation.prior);
            observation.correction = observation.corrected - observation.prior;
            observation.residual = observation.observed - observation.corrected;
            assert(state.decoder.ofdm_phase.append(observation, rt::OneSegLabSource::Synthetic, 0));
        }
        workbench.selectStage(rt::OneSegOfdmStage::Tracking);
        lv_group_focus_obj(input("Carrier BW"));
        workbench.update(state);
        assert(label_contains(lv_screen_active(), "ACTUAL native alpha=0.2"));
        assert(label_contains(lv_screen_active(), "RMS forecast"));
        assert(label(lv_screen_active(), "prediction"));
        capture(host, root, "forecast-v2.bmp", "SYNTHETIC STATE FIXTURE / FORECAST AND READBACK / NO RF");
    }
    lv_obj_delete(parent);
    lv_group_delete(group);
}
static lv_obj_t* dropdown_with(lv_obj_t* root, const char* marker)
{
    if (lv_obj_check_type(root, &lv_dropdown_class) && std::strstr(lv_dropdown_get_options(root), marker)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* found = dropdown_with(lv_obj_get_child(root, i), marker)) return found;
    return nullptr;
}
static void iq_workflow(platform::desktop::SdlLvglHost& host, const std::filesystem::path& root,
                        rt::OneSegTvSession::Config config, RecordedRadio& radio, const char* input)
{
    const auto saved = root / "settings" / "oneseg-channels.txt";
    const auto preserved = root / "settings" / "before-iq-workflow.txt";
    const bool had_saved = std::filesystem::exists(saved);
    if (had_saved) std::filesystem::rename(saved, preserved);
    radio.synthetic_iq = false;
    radio.present = true;
    radio.restore_ok = true;
    radio.path = input;
    radio.pacing_rate = 450000;
    config.reception_tuning = config.ofdm_experiments = true;
    config.diagnostic_source = rt::OneSegLabSource::RecordedIq;
    {
        auto session = std::make_unique<rt::OneSegTvSession>(radio, config, []
                                                             { return true; });
        auto* state = session.get();
        rt::OneSegTvApp page(std::move(session));
        page.enter(lv_screen_active());
        until([&]
              { return !state->snapshot().iq_catalog_loading; });
        auto* sources = dropdown_with(lv_screen_active(), "Live receiver\nRecorded IQ");
        assert(sources && lv_dropdown_get_selected(sources) == 0);
        click("Record IQ");
        until([&]
              { const auto s = state->snapshot(); return s.presented_frames > 0 && s.receive.recorded_bytes >= 450000*2*7; });
        assert(state->snapshot().source.recording_capacity == 16 * 1024 * 1024);
        const auto before_serial = state->snapshot().iq_export_serial;
        click("Save IQ");
        until([&]
              { const auto s = state->snapshot(); return s.iq_export_serial > before_serial && !s.iq_exporting && !s.iq_catalog_loading && dropdown_with(lv_screen_active(), ".tmiq"); });
        const auto stopped = state->snapshot();
        assert(!stopped.active && stopped.iq_export_ok && stopped.receive.recorded_bytes > 0);
        rt::OneSegIqArchive archive;
        std::string error;
        assert(!stopped.iq_archives.empty() && archive.load(stopped.iq_archives.front(), error));
        assert(archive.metadata().rate == 450000 && archive.metadata().gain == 9 && archive.metadata().spi == 8700000);
        const auto acquisitions = radio.acquired.load();
        lv_dropdown_set_selected(sources, 1);
        lv_obj_send_event(sources, LV_EVENT_VALUE_CHANGED, nullptr);
        until([&]
              { return !state->snapshot().iq_catalog_loading && !lv_obj_has_state(dropdown_with(lv_screen_active(), ".tmiq"), LV_STATE_DISABLED); });
        click("Start");
        until([&]
              { return state->snapshot().presented_frames > 0 && state->snapshot().decoder.ofdm_phase.size >= 5; });
        const auto replay = state->snapshot();
        assert(replay.active && replay.receive.recorded_source && replay.input_rate_hz == 450000);
        assert(replay.decoder.ofdm_phase.source == rt::OneSegLabSource::RecordedIq);
        assert(radio.acquired == acquisitions);
        assert(lv_obj_has_state(tuning_choice(lv_screen_active(), "IQ sample rate"), LV_STATE_DISABLED));
        assert(lv_obj_has_state(lv_obj_get_parent(label(lv_screen_active(), "CH +")), LV_STATE_DISABLED));
        capture(host, root, "tv-iq-archive-replay.bmp", "ACTUAL RECORDED IQ WORKFLOW / NO LIVE RF");
        const auto replay_generation = replay.generation;
        click("Default");
        until([&]
              { return state->snapshot().generation > replay_generation && state->snapshot().presented_frames > 0; });
        assert(lv_dropdown_get_selected(sources) == 0);
        assert(state->snapshot().source.archive_path.empty() && radio.acquired > acquisitions);
        assert(radio.sample_rate == 450000 && radio.spi_clock == 8700000 && radio.gain_step == 9);
        const auto live_generation = state->snapshot().generation;
        click("Record IQ");
        until([&]
              { return state->snapshot().generation > live_generation && state->snapshot().receive.recorded_bytes >= 450000 * 2 * 2; });
        const auto export_serial = state->snapshot().iq_export_serial;
        click("Save IQ");
        const auto exit_started = Clock::now();
        page.exit(lv_screen_active());
        assert(Clock::now() - exit_started < std::chrono::milliseconds(100));
        until([&]
              { return state->snapshot().iq_export_serial > export_serial && !state->snapshot().iq_exporting; });
        assert(state->snapshot().iq_export_ok && !state->snapshot().active);
        unsigned saved_count = 0;
        for (const auto& item : std::filesystem::directory_iterator(root / "recordings" / "oneseg"))
            if (item.path().extension() == ".tmiq") ++saved_count;
        assert(saved_count >= 2);
    }
    std::filesystem::remove(saved);
    if (had_saved) std::filesystem::rename(preserved, saved);
    radio.path.clear();
    radio.pacing_rate = 576000;
}
static void synthetic_workflow(platform::desktop::SdlLvglHost& host, const std::filesystem::path& root,
                               rt::OneSegTvSession::Config config, RecordedRadio& radio)
{
    const auto saved = root / "settings" / "oneseg-channels.txt";
    const auto preserved = root / "settings" / "before-synthetic-workflow.txt";
    const bool had_saved = std::filesystem::exists(saved);
    if (had_saved) std::filesystem::rename(saved, preserved);
    radio.present = false;
    const auto acquired = radio.acquired.load();
    config.reception_tuning = config.ofdm_experiments = true;
    {
        auto session = std::make_unique<rt::OneSegTvSession>(radio, config, []
                                                             { return true; });
        auto* state = session.get();
        rt::OneSegTvApp page(std::move(session));
        page.enter(lv_screen_active());
        auto* source = dropdown_with(lv_screen_active(), "Synthetic OFDM");
        assert(source);
        lv_dropdown_set_selected(source, 2);
        lv_obj_send_event(source, LV_EVENT_VALUE_CHANGED, nullptr);
        until([&]
              { return !state->snapshot().iq_catalog_loading && label(lv_screen_active(), "Start"); });
        click("Start");
        until([&]
              { const auto s = state->snapshot(); return s.decoder.ofdm_phase.size >= 8 && s.decoder.ofdm_signal[3].count; });
        auto current = state->snapshot();
        assert(current.source.synthetic && current.active && !current.decoder.ts_bytes && !current.presented_frames);
        assert(current.decoder.ofdm_phase.source == rt::OneSegLabSource::Synthetic && current.decoder.ofdm_phase.newest().has_reference);
        assert(!current.decoder.rejected_ofdm_records && radio.acquired == acquired);
        assert(label_contains(lv_screen_active(), "SYNTHETIC OFDM"));
        click("Subcarriers / OFDM");
        until([&]
              { return label_contains(lv_screen_active(), "ACTUAL / Synthetic"); });
        for (unsigned tick = 0; tick < 40; ++tick) pump();
        capture(host, root, "tv-synthetic-subcarriers.bmp", "ACTUAL GENERIC OFDM FROM GENERATED IQ / NO RF, TV OR FEC");
        const char* stages[]{"Input / RF", "Filter / resample", "Symbol / CP", "Carrier / phase", "Pilots / equalizer", "Constellation / FEC"};
        for (unsigned stage = 0; stage < 6; ++stage)
        {
            click(stages[stage]);
            for (unsigned tick = 0; tick < 50; ++tick) pump();
            for (const auto& control : rt::oneseg_ofdm_controls())
                if (control.stage == static_cast<rt::OneSegOfdmStage>(stage) && !control.synthetic_rf)
                    assert(label(lv_screen_active(), control.label));
            const auto name = "tv-synthetic-stage-" + std::to_string(stage) + ".bmp";
            capture(host, root, name.c_str(), "GENERATED OFDM / KNOWN FRAMING / NO RF, TV OR FEC");
            if (stage == 5)
            {
                auto* view = dropdown_with(lv_screen_active(), "Reference BER");
                assert(view);
                lv_dropdown_set_selected(view, 1);
                lv_obj_send_event(view, LV_EVENT_VALUE_CHANGED, nullptr);
                until([&]
                      { return label_contains(lv_screen_active(), "Known-reference uncoded BER"); });
            }
        }
        auto* heading = label(lv_screen_active(), "SNR [dB]");
        assert(heading);
        lv_obj_t* snr = nullptr;
        auto* cell = lv_obj_get_parent(heading);
        for (uint32_t i = 0; i < lv_obj_get_child_count(cell); ++i)
            if (lv_obj_check_type(lv_obj_get_child(cell, i), &lv_spinbox_class)) snr = lv_obj_get_child(cell, i);
        assert(snr);
        lv_spinbox_set_value(snr, 100); // 10 dB, stored scale10.
        lv_obj_send_event(snr, LV_EVENT_VALUE_CHANGED, nullptr);
        const auto generation = current.generation;
        click("Apply experiment");
        until([&]
              { const auto s = state->snapshot(); return s.generation > generation && s.decoder.ofdm_phase.size >= 8; });
        current = state->snapshot();
        assert(current.decoder.ofdm_phase.newest().synthetic_snr == 10 && radio.acquired == acquired);
        click("Carrier / phase");
        capture(host, root, "tv-synthetic-ofdm.bmp", "SYNTHETIC GENERATED OFDM / KNOWN REFERENCE / NO RF OR TV");
        std::string saved_name;
        const auto directory = root / "settings" / "oneseg-experiments";
        rt::OneSegOfdmProfiles profiles(directory);
        std::vector<std::filesystem::path> existing;
        if (std::filesystem::is_directory(directory))
            for (const auto& item : std::filesystem::directory_iterator(directory)) existing.push_back(item.path());
        const auto before_save_generation = state->snapshot().generation;
        click("Save");
        until([&]
              {
            std::error_code error;
            if (!std::filesystem::is_directory(directory, error)) return false;
            for (const auto& item : std::filesystem::directory_iterator(directory, error))
            {
                if (std::find(existing.begin(), existing.end(), item.path()) != existing.end()) continue;
                if (item.path().extension() != ".profile") continue;
                rt::OneSegOfdmProfile profile;
                if (profiles.load(item.path().stem().string(), profile) && profile.synthetic && profile.experiment.synthetic_snr_db == 10)
                { saved_name = profile.name; return true; }
            }
            return false; });
        until([&]
              { return state->snapshot().generation > before_save_generation && state->snapshot().active && label(lv_screen_active(), "Stop"); });
        click("Stop");
        until([&]
              { return !state->snapshot().active && label(lv_screen_active(), "Start"); });
        click("Load");
        auto* experiments = dropdown_with(lv_screen_active(), saved_name.c_str());
        assert(experiments);
        std::istringstream options(lv_dropdown_get_options(experiments));
        std::string option;
        unsigned option_index = 0;
        while (std::getline(options, option) && option != saved_name) ++option_index;
        assert(option == saved_name);
        lv_dropdown_set_selected(experiments, option_index);
        lv_obj_send_event(experiments, LV_EVENT_VALUE_CHANGED, nullptr);
        assert(lv_dropdown_get_selected(source) == 2 && !state->snapshot().active && lv_spinbox_get_value(snr) == 100);
        click("Default");
        until([&]
              { return !state->snapshot().active && !state->snapshot().source.synthetic; });
        assert(lv_dropdown_get_selected(source) == 0 && radio.acquired == acquired);
        assert(lv_dropdown_get_selected(tuning_choice(lv_screen_active(), "IQ sample rate")) == 0);
        assert(lv_dropdown_get_selected(tuning_choice(lv_screen_active(), "SPI clock request")) == 1);
        assert(lv_dropdown_get_selected(tuning_choice(lv_screen_active(), "Receiver gain step")) == 9);
        page.exit(lv_screen_active());
    }
    std::filesystem::remove(saved);
    if (had_saved) std::filesystem::rename(preserved, saved);
}
static lv_obj_t* carrier_control(const char* caption, const lv_obj_class_t* type)
{
    auto* heading = label(lv_screen_active(), caption);
    assert(heading);
    auto* cell = lv_obj_get_parent(heading);
    for (unsigned i = 0; i < lv_obj_get_child_count(cell); ++i)
    {
        auto* object = lv_obj_get_child(cell, i);
        if (lv_obj_check_type(object, type)) return object;
    }
    assert(false);
    return nullptr;
}
static void carrier_lesson_fixture(const std::filesystem::path& root)
{
    platform::desktop::DeviceOptions options;
    options.ux_pack = "t_display_p4_touch";
    options.width = 1232;
    options.height = 568;
    platform::desktop::SdlLvglHost host(options);
    auto* parent = lv_obj_create(lv_screen_active());
    lv_obj_set_size(parent, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    auto* group = lv_group_create();
    {
        unsigned applies = 0;
        rt::OneSegOfdmWorkbench::Callbacks callbacks;
        callbacks.apply = [&](const auto&, const auto&)
        { ++applies; };
        rt::OneSegOfdmWorkbench view(parent, group, callbacks);
        view.selectStage(rt::OneSegOfdmStage::Subcarriers);
        auto* video = lv_obj_create(view.videoHost());
        lv_obj_set_size(video, 320, 240);
        rt::OneSegTvSession::Snapshot state;
        state.active = true;
        state.decoder.generation = 7;
        state.generation = 7;
        const auto make_burst = [](bool generic, unsigned burst)
        {
            auto result = std::make_shared<rt::OneSegCarrierBurst>();
            result->source = generic ? rt::OneSegLabSource::Synthetic : rt::OneSegLabSource::LiveReceiver;
            result->updated_ms = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
            for (unsigned row = 0; row < 4; ++row)
            {
                auto& frame = result->symbols[row];
                auto& m = frame.metadata;
                m.sequence = (burst - 1) * (generic ? 12 : 512) + row + 1;
                m.burst = burst;
                m.slot = row;
                m.layout = generic;
                m.mode = 3;
                m.fft = generic ? 1024 : 576;
                m.cp = m.fft / 16;
                m.count = 432;
                m.sample_rate = generic ? 64000000. / 63 : 4000000. / 7;
                m.phase = (row + 2) % 4;
                m.flags = 1;
                assert(m.valid());
                for (unsigned k = 0; k < 432; ++k) frame.values[k].values = {.6f, .2f, .75f, .25f, .8f, 0, 1.33333337f, 0};
            }
            return result;
        };
        const auto set_burst = [&](auto burst)
        {
            state.decoder.ofdm_carriers = burst;
            state.decoder.ofdm_phase = {};
            for (const auto& frame : burst->symbols)
            {
                rt::OneSegPhaseObservation p;
                p.symbol = frame.metadata.sequence;
                p.prior = .1;
                p.observed = .2;
                p.integrator = .01;
                p.prediction = .11;
                p.alpha = .1;
                p.beta = .01;
                p.corrected = .121;
                p.correction = .021;
                p.residual = .079;
                p.has_prediction = true;
                assert(state.decoder.ofdm_phase.append(p, burst->source, burst->updated_ms));
            }
        };
        const auto refresh = [&]
        { lv_tick_inc(120); view.update(state); lv_obj_update_layout(parent); lv_refr_now(nullptr); host.present(); };
        const auto choice = [&](const char* name, unsigned value)
        { auto* object = carrier_control(name,&lv_dropdown_class); lv_dropdown_set_selected(object,value); lv_obj_send_event(object,LV_EVENT_VALUE_CHANGED,nullptr); refresh(); };
        const auto spin = [&](const char* name, int value)
        { auto* object = carrier_control(name,&lv_spinbox_class); lv_spinbox_set_value(object,value); lv_obj_send_event(object,LV_EVENT_VALUE_CHANGED,nullptr); refresh(); };
        set_burst(make_burst(false, 2));
        refresh();
        spin("Carrier index", 6);
        assert(label_contains(parent, "ACTUAL / Live"));
        assert(label_contains(parent, "Same output 513"));
        assert(label_contains(parent, "Pilot fit is self-referenced"));
        capture(host, root, "subcarriers-native-pilot.bmp", "UI FIXTURE / COHERENT NATIVE SYMBOLS / NO RF");
        choice("Symbol row", 2);
        assert(label_contains(parent, "Same output 515"));
        choice("Symbol row", 0);
        spin("Carrier index", 7);
        assert(label_contains(parent, "Payload reference unknown"));
        choice("Dataset", 1);
        choice("Lesson", 1);
        spin("CFO model Hz", 250);
        assert(label_contains(parent, "epsilon=0.2520"));
        capture(host, root, "subcarriers-cfo-model.bmp", "MATHEMATICAL FOUR-TONE PREVIEW / NO RF");
        choice("Lesson", 2);
        spin("Delay / shift", 100);
        spin("Echo % amp", 60);
        assert(label_contains(parent, "CP violated"));
        capture(host, root, "subcarriers-cp-violation.bmp", "LINEAR ECHO + ADJACENT SYMBOL MODEL / NO RF");
        choice("Lesson", 3);
        spin("Delay / shift", -18);
        assert(!label_contains(parent, "CP violated"));
        spin("Delay / shift", 18);
        assert(label_contains(parent, "CP violated"));
        choice("FFT / mode", 3);
        assert(label_contains(parent, "receiver FFT 1024"));
        choice("Dataset", 0);
        click("Hold");
        refresh();
        set_burst(make_burst(false, 3));
        refresh();
        assert(label_contains(parent, "Same output 513"));
        click("Hold");
        refresh();
        assert(label_contains(parent, "Same output 1025"));
        state.decoder.generation = 8;
        state.generation = 8;
        state.source.synthetic = true;
        set_burst(make_burst(true, 2));
        refresh();
        spin("Carrier index", 216);
        assert(label_contains(parent, "ACTUAL / Synthetic"));
        assert(label_contains(parent, "signed k=1"));
        capture(host, root, "subcarriers-generic-reference.bmp", "GENERIC KNOWN-REFERENCE OFDM UI FIXTURE / NO RF");
        state.source.synthetic = false;
        state.source.archive_path = "fixture-A.tmiq";
        state.generation = state.decoder.generation = 9;
        auto recorded = make_burst(false, 2);
        recorded->source = rt::OneSegLabSource::RecordedIq;
        set_burst(recorded);
        refresh();
        assert(label_contains(parent, "ACTUAL / Recorded IQ"));
        click("Hold");
        refresh();
        state.source.archive_path = "fixture-B.tmiq";
        refresh();
        assert(label_contains(parent, "no observed burst"));
        state.generation = state.decoder.generation = 10;
        set_burst(recorded);
        refresh();
        assert(label_contains(parent, "ACTUAL / Recorded IQ"));
        view.fullscreen(true);
        refresh();
        assert(lv_obj_get_width(video) == 320 && lv_obj_get_height(video) == 240);
        view.fullscreen(false);
        refresh();
        state.decoder.generation = 123; // Receive-worker and outer session counters need not match.
        refresh();
        assert(label_contains(parent, "ACTUAL / Recorded IQ"));
        for (unsigned i = 0; i < 6; ++i)
        {
            view.selectStage(static_cast<rt::OneSegOfdmStage>(i));
            refresh();
        }
        view.selectStage(rt::OneSegOfdmStage::Subcarriers);
        refresh();
        assert(label_contains(parent, "ACTUAL / Recorded IQ"));
        assert(applies == 0); // Teaching controls never apply or retune the receiver.
    }
    lv_group_delete(group);
    lv_obj_delete(parent);
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
    if (std::getenv("TRAIL_MATE_TEST_CARRIER_UI"))
    {
        carrier_lesson_fixture(root);
        lv_deinit();
        return 0;
    }
    if (std::getenv("TRAIL_MATE_TEST_REDRAW_ONLY"))
    {
        workbench_redraw_fixture(root);
        lv_deinit();
        return 0;
    }
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
        const bool synthetic_only = std::getenv("TRAIL_MATE_TEST_SYNTHETIC_ONLY");
        std::string python, libraries;
        if (!synthetic_only)
            for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[2]))
            {
                const auto name = entry.path().filename().string();
                if (entry.is_directory() && (name == "site-packages" || name == "dist-packages")) python += entry.path().string() + ":";
                if (entry.is_regular_file() && name.find("libgnuradio-isdbt.so") == 0) libraries += entry.path().parent_path().string() + ":";
            }
        if (const char* extra = std::getenv("TRAIL_MATE_TEST_RUNTIME_LIBRARY_PATH")) libraries += std::string(extra) + ":";
        config.environment = {"PYTHONPATH=" + python, "LD_LIBRARY_PATH=" + libraries};
        const bool measured_tracking = std::getenv("TRAIL_MATE_TEST_OFDM_TRACKING");
        if (measured_tracking)
        {
            config.environment.push_back("TRAIL_MATE_OFDM_DIAGNOSTICS=1");
            config.diagnostic_source = rt::OneSegLabSource::RecordedIq;
        }
        RecordedRadio radio;
        radio.path = argv[3];
        if (!synthetic_only)
        {
            auto session = std::make_unique<rt::OneSegTvSession>(radio, config, []
                                                                 { return true; });
            auto* state = session.get();
            {
                rt::OneSegTvApp page(std::move(session));
                page.enter(lv_screen_active());
                assert(!label(lv_screen_active(), "Tune"));
                assert(label(lv_screen_active(), "Carrier / phase"));
                assert(label(lv_screen_active(), "Apply experiment"));
                assert(label(lv_screen_active(), "No native tracking observations"));
                assert(lv_dropdown_get_selected(tuning_choice(lv_screen_active(), "IQ sample rate")) == 0);
                capture(host, root, "tv-ofdm-workbench-idle.bmp", "TEST FIXTURE - NATIVE WORKBENCH / NO RF");
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
                if (measured_tracking)
                {
                    until([&]
                          { return state->snapshot().decoder.ofdm_signal[3].count >= 32; });
                    const auto measurements = state->snapshot().decoder;
                    for (const auto& signal : measurements.ofdm_signal)
                        assert(signal.valid() && signal.source == rt::OneSegLabSource::RecordedIq);
                    assert(measurements.rejected_ofdm_records == 0);
                    if (std::getenv("TRAIL_MATE_TEST_CARRIERS"))
                    {
                        until([&]
                              { return bool(state->snapshot().decoder.ofdm_carriers); });
                        click("Subcarriers / OFDM");
                        until([&]
                              { return label_contains(lv_screen_active(), "ACTUAL / Recorded IQ"); });
                        for (unsigned tick = 0; tick < 40; ++tick) pump();
                        capture(host, root, "tv-native-subcarriers.bmp", "COHERENT ACTUAL NATIVE CARRIERS FROM RECORDED IQ / NO RF");
                    }
                    const char* stages[]{"Input / RF", "Filter / resample", "Symbol / CP", "Carrier / phase", "Pilots / equalizer", "Constellation / FEC"};
                    for (size_t i = 0; i < 6; ++i)
                    {
                        click(stages[i]);
                        assert(label_contains(lv_screen_active(), rt::oneseg_ofdm_equation(static_cast<rt::OneSegOfdmStage>(i))));
                        assert(label_contains(lv_screen_active(), rt::oneseg_ofdm_explanation(static_cast<rt::OneSegOfdmStage>(i))));
                        const char* views[]{"Post-filter Hz", "Post-filter Hz", "CP offset", "Equalized I/Q", "Channel carrier", "Equalized I/Q"};
                        until([&]
                              { return label_contains(lv_screen_active(), views[i]); });
                        for (unsigned j = 0; j < 50; ++j) pump();
                        capture(host, root, ("tv-measured-stage-" + std::to_string(i) + ".bmp").c_str(), "RECORDED IQ - ACTUAL RECEIVER DIAGNOSTICS / NO RF");
                        if (i == 4 || i == 5)
                        {
                            auto* view = dropdown_with(lv_screen_active(), i == 4 ? "H phase" : "RS words");
                            assert(view);
                            lv_dropdown_set_selected(view, 1);
                            lv_obj_send_event(view, LV_EVENT_VALUE_CHANGED, nullptr);
                            until([&]
                                  { return label_contains(lv_screen_active(), i == 4 ? "Channel unwrapped phase" : "RS uncorrectable-word fraction"); });
                            capture(host, root, i == 4 ? "tv-native-channel-phase.bmp" : "tv-native-rs-outcomes.bmp", "ACTUAL NATIVE DIAGNOSTICS FROM RECORDED RF / NO LIVE RF");
                        }
                    }
                    click("Carrier / phase");
                }
                if (state->snapshot().timing != rt::OneSegTvSession::Timing::AudioQueueEstimate)
                {
                    const auto s = state->snapshot();
                    std::fprintf(stderr, "ARM/UI timing diagnostic: timing=%u active=%d iq_pairs=%llu queue=%u ts=%llu frames=%llu video_age=%u pcm=%llu audio_valid=%d audio_starved=%d\n", unsigned(s.timing), s.active, (unsigned long long)s.receive.forwarded_pairs, s.receive.queue_high_water, (unsigned long long)s.decoder.ts_bytes, (unsigned long long)s.presented_frames, s.video_age_ms, (unsigned long long)s.media.pcm_samples, s.audio_clock.valid, s.audio_clock.starved);
                }
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
                radio.pause_iq = true;
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
                radio.pause_iq = false;
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
                metadata.reception_tuning = true;
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
                const auto tuning_generation = received->snapshot().generation;
                auto* rate = tuning_choice(lv_screen_active(), "IQ sample rate");
                auto* clock = tuning_choice(lv_screen_active(), "SPI clock request");
                auto* gain = tuning_choice(lv_screen_active(), "Receiver gain step / 0 = chip AGC");
                lv_dropdown_set_selected(rate, 1);
                lv_dropdown_set_selected(clock, 2);
                lv_dropdown_set_selected(gain, 13);
                lv_obj_send_event(rate, LV_EVENT_VALUE_CHANGED, nullptr);
                click("Apply experiment");
                until([&]
                      { return received->snapshot().generation > tuning_generation && radio.sample_rate == 500000 &&
                               radio.spi_clock == 16000000 && radio.gain_step == 13 && received->snapshot().service_text.following_valid; });
                assert(received->snapshot().reception.sample_rate_hz == 500000);
                capture(host, root, "tv-ofdm-workbench.bmp", "TEST FIXTURE - NATIVE WORKBENCH / SYNTHETIC METADATA - NO RF");
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
                {
                    rt::OneSegTvApp restored_page(std::make_unique<rt::OneSegTvSession>(radio, metadata, []
                                                                                        { return true; }));
                    restored_page.enter(lv_screen_active());
                    assert(lv_dropdown_get_selected(tuning_choice(lv_screen_active(), "IQ sample rate")) == 0);
                    assert(lv_dropdown_get_selected(tuning_choice(lv_screen_active(), "SPI clock request")) == 1);
                    assert(lv_dropdown_get_selected(tuning_choice(lv_screen_active(), "Receiver gain step / 0 = chip AGC")) == 9);
                    restored_page.exit(lv_screen_active());
                }
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
            if (const char* recorded450 = std::getenv("TRAIL_MATE_TEST_450_IQ"))
                iq_workflow(host, root, config, radio, recorded450);
        }
        synthetic_workflow(host, root, config, radio);
        facade.shutdown();
    }
    lv_deinit();
}
