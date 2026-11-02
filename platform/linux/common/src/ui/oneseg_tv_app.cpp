#include "ui/oneseg_tv_app.h"
#include "platform/linux/oneseg_channel_name.h"
#include "platform/linux/oneseg_channel_store.h"
#include "platform/linux/oneseg_event_schedule.h"
#include "platform/linux/oneseg_ofdm_profiles.h"
#include "platform/linux/oneseg_session_scan_port.h"
#include "platform/linux/runtime_paths.h"
#include "platform/ui/screen_runtime.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/input/pager_navigation.h"
#include "ui/oneseg_ofdm_workbench.h"
#include "ui/ui_theme.h"
#include "ui/widgets/top_bar.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace platform::linux_runtime
{
struct OneSegTvApp::Impl
{
    std::unique_ptr<OneSegTvSession> session;
    OneSegChannelStore saved{resolve_paths().settings_root / "settings" / "oneseg-channels.txt"};
    std::unique_ptr<OneSegSessionScanPort> scan_port;
    std::unique_ptr<OneSegScanController> scanner;
    bool scan_pending = false, resume_after_scan = false;
    unsigned scan_volume = 70, resume_channel = 13, resume_service = 0;
    std::string notice;
    unsigned pending_channel = 0, desired_service = 0;
    uint64_t save_attempt_generation = 0;
    unsigned save_attempt_service = 0;
    OneSegReceptionSettings reception;
    OneSegOfdmExperiment experiment;
    OneSegReceiveWorker::Source iq_source;
    bool recorded_selected = false, iq_catalog_pending = true;
    bool offlineSource() const { return recorded_selected || iq_source.synthetic; }
    unsigned live_channel = 13;
    uint64_t iq_export_serial = 0;
    uint64_t iq_catalog_serial = 0;
    std::filesystem::path iq_directory = resolve_paths().sd_root / "recordings" / "oneseg";
    std::vector<std::filesystem::path> iq_archives;
    lv_obj_t *source_bar = nullptr, *source_selector = nullptr, *archive_selector = nullptr,
             *record_button = nullptr, *record_caption = nullptr, *iq_save_button = nullptr, *iq_count = nullptr, *iq_loop = nullptr;
    std::unique_ptr<OneSegOfdmWorkbench> workbench;
    const std::filesystem::path profile_directory = resolve_paths().settings_root / "settings" / "oneseg-experiments";
    OneSegOfdmProfiles profiles{profile_directory};
    OneSegOfdmProfile queued_profile;
    bool profile_save_pending = false;
    lv_obj_t* experiment_list = nullptr;
    std::vector<std::string> experiment_ids;
    std::string save_attempt_name;
    lv_obj_t* broadcast = nullptr;
    std::string broadcast_text;
    lv_obj_t *details_panel = nullptr, *details_body = nullptr, *details_label = nullptr,
             *details_close = nullptr, *details_button = nullptr;
    std::string details_text;
    lv_obj_t *root = nullptr, *title = nullptr, *status = nullptr, *viewing = nullptr, *video = nullptr, *image = nullptr,
             *waiting = nullptr, *controls = nullptr, *channel_label = nullptr, *start_button = nullptr,
             *start_label = nullptr, *volume_label = nullptr;
    lv_obj_t *channel_down = nullptr, *channel_up = nullptr, *programme = nullptr;
    lv_obj_t *station_down = nullptr, *station_up = nullptr;
    lv_obj_t* retry_button = nullptr;
    lv_obj_t *menu_panel = nullptr, *channel_list = nullptr, *scan_button = nullptr, *scan_label = nullptr;
    lv_timer_t* timer = nullptr;
    ::ui::widgets::TopBar bar{};
    lv_image_dsc_t descriptor{};
    std::vector<uint8_t> pixels;
    unsigned channel = 13;
    bool fullscreen = false;
    bool retry_pending = false;
    uint64_t drawn_generation = 0, drawn_epoch = 0, drawn_ordinal = 0;
    uint32_t status_tick = 0;
    uint64_t status_generation = 0;
    unsigned status_programme = 0;
    const char *status_phase = nullptr, *status_timing = nullptr;
    std::string status_message, status_notice;
    bool status_initialized = false;
    static void setText(lv_obj_t* label, const char* value)
    {
        if (std::strcmp(lv_label_get_text(label), value) != 0) lv_label_set_text(label, value);
    }
    explicit Impl(std::unique_ptr<OneSegTvSession> value) : session(std::move(value))
    {
        saved.load(notice);
        if (const auto* last = saved.lastChannel()) channel = last->channel;
        reception = oneseg_factory_profile().reception;
    }
    static void hidden(lv_obj_t* object, bool value)
    {
        if (!object) return;
        if (value) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
    static lv_obj_t* text(lv_obj_t* parent, const char* value)
    {
        auto* label = lv_label_create(parent);
        lv_label_set_text(label, value);
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        return label;
    }
    static std::string field(bool valid, const OneSegAribTextResult& value)
    {
        if (!valid) return "Not received";
        if (!value) return "Unsupported or invalid broadcast text";
        return value.text.empty() ? "Not supplied" : value.text;
    }
    static std::string schedule(const OneSegServiceInformation::Event& event)
    {
        const auto value = oneseg_event_schedule(event);
        using State = OneSegEventSchedule::State;
        std::string result = "Scheduled start: ";
        char buffer[96];
        if (value.start == State::Known)
        {
            const std::chrono::year_month_day date{std::chrono::sys_days{std::chrono::days{int(value.start_mjd) - 40587}}};
            std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u %02u:%02u:%02u JST", int(date.year()), unsigned(date.month()), unsigned(date.day()),
                          value.start_jst_seconds / 3600, value.start_jst_seconds / 60 % 60, value.start_jst_seconds % 60);
            result += buffer;
        }
        else result += value.start == State::Undefined ? "Not supplied" : "Invalid broadcast time";
        result += "\nScheduled duration: ";
        if (value.duration == State::Known)
        {
            std::snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u", value.duration_seconds / 3600,
                          value.duration_seconds / 60 % 60, value.duration_seconds % 60);
            result += buffer;
        }
        else result += value.duration == State::Undefined ? "Not supplied" : "Invalid broadcast duration";
        result += "\nScheduled end: ";
        if (const auto end = value.scheduledEndUtc())
        {
            const auto local = std::chrono::sys_seconds{std::chrono::seconds{*end + 9 * 3600}};
            const auto day = std::chrono::floor<std::chrono::days>(local);
            const std::chrono::year_month_day date{day};
            const std::chrono::hh_mm_ss clock{local - day};
            std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u %02u:%02u:%02u JST", int(date.year()), unsigned(date.month()), unsigned(date.day()),
                          unsigned(clock.hours().count()), unsigned(clock.minutes().count()), unsigned(clock.seconds().count()));
            result += buffer;
        }
        else result += value.start == State::Invalid || value.duration == State::Invalid ? "Unavailable (invalid schedule)" : "Not supplied";
        return result;
    }
    static const char* broadcastStatus(uint8_t value)
    {
        switch (value)
        {
        case 0:
            return "Not specified";
        case 1:
            return "Not running";
        case 2:
            return "Starting soon";
        case 3:
            return "Paused";
        case 4:
            return "Running";
        default:
            return "Reserved value";
        }
    }
    void toggleDetails()
    {
        if (scanning()) return;
        const bool opening = lv_obj_has_flag(details_panel, LV_OBJ_FLAG_HIDDEN);
        hidden(menu_panel, true);
        hidden(details_panel, !opening);
        details_text.clear();
        lv_group_focus_obj(opening ? details_close : start_button);
        refresh();
    }
    void refreshDetails(const OneSegTvSession::Snapshot& state)
    {
        if (!details_panel || lv_obj_has_flag(details_panel, LV_OBJ_FLAG_HIDDEN)) return;
        const auto& info = state.service_text;
        const auto& raw = state.decoder.service_information;
        std::string value = "Station: " + field(info.service_valid, info.name) +
                            "\nProvider: " + field(info.service_valid, info.provider);
        const auto append = [&](const char* heading, bool valid, const OneSegAribTextResult& title,
                                const OneSegAribTextResult& description, const OneSegServiceInformation::Event& event)
        {
            value += std::string("\n\n") + heading + ": " + field(valid, title);
            if (!valid) return;
            value += "\n" + schedule(event) + "\nBroadcast status: " + broadcastStatus(event.running) + "\nDescription: " + field(valid, description);
        };
        append("Now", info.present_valid, info.present_title, info.present_description, raw.present);
        append("Next", info.following_valid, info.following_title, info.following_description, raw.following);
        if (value == details_text) return;
        details_text = value;
        if (!::ui::i18n::prepare_content_font_for_text(value.c_str())) value = "Programme information received, but its content font is unavailable.";
        ::ui::fonts::apply_content_font(details_label, value.c_str(), &lv_font_montserrat_14);
        lv_label_set_text(details_label, value.c_str());
    }
    void refreshBroadcast(const OneSegTvSession::Snapshot& state)
    {
        if (!broadcast) return;
        const auto& info = state.service_text;
        std::string value = "Station: " + field(info.service_valid, info.name) +
                            "\nNow: " + field(info.present_valid, info.present_title) +
                            "\nNext: " + field(info.following_valid, info.following_title);
        if (value == broadcast_text) return;
        broadcast_text = value;
        // Font loading is attempted only when the received text changes, never
        // on every video refresh. Do not render unsupported glyph placeholders.
        if (!::ui::i18n::prepare_content_font_for_text(value.c_str()))
            value = "Station / programme information received\nJapanese content font unavailable\nOpen Menu for reception controls";
        ::ui::fonts::apply_content_font(broadcast, value.c_str(), &lv_font_montserrat_14);
        lv_label_set_text(broadcast, value.c_str());
    }
    lv_obj_t* button(const char* caption, void (*action)(Impl&))
    {
        auto* value = lv_button_create(controls);
        lv_obj_set_size(value, LV_SIZE_CONTENT, 36);
        lv_obj_set_style_bg_color(value, ::ui::theme::surface_alt(), 0);
        lv_obj_set_style_text_color(value, ::ui::theme::text(), 0);
        lv_obj_set_style_border_color(value, ::ui::theme::border(), 0);
        lv_obj_set_style_border_width(value, 1, 0);
        lv_obj_set_style_shadow_width(value, 0, 0);
        lv_obj_set_style_bg_color(value, ::ui::theme::accent(), LV_STATE_FOCUSED);
        auto* label = lv_label_create(value);
        lv_label_set_text(label, caption);
        lv_obj_center(label);
        // Each callback owns a small, stable action slot for this page lifetime.
        actions.push_back({this, action});
        lv_obj_add_event_cb(
            value, [](lv_event_t* event)
            {
            auto* slot = static_cast<Action*>(lv_event_get_user_data(event));
            slot->call(*slot->owner); },
            LV_EVENT_CLICKED, &actions.back());
        lv_obj_add_event_cb(value, key, LV_EVENT_KEY, this);
        lv_group_add_obj(app_g, value);
        return value;
    }
    struct Action
    {
        Impl* owner;
        void (*call)(Impl&);
    };
    std::vector<Action> actions;
    bool scanning() const { return scan_pending; }
    void updateChannelList()
    {
        if (!channel_list) return;
        std::string options;
        for (const auto& entry : saved.channels())
        {
            if (!options.empty()) options += '\n';
            options += "CH " + std::to_string(entry.channel) + " / " + std::to_string(entry.service);
            if (!entry.name.empty()) options += " " + entry.name;
        }
        lv_dropdown_set_options(channel_list, options.empty() ? "No saved channels - scan first" : options.c_str());
        if (::ui::i18n::prepare_content_font_for_text(options.c_str()))
        {
            ::ui::fonts::apply_content_font(channel_list, options.c_str(), &lv_font_montserrat_14);
            ::ui::fonts::apply_content_font(lv_dropdown_get_list(channel_list), options.c_str(), &lv_font_montserrat_14);
        }
        else
        {
            // Keep the real channel/service identifiers usable if the optional
            // content font is absent; never show missing-glyph station names.
            std::string identifiers;
            for (const auto& entry : saved.channels())
            {
                if (!identifiers.empty()) identifiers += '\n';
                identifiers += "CH " + std::to_string(entry.channel) + " / " + std::to_string(entry.service);
            }
            lv_dropdown_set_options(channel_list, identifiers.empty() ? "No saved channels - scan first" : identifiers.c_str());
        }
        const auto selected_service = desired_service ? desired_service : session ? session->snapshot().decoder.programmes.selected_program
                                                                                  : 0;
        const auto& entries = saved.channels();
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].channel == channel && entries[i].service == selected_service)
            {
                lv_dropdown_set_selected(channel_list, static_cast<uint32_t>(i));
                break;
            }
        if (options.empty() || scanning()) lv_obj_add_state(channel_list, LV_STATE_DISABLED);
        else lv_obj_remove_state(channel_list, LV_STATE_DISABLED);
    }
    void toggleMenu()
    {
        hidden(details_panel, true);
        hidden(menu_panel, !lv_obj_has_flag(menu_panel, LV_OBJ_FLAG_HIDDEN));
        updateChannelList();
        refresh();
    }
    void scan()
    {
        if (offlineSource()) return;
        if (!session) return;
        if (scanning())
        {
            scanner->cancel();
            return;
        }
        const auto state = session->snapshot();
        if (!state.automatic_parameters)
        {
            notice = "Automatic scan unavailable with a fixed research profile";
            return;
        }
        resume_channel = channel;
        resume_service = desired_service ? desired_service : state.decoder.programmes.selected_program;
        resume_after_scan = state.active || pending_channel;
        scan_volume = state.volume;
        pending_channel = desired_service = 0;
        scan_port = std::make_unique<OneSegSessionScanPort>(*session);
        scanner = std::make_unique<OneSegScanController>(*scan_port, saved, OneSegScanController::Config{});
        scan_pending = scanner->start();
        if (scan_pending) session->setVolume(0);
        notice = scanner->snapshot().message;
        hidden(menu_panel, false);
        updateChannelList();
    }
    void refreshScan()
    {
        if (!scanning()) return;
        scanner->tick(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now().time_since_epoch())
                                                .count()));
        const auto& progress = scanner->snapshot();
        channel = progress.channel;
        notice = "Scan " + std::to_string(progress.scanned) + "/" + std::to_string(progress.total) +
                 " | found " + std::to_string(progress.found) + " | " + progress.message;
        if (scanner->running()) return;
        scan_pending = false;
        session->setVolume(scan_volume);
        const auto ended = scanner->snapshot();
        channel = resume_channel;
        if (ended.phase == OneSegScanController::Phase::Completed && ended.found && saved.lastChannel())
        {
            const auto selected = *saved.lastChannel();
            queueTune(selected.channel, selected.service);
            hidden(menu_panel, true);
        }
        else if (resume_after_scan && ended.phase != OneSegScanController::Phase::Error)
        {
            queueTune(resume_channel, resume_service);
            hidden(menu_panel, true);
        }
        notice = ended.message;
        updateChannelList();
    }
    bool startReception(unsigned target)
    {
        if (recorded_selected && iq_source.archive_path.empty())
        {
            notice = "Select a saved IQ archive first";
            return false;
        }
        auto value = reception;
        value.enabled = session && session->receptionTuningAvailable();
        const bool accepted = session && session->start(target, 320, 240, value, experiment, iq_source);
        if (accepted && recorded_selected) notice = "Recorded IQ input; RF controls are disabled";
        if (accepted && iq_source.synthetic) notice = "Known-reference OFDM model / virtual time / no RF, TV or FEC";
        return accepted;
    }
    void archiveOptions()
    {
        if (!archive_selector || !session || session->snapshot().active || session->snapshot().iq_exporting) return;
        const auto previous = iq_source.archive_path;
        iq_archives.clear();
        std::string options;
        unsigned selected = 0;
        for (const auto& filename : session->snapshot().iq_archives)
        {
            const std::filesystem::path path(filename);
            if (iq_archives.size() == 24) break;
            const auto name = path.filename().string();
            if (name.find_first_of("\r\n") != std::string::npos) continue;
            if (!options.empty()) options += '\n';
            if (path.string() == previous) selected = static_cast<unsigned>(iq_archives.size());
            options += name;
            iq_archives.push_back(path);
        }
        lv_dropdown_set_options(archive_selector, options.empty() ? "No saved IQ archives" : options.c_str());
        lv_dropdown_set_selected(archive_selector, selected);
        if (recorded_selected) iq_source.archive_path = iq_archives.empty() ? "" : iq_archives[selected].string();
    }
    void selectIqSource(bool recorded)
    {
        if (!session || scanning() || session->snapshot().iq_exporting) return;
        if (!offlineSource()) live_channel = channel;
        iq_source.synthetic = false;
        recorded_selected = recorded;
        iq_source.recording_capacity = 0;
        iq_source.archive_path = recorded && !iq_archives.empty() ? iq_archives[std::min<size_t>(lv_dropdown_get_selected(archive_selector), iq_archives.size() - 1)].string() : "";
        if (!recorded) channel = live_channel;
        pending_channel = desired_service = 0;
        session->requestStop();
        iq_catalog_pending = true;
        notice = recorded ? "Recorded IQ selected; choose archive then Start" : "Live receiver selected; factory RF controls available";
        refresh();
    }
    void recordIq()
    {
        if (!session || offlineSource() || scanning() || !session->receptionTuningAvailable()) return;
        const auto state = session->snapshot();
        if (state.iq_exporting) return;
        if (state.active && state.source.recording_capacity)
        {
            iq_source.recording_capacity = 0;
            pending_channel = 0;
            session->requestStop();
            notice = "Recording stopped; Save IQ exports the retained prefix";
        }
        else
        {
            iq_source.recording_capacity = 16 * 1024 * 1024;
            pending_channel = channel;
            if (state.active) session->requestStop();
            notice = "Recording raw IQ to a 16 MiB RAM buffer; no disk writes during reception";
        }
        refresh();
    }
    void saveIq()
    {
        if (!session || scanning() || session->snapshot().iq_exporting) return;
        iq_source.recording_capacity = 0;
        pending_channel = 0;
        const auto name = "iq-" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + ".tmiq";
        notice = session->requestIqExport((iq_directory / name).string()) ? "Stopping capture before saving IQ archive" : "No IQ recording is available to save";
        refresh();
    }
    void selectSyntheticSource()
    {
        if (!session || scanning() || session->snapshot().iq_exporting) return;
        if (!offlineSource()) live_channel = channel;
        recorded_selected = false;
        iq_source = {};
        iq_source.synthetic = true;
        iq_catalog_pending = false;
        pending_channel = desired_service = 0;
        session->requestStop();
        notice = "Synthetic OFDM: known framing, no RF, TV or FEC. Apply working parameters to run.";
        refresh();
    }
    void buildIqControls()
    {
        const auto dropdown = [&](int width, const char* options)
        {
            auto* object = lv_dropdown_create(source_bar);
            lv_obj_set_size(object, width, 28);
            lv_obj_set_style_text_font(object, &lv_font_montserrat_12, 0);
            lv_dropdown_set_options(object, options);
            lv_group_add_obj(app_g, object);
            lv_obj_add_event_cb(object, key, LV_EVENT_KEY, this);
            return object;
        };
        source_selector = dropdown(132, "Live receiver\nRecorded IQ\nSynthetic OFDM");
        archive_selector = dropdown(190, "No saved IQ archives");
        lv_obj_add_event_cb(
            source_selector, [](lv_event_t* event)
            { auto& self = *static_cast<Impl*>(lv_event_get_user_data(event)); const auto selected = lv_dropdown_get_selected(self.source_selector); if (selected == 2) self.selectSyntheticSource(); else self.selectIqSource(selected == 1); },
            LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(
            archive_selector, [](lv_event_t* event)
            {
            auto& self = *static_cast<Impl*>(lv_event_get_user_data(event));
            const auto index = lv_dropdown_get_selected(self.archive_selector);
            if (self.recorded_selected && index < self.iq_archives.size()) self.iq_source.archive_path = self.iq_archives[index].string(); },
            LV_EVENT_VALUE_CHANGED, this);
        iq_loop = lv_checkbox_create(source_bar);
        lv_checkbox_set_text(iq_loop, "Loop");
        lv_obj_set_style_text_font(iq_loop, &lv_font_montserrat_12, 0);
        lv_group_add_obj(app_g, iq_loop);
        lv_obj_add_event_cb(
            iq_loop, [](lv_event_t* event)
            { auto& self = *static_cast<Impl*>(lv_event_get_user_data(event)); self.iq_source.loop = lv_obj_has_state(self.iq_loop, LV_STATE_CHECKED); },
            LV_EVENT_VALUE_CHANGED, this);
        const auto button = [&](const char* caption, lv_event_cb_t action)
        {
            auto* object = lv_button_create(source_bar);
            lv_obj_set_size(object, 80, 28);
            lv_obj_set_style_pad_all(object, 2, 0);
            lv_obj_set_style_radius(object, 4, 0);
            lv_obj_set_style_bg_color(object, ::ui::theme::surface_alt(), 0);
            lv_obj_set_style_bg_color(object, ::ui::theme::surface_alt(), LV_STATE_DISABLED);
            lv_obj_set_style_text_color(object, ::ui::theme::text(), 0);
            lv_obj_set_style_text_opa(object, LV_OPA_50, LV_STATE_DISABLED);
            lv_obj_set_style_border_width(object, 1, 0);
            lv_obj_set_style_border_color(object, ::ui::theme::border(), 0);
            auto* label = text(object, caption);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(label);
            lv_group_add_obj(app_g, object);
            lv_obj_add_event_cb(object, action, LV_EVENT_CLICKED, this);
            lv_obj_add_event_cb(object, key, LV_EVENT_KEY, this);
            return object;
        };
        record_button = button("Record IQ", [](lv_event_t* event)
                               { static_cast<Impl*>(lv_event_get_user_data(event))->recordIq(); });
        record_caption = lv_obj_get_child(record_button, 0);
        iq_save_button = button("Save IQ", [](lv_event_t* event)
                                { static_cast<Impl*>(lv_event_get_user_data(event))->saveIq(); });
        iq_count = text(source_bar, "0.0 MiB");
        lv_obj_set_width(iq_count, 76);
        lv_obj_set_style_text_font(iq_count, &lv_font_montserrat_12, 0);
        lv_label_set_long_mode(iq_count, LV_LABEL_LONG_CLIP);
        archiveOptions();
    }
    void applyWorkbench(const OneSegReceptionSettings& hardware, const OneSegOfdmExperiment& settings)
    {
        if (!session || scanning()) return;
        reception = hardware;
        experiment = settings;
        pending_channel = channel;
        const auto state = session->snapshot();
        desired_service = state.decoder.programmes.selected_program;
        notice = "Applying experiment after receiver release";
        if (state.active) session->requestStop(state.generation);
        refresh();
    }
    void saveExperiment(const OneSegReceptionSettings& hardware, const OneSegOfdmExperiment& settings)
    {
        if (!session || scanning() || !settings.valid(iq_source.synthetic ? 64000000 / 63 : hardware.sample_rate_hz, iq_source.synthetic)) return;
        const auto number = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        queued_profile = {};
        queued_profile.id = "experiment-" + number;
        queued_profile.name = "Experiment " + number;
        queued_profile.reception = hardware;
        queued_profile.experiment = settings;
        queued_profile.synthetic = iq_source.synthetic;
        profile_save_pending = true;
        const auto state = session->snapshot();
        if (state.active)
        {
            pending_channel = channel;
            session->requestStop(state.generation);
        }
        notice = "Saving experiment after capture stops";
        refresh();
    }
    void profileOptions()
    {
        if (!experiment_list) return;
        experiment_ids = {"default"};
        std::string options = "Factory default / 450k / 8.7M / gain 9";
        std::error_code error;
        std::vector<std::string> ids;
        if (std::filesystem::is_directory(profile_directory, error))
            for (const auto& entry : std::filesystem::directory_iterator(profile_directory, error))
            {
                if (ids.size() >= 16) break;
                if (entry.path().extension() == ".profile" && entry.path().stem() != "default") ids.push_back(entry.path().stem().string());
            }
        std::sort(ids.begin(), ids.end());
        for (const auto& id : ids)
        {
            OneSegOfdmProfile profile;
            if (!profiles.load(id, profile)) continue;
            experiment_ids.push_back(id);
            options += '\n' + profile.name;
        }
        lv_dropdown_set_options(experiment_list, options.c_str());
    }
    void toggle()
    {
        if (!session || scanning()) return;
        const auto state = session->snapshot();
        if (state.active)
        {
            pending_channel = desired_service = 0;
            session->requestStop(state.generation);
        }
        else if (state.iq_catalog_loading)
        {
            pending_channel = channel;
            notice = "Starting after archive directory read completes";
        }
        else startReception(channel);
        refresh();
    }
    void queueTune(unsigned target, unsigned service = 0)
    {
        if (!session || scanning()) return;
        notice.clear();
        channel = target;
        desired_service = service;
        const auto state = session->snapshot();
        if (state.active && state.channel == target && service && state.error == OneSegTvSession::Error::None && state.phase != OneSegTvSession::Phase::Stopping)
            return; // Selection within the same transport does not restart RF.
        pending_channel = target;
        if (state.active) session->requestStop(state.generation);
    }
    void retry()
    {
        if (!session || scanning() || pending_channel) return;
        const auto state = session->snapshot();
        if (state.receive.lease_restore_failed || state.receive.error == OneSegReceiveWorker::Error::RadioRestore)
        {
            notice = "Radio restoration failed; receiver restart blocked";
            refresh();
            return;
        }
        if (state.phase == OneSegTvSession::Phase::Stopping) return;
        if (!desired_service) desired_service = state.decoder.programmes.selected_program;
        pending_channel = channel;
        retry_pending = true;
        notice = "Retrying selected channel after receiver release";
        hidden(menu_panel, true);
        if (state.active) session->requestStop(state.generation);
        refresh();
    }
    void stepChannel(int direction)
    {
        if (scanning() || offlineSource()) return;
        const unsigned target = static_cast<unsigned>(std::clamp(static_cast<int>(channel) + direction, 13, 62));
        if (target == channel) return;
        const bool receiving = session && session->snapshot().active;
        channel = target;
        desired_service = 0;
        if (receiving || pending_channel) queueTune(target);
        refresh();
    }
    void stepStation(int direction)
    {
        if (offlineSource()) return;
        const auto& entries = saved.channels();
        if (entries.empty() || !session || scanning()) return;
        const auto state = session->snapshot();
        size_t index = 0;
        bool found = false;
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].channel == channel &&
                entries[i].service == (desired_service ? desired_service : state.decoder.programmes.selected_program))
            {
                index = i;
                found = true;
                break;
            }
        if (found) index = direction > 0 ? (index + 1) % entries.size() : (index + entries.size() - 1) % entries.size();
        const auto target = entries[index];
        queueTune(target.channel, target.service);
        refresh();
    }
    void nextProgramme()
    {
        if (!session || scanning()) return;
        const auto state = session->snapshot();
        const auto& table = state.decoder.programmes;
        if (table.program_count < 2) return;
        size_t next = 0;
        for (size_t i = 0; i < table.program_count; ++i)
            if (table.programs[i].number == table.selected_program) next = (i + 1) % table.program_count;
        desired_service = table.programs[next].number;
        session->selectProgramme(state.generation, table.programs[next].number);
    }
    void setFullscreen()
    {
        hidden(details_panel, true);
        fullscreen = !fullscreen;
        if (workbench) workbench->fullscreen(fullscreen);
        hidden(bar.container, fullscreen);
        hidden(status, fullscreen);
        hidden(source_bar, fullscreen);
        hidden(broadcast, fullscreen);
        // Focused viewing keeps the requested 320x240 viewport unchanged.
        lv_obj_set_flex_align(root, fullscreen ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
    static void key(lv_event_t* event)
    {
        auto& self = *static_cast<Impl*>(lv_event_get_user_data(event));
        const uint32_t value = lv_event_get_key(event);
        if (value == LV_KEY_ESC)
        {
            if (self.scanning())
            {
                self.scanner->cancel();
                return;
            }
            if (self.details_panel && !lv_obj_has_flag(self.details_panel, LV_OBJ_FLAG_HIDDEN))
            {
                self.toggleDetails();
                return;
            }
            if (self.menu_panel && !lv_obj_has_flag(self.menu_panel, LV_OBJ_FLAG_HIDDEN))
            {
                hidden(self.menu_panel, true);
                self.refresh();
                return;
            }
            ui_request_exit_to_menu();
            return;
        }
        if (self.details_panel && !lv_obj_has_flag(self.details_panel, LV_OBJ_FLAG_HIDDEN) &&
            (value == LV_KEY_UP || value == LV_KEY_DOWN))
        {
            lv_obj_scroll_by(self.details_body, 0, value == LV_KEY_UP ? 64 : -64, LV_ANIM_OFF);
            return;
        }
        if (value == ' ') self.toggle();
        else if (value == 'p' || value == 'P') self.nextProgramme();
        else if (value == 'f' || value == 'F') self.setFullscreen();
        else if (value == 'm' || value == 'M') self.toggleMenu();
        else if (value == 'r' || value == 'R') self.retry();
        else if ((value == 't' || value == 'T') && self.workbench) self.workbench->selectStage(OneSegOfdmStage::Tracking);
    }
    void refresh()
    {
        if (!root) return;
        if (!session)
        {
            setText(status, "TV receiver unavailable in this runtime mode.");
            for (auto* object : {start_button, channel_down, channel_up, programme, station_down, station_up, scan_button, channel_list, retry_button})
                lv_obj_add_state(object, LV_STATE_DISABLED);
            return;
        }
        refreshScan();
        const bool menu_open = !lv_obj_has_flag(menu_panel, LV_OBJ_FLAG_HIDDEN);
        const bool details_open = !lv_obj_has_flag(details_panel, LV_OBJ_FLAG_HIDDEN);
        if (workbench) hidden(workbench->object(), menu_open || details_open);
        hidden(controls, false);
        hidden(status, fullscreen);
        hidden(viewing, menu_open || details_open);
        hidden(broadcast, fullscreen || menu_open || details_open);
        hidden(video, menu_open || details_open);
        auto state = session->snapshot();
        hidden(source_bar, fullscreen);
        if (recorded_selected && !state.source.archive_path.empty() && state.active) channel = state.channel;
        if (state.iq_export_serial != iq_export_serial)
        {
            iq_export_serial = state.iq_export_serial;
            notice = state.iq_export_message;
            iq_catalog_pending = true;
        }
        if (state.iq_catalog_serial != iq_catalog_serial)
        {
            iq_catalog_serial = state.iq_catalog_serial;
            archiveOptions();
            if (state.iq_catalog_message[0]) notice = state.iq_catalog_message;
        }
        if (iq_catalog_pending && !state.active && !state.iq_exporting && !state.iq_catalog_loading)
            if (session->requestIqCatalog(iq_directory.string()))
            {
                iq_catalog_pending = false;
                state = session->snapshot();
            }
        if (profile_save_pending && !state.active)
        {
            notice = profiles.save(queued_profile, true) ? "Experiment saved; factory preset unchanged" : "Unable to save experiment";
            profile_save_pending = false;
            profileOptions();
        }
        if (workbench)
        {
            workbench->update(state);
            workbench->setRecordedSource(offlineSource());
            workbench->setSyntheticSource(iq_source.synthetic);
            if (!notice.empty()) workbench->notice(notice.c_str());
        }
        refreshBroadcast(state);
        refreshDetails(state);
        if (pending_channel && !state.active && !state.iq_exporting && !state.iq_catalog_loading)
        {
            const unsigned target = pending_channel;
            pending_channel = 0;
            if (state.receive.lease_restore_failed || state.receive.error == OneSegReceiveWorker::Error::RadioRestore)
                notice = "Radio restoration failed; channel switch cancelled";
            else if (!startReception(target)) notice = "Unable to start selected channel";
            state = session->snapshot();
        }
        if (state.active && desired_service && state.decoder.programmes.selected_program != desired_service)
            session->selectProgramme(state.generation, static_cast<uint16_t>(desired_service));
        const char* phase = "Stopped - select channel and Start";
        using Phase = OneSegTvSession::Phase;
        switch (state.phase)
        {
        case Phase::Starting:
            phase = "Starting receiver";
            break;
        case Phase::ReceivingIq:
            phase = "Receiving IQ / waiting for programme";
            break;
        case Phase::Decoding:
            phase = "Decoding / waiting for fresh video";
            break;
        case Phase::FramePresented:
            phase = "Video frame submitted";
            break;
        case Phase::Stopping:
            phase = "Stopping / restoring radio";
            break;
        case Phase::Stopped:
            phase = "Stopped";
            break;
        case Phase::Error:
            phase = "Receiver error";
            break;
        default:
            break;
        }
        const bool signal_interrupted = state.active && state.error == OneSegTvSession::Error::None &&
                                        state.phase != Phase::Stopping && state.media.video_frames &&
                                        (state.iq_age_ms >= 2000 || state.video_age_ms >= 2000);
        if (signal_interrupted) phase = "Signal interrupted - Retry / R to reacquire";
        else if (!state.active && oneseg_scan_observation(state).no_signal)
            phase = "No supported television signal - Retry / R or scan channels";
        const char* timing = "Waiting for media clock";
        using Timing = OneSegTvSession::Timing;
        switch (state.timing)
        {
        case Timing::WaitingForAudio:
            timing = "Waiting for audio timestamps";
            break;
        case Timing::AudioQueueEstimate:
            timing = "Audio queue clock estimate";
            break;
        case Timing::AudioStarved:
            timing = "Audio queue empty";
            break;
        case Timing::VideoMonotonic:
            timing = "Video-only clock";
            break;
        case Timing::MissingVideoPts:
            timing = "Missing video timestamps";
            break;
        default:
            break;
        }
        const uint32_t now = lv_tick_get();
        if (state.source.synthetic && state.active && state.error == OneSegTvSession::Error::None && state.phase != Phase::Stopping)
            phase = "Synthetic OFDM / no TV programme";
        const bool status_changed = !status_initialized || status_generation != state.generation ||
                                    status_programme != state.decoder.programmes.selected_program || status_phase != phase || status_timing != timing ||
                                    status_message != state.message || status_notice != notice;
        if (status_changed || uint32_t(now - status_tick) >= 1000)
        {
            // Counters are diagnostic text, not video. Repainting them at 30 Hz
            // needlessly competes with the sub-millisecond radio FIFO service.
            lv_label_set_text_fmt(status, "%s%s%s | CH %u / Service %u | %s",
                                  phase, state.message[0] ? ": " : "", state.message, channel,
                                  state.decoder.programmes.selected_program, notice.empty() ? timing : notice.c_str());
            status_initialized = true;
            status_tick = now;
            status_generation = state.generation;
            status_programme = state.decoder.programmes.selected_program;
            status_phase = phase;
            status_timing = timing;
            status_message = state.message;
            status_notice = notice;
        }
        char caption[40];
        if (iq_source.synthetic) std::snprintf(caption, sizeof(caption), "Model");
        else std::snprintf(caption, sizeof(caption), "CH %u", channel);
        setText(channel_label, caption);
        std::snprintf(caption, sizeof(caption), "Volume %u%%", state.volume);
        setText(volume_label, caption);
        setText(start_label, state.active ? "Stop" : "Start");
        for (auto* object : {channel_down, channel_up})
        {
            if (offlineSource()) lv_obj_add_state(object, LV_STATE_DISABLED);
            else lv_obj_remove_state(object, LV_STATE_DISABLED);
        }
        for (auto* object : {station_down, station_up})
            if (offlineSource() || saved.channels().empty()) lv_obj_add_state(object, LV_STATE_DISABLED);
            else lv_obj_remove_state(object, LV_STATE_DISABLED);
        if (!state.active || state.decoder.programmes.program_count < 2) lv_obj_add_state(programme, LV_STATE_DISABLED);
        else lv_obj_remove_state(programme, LV_STATE_DISABLED);
        setText(scan_label, scanning() ? "Cancel scan" : "Scan channels");
        if (scanning() || pending_channel || state.phase == Phase::Stopping || state.receive.lease_restore_failed ||
            state.receive.error == OneSegReceiveWorker::Error::RadioRestore)
            lv_obj_add_state(retry_button, LV_STATE_DISABLED);
        else lv_obj_remove_state(retry_button, LV_STATE_DISABLED);
        if (scanning())
            for (auto* object : {start_button, channel_down, channel_up, programme, station_down, station_up, channel_list})
                lv_obj_add_state(object, LV_STATE_DISABLED);
        else lv_obj_remove_state(start_button, LV_STATE_DISABLED);
        const auto enable = [](lv_obj_t* object, bool enabled)
        { if (enabled) lv_obj_remove_state(object, LV_STATE_DISABLED); else lv_obj_add_state(object, LV_STATE_DISABLED); };
        enable(source_selector, !scanning() && !state.iq_exporting);
        enable(archive_selector, recorded_selected && !state.active && !state.iq_exporting && !state.iq_catalog_loading && !iq_archives.empty());
        enable(iq_loop, recorded_selected && !state.active && !state.iq_exporting);
        enable(record_button, !offlineSource() && !scanning() && !state.iq_exporting && session->receptionTuningAvailable());
        enable(iq_save_button, !state.iq_exporting && !state.iq_catalog_loading && !scanning() && state.receive.recorded_bytes > 0);
        if (state.iq_exporting || state.iq_catalog_loading) enable(start_button, false);
        if (offlineSource()) enable(scan_button, false);
        else enable(scan_button, state.automatic_parameters && !state.iq_exporting);
        setText(record_caption, state.active && state.source.recording_capacity ? state.receive.recording_full ? "Buffer full" : "Stop rec" : "Record IQ");
        char recorded[64];
        std::snprintf(recorded, sizeof(recorded), "%.1f MiB%s", double(state.receive.recorded_bytes) / (1024 * 1024), state.iq_exporting ? " saving" : "");
        setText(iq_count, recorded);
        if (scanning()) lv_obj_add_state(details_button, LV_STATE_DISABLED);
        else lv_obj_remove_state(details_button, LV_STATE_DISABLED);
        const bool fresh = !scanning() && !menu_open && !details_open && state.active && state.error == OneSegTvSession::Error::None && state.media_epoch &&
                           state.phase != Phase::Stopping && state.iq_age_ms < 2000 && state.video_age_ms < 2000;
        OneSegMediaWorker::FrameInfo frame;
        if (fresh && session->copyLatestFrame(state.generation, state.media_epoch, pixels.data(), pixels.size(), frame))
        {
            if (drawn_generation != state.generation || drawn_epoch != state.media_epoch || drawn_ordinal != frame.ordinal)
            {
                lv_image_cache_drop(&descriptor);
                descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
                descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
                descriptor.header.w = frame.width;
                descriptor.header.h = frame.height;
                descriptor.header.stride = frame.stride;
                descriptor.data_size = frame.stride * frame.height;
                descriptor.data = pixels.data();
                lv_image_set_src(image, &descriptor);
                lv_obj_update_layout(video);
                const auto scale = std::min({256u,
                                             lv_obj_get_content_width(video) * 256 / std::max<unsigned>(frame.width, 1),
                                             lv_obj_get_content_height(video) * 256 / std::max<unsigned>(frame.height, 1)});
                lv_image_set_scale(image, std::max<unsigned>(1, scale));
                lv_obj_center(image);
                drawn_generation = state.generation;
                drawn_epoch = state.media_epoch;
                drawn_ordinal = frame.ordinal;
                session->acknowledgeFrame(state.generation, state.media_epoch, frame.ordinal);
            }
        }
        const auto displayed = session->snapshot();
        const bool visible = fresh && displayed.active && displayed.error == OneSegTvSession::Error::None &&
                             displayed.phase != Phase::Stopping && displayed.presentation_age_ms < 2000 &&
                             (!desired_service || displayed.decoder.programmes.selected_program == desired_service) &&
                             (displayed.timing == Timing::AudioQueueEstimate || displayed.timing == Timing::VideoMonotonic) &&
                             drawn_generation == state.generation && drawn_epoch == state.media_epoch && drawn_ordinal;
        hidden(image, !visible);
        hidden(waiting, visible);
        if (retry_pending && visible)
        {
            retry_pending = false;
            notice = "Reception recovered";
        }
        else if (retry_pending && !displayed.active && !pending_channel)
        {
            retry_pending = false;
            notice = "Retry ended without a fresh picture";
        }
        if (state.source.synthetic)
        {
            char model[400];
            if (state.decoder.ofdm_phase.size && state.decoder.ofdm_phase.newest().has_reference)
            {
                const auto& r = state.decoder.ofdm_phase.newest();
                std::snprintf(model, sizeof(model), "SYNTHETIC OFDM\nKnown framing / no TV or FEC\nVirtual time %.3f s\nInjected SNR %.1f dB\nReference EVM %.2f%%\nUncoded BER %.4g / %.0f bits\nNo RF transmission or reception", r.virtual_time, r.synthetic_snr, r.reference_evm * 100, r.reference_ber, r.reference_bits);
            }
            else std::snprintf(model, sizeof(model), "SYNTHETIC OFDM\nGenerating known-reference IQ\nNo RF, TV, TMCC or FEC");
            setText(waiting, model);
            setText(broadcast, "Synthetic reference / no broadcast service");
        }
        else setText(waiting, scanning() ? "Scanning channels - audio muted" : signal_interrupted                                                                ? "Signal interrupted\nRetry / R to reacquire"
                                                                           : desired_service && displayed.decoder.programmes.selected_program != desired_service ? "Waiting for selected service"
                                                                                                                                                                 : "No fresh decoded picture");
        const auto& table = displayed.decoder.programmes;
        const auto received_name = oneseg_received_channel_name(displayed.service_text, table.selected_program);
        if (!offlineSource() && !scanning() && visible && table.pmt_valid && table.selected_program &&
            (!desired_service || desired_service == table.selected_program) &&
            (save_attempt_generation != displayed.generation || save_attempt_service != table.selected_program ||
             (!received_name.empty() && save_attempt_name != received_name)))
        {
            save_attempt_generation = displayed.generation;
            save_attempt_service = table.selected_program;
            save_attempt_name = received_name;
            const auto* last = saved.lastChannel();
            if (!last || last->channel != displayed.channel || last->service != table.selected_program ||
                (!received_name.empty() && last->name != received_name))
            {
                saved.rememberReceived({displayed.channel, table.selected_program, received_name}, notice);
                updateChannelList();
            }
        }
    }
    void close()
    {
        retry_pending = false;
        if (scanning())
        {
            scanner->cancel();
            if (session) session->setVolume(scan_volume);
        }
        scanner.reset();
        scan_port.reset();
        scan_pending = resume_after_scan = false;
        pending_channel = desired_service = 0;
        if (session) session->requestStop();
        if (timer) lv_timer_delete(timer);
        timer = nullptr;
        workbench.reset();
        if (root)
        {
            platform::ui::screen::enable_sleep();
            lv_obj_delete(root);
        }
        root = nullptr;
        viewing = nullptr;
        broadcast = nullptr;
        broadcast_text.clear();
        details_panel = details_body = details_label = details_close = details_button = nullptr;
        details_text.clear();
        menu_panel = channel_list = scan_button = scan_label = nullptr;
        experiment_list = nullptr;
        experiment_ids.clear();
        profile_save_pending = false;
        source_bar = source_selector = archive_selector = record_button = record_caption = iq_save_button = iq_count = iq_loop = nullptr;
        iq_archives.clear();
        iq_source = {};
        recorded_selected = false;
        iq_catalog_pending = true;
        lv_image_cache_drop(&descriptor);
        actions.clear();
        pixels.clear();
        drawn_generation = drawn_epoch = drawn_ordinal = 0;
        status_initialized = false;
    }
};
OneSegTvApp::OneSegTvApp(std::unique_ptr<OneSegTvSession> session) : impl_(std::make_unique<Impl>(std::move(session))) {}
OneSegTvApp::~OneSegTvApp() { impl_->close(); }
void OneSegTvApp::exit(lv_obj_t*) { impl_->close(); }
void OneSegTvApp::enter(lv_obj_t* parent)
{
    auto& s = *impl_;
    s.close();
    s.actions.reserve(64);
    s.pixels.resize(OneSegMediaWorker::max_width * OneSegMediaWorker::max_height * 2);
    s.fullscreen = false;
    s.root = lv_obj_create(parent);
    platform::ui::screen::disable_sleep();
    lv_obj_set_size(s.root, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(s.root, 0, 0);
    lv_obj_set_style_bg_color(s.root, ::ui::theme::page_bg(), 0);
    lv_obj_set_style_border_width(s.root, 0, 0);
    lv_obj_set_style_radius(s.root, 0, 0);
    ::ui::widgets::TopBarConfig bar_config;
    bar_config.power_indicator = false;
    ::ui::widgets::top_bar_init(s.bar, s.root, bar_config);
    ::ui::widgets::top_bar_set_title(s.bar, "1SEG TV");
    ::ui::widgets::top_bar_set_back_callback(
        s.bar, [](void*)
        { ui_request_exit_to_menu(); },
        nullptr);
    s.title = s.bar.title_label;
    s.source_bar = lv_obj_create(s.root);
    lv_obj_set_size(s.source_bar, lv_pct(100), 30);
    lv_obj_set_flex_flow(s.source_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s.source_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(s.source_bar, 0, 0);
    lv_obj_set_style_pad_column(s.source_bar, 4, 0);
    lv_obj_set_style_border_width(s.source_bar, 0, 0);
    lv_obj_remove_flag(s.source_bar, LV_OBJ_FLAG_SCROLLABLE);
    s.status = Impl::text(s.source_bar, "Ready");
    lv_obj_set_width(s.status, 0);
    lv_obj_set_flex_grow(s.status, 1);
    lv_obj_set_height(s.status, 18);
    lv_label_set_long_mode(s.status, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s.status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_hor(s.status, 12, 0);
    lv_obj_set_style_text_color(s.status, ::ui::theme::text(), 0);
    s.broadcast = Impl::text(s.root, "Station: Not received\nNow: Not received\nNext: Not received");
    lv_obj_set_height(s.broadcast, 54);
    lv_label_set_long_mode(s.broadcast, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s.broadcast, &lv_font_montserrat_14, 0);
    s.viewing = lv_obj_create(s.root);
    lv_obj_set_size(s.viewing, lv_pct(100), 260);
    lv_obj_set_flex_flow(s.viewing, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s.viewing, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(s.viewing, 10, 0);
    lv_obj_set_style_border_width(s.viewing, 0, 0);
    lv_obj_set_style_bg_color(s.viewing, ::ui::theme::surface(), 0);
    s.video = lv_obj_create(s.viewing);
    lv_obj_set_size(s.video, 320, 240);
    lv_obj_set_style_pad_all(s.video, 0, 0);
    lv_obj_set_style_border_width(s.video, 0, 0);
    lv_obj_set_style_radius(s.video, 0, 0);
    lv_obj_set_style_bg_color(s.video, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s.video, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s.video, LV_OBJ_FLAG_SCROLLABLE);
    s.waiting = Impl::text(s.video, "No fresh decoded picture");
    lv_obj_set_style_text_color(s.waiting, lv_color_white(), 0);
    lv_obj_set_style_text_align(s.waiting, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s.waiting);
    s.image = lv_image_create(s.video);
    Impl::hidden(s.image, true);
    lv_obj_set_parent(s.broadcast, s.viewing);
    lv_obj_set_width(s.broadcast, lv_pct(45));
    lv_obj_set_height(s.broadcast, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(s.broadcast, 14, 0);
    lv_obj_set_style_bg_color(s.broadcast, ::ui::theme::surface_alt(), 0);
    lv_obj_set_style_bg_opa(s.broadcast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s.broadcast, 12, 0);
    lv_label_set_long_mode(s.broadcast, LV_LABEL_LONG_WRAP);
    s.controls = lv_obj_create(s.root);
    lv_obj_set_size(s.controls, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s.controls, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(s.controls, 4, 0);
    lv_obj_set_style_bg_color(s.controls, ::ui::theme::surface(), 0);
    lv_obj_set_style_border_color(s.controls, ::ui::theme::separator(), 0);
    lv_group_remove_all_objs(app_g);
    lv_group_add_obj(app_g, s.bar.back_btn);
    lv_obj_add_event_cb(s.bar.back_btn, Impl::key, LV_EVENT_KEY, &s);
    s.channel_down = s.button("CH -", [](Impl& v)
                              { v.stepChannel(-1); });
    s.channel_label = Impl::text(s.controls, "CH 13");
    lv_obj_set_width(s.channel_label, 65);
    s.channel_up = s.button("CH +", [](Impl& v)
                            { v.stepChannel(1); });
    s.station_down = s.button("Station -", [](Impl& v)
                              { v.stepStation(-1); });
    s.station_up = s.button("Station +", [](Impl& v)
                            { v.stepStation(1); });
    s.start_button = s.button("Start", [](Impl& v)
                              { v.toggle(); });
    s.start_label = lv_obj_get_child(s.start_button, 0);
    s.retry_button = s.button("Retry", [](Impl& v)
                              { v.retry(); });
    s.programme = s.button("Next service", [](Impl& v)
                           { v.nextProgramme(); });
    s.button("Vol -", [](Impl& v)
             { if (v.session && !v.scanning()) { auto n = v.session->snapshot().volume; v.session->setVolume(n > 5 ? n - 5 : 0); } v.refresh(); });
    s.volume_label = Impl::text(s.controls, "Volume 70%");
    lv_obj_set_width(s.volume_label, 110);
    s.button("Vol +", [](Impl& v)
             { if (v.session && !v.scanning()) v.session->setVolume(v.session->snapshot().volume + 5); v.refresh(); });
    s.button("Fullscreen", [](Impl& v)
             { v.setFullscreen(); });
    s.button("Menu", [](Impl& v)
             { v.toggleMenu(); });
    s.menu_panel = lv_obj_create(s.root);
    lv_obj_set_size(s.menu_panel, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s.menu_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(s.menu_panel, ::ui::theme::surface(), 0);
    for (auto* object : {s.station_down, s.station_up, s.programme}) lv_obj_set_parent(object, s.menu_panel);
    Impl::text(s.menu_panel, "Channels - select a saved service or scan UHF 13-62");
    s.channel_list = lv_dropdown_create(s.menu_panel);
    lv_obj_set_width(s.channel_list, lv_pct(100));
    lv_group_add_obj(app_g, s.channel_list);
    lv_obj_add_event_cb(s.channel_list, Impl::key, LV_EVENT_KEY, &s);
    lv_obj_add_event_cb(
        s.channel_list, [](lv_event_t* event)
        {
        auto& v = *static_cast<Impl*>(lv_event_get_user_data(event));
        if (v.scanning()) return;
        const auto index = lv_dropdown_get_selected(v.channel_list);
        if (index < v.saved.channels().size())
        {
            const auto entry = v.saved.channels()[index];
            v.queueTune(entry.channel, entry.service);
            Impl::hidden(v.menu_panel, true);
            v.refresh();
        } },
        LV_EVENT_VALUE_CHANGED, &s);
    s.scan_button = s.button("Scan channels", [](Impl& v)
                             { v.scan(); v.refresh(); });
    lv_obj_set_parent(s.scan_button, s.menu_panel);
    s.scan_label = lv_obj_get_child(s.scan_button, 0);
    s.details_button = s.button("Programme info", [](Impl& v)
                                { v.toggleDetails(); });
    lv_obj_set_parent(s.details_button, s.menu_panel);
    s.details_panel = lv_obj_create(s.root);
    lv_obj_set_size(s.details_panel, lv_pct(100), 240);
    lv_obj_set_flex_flow(s.details_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(s.details_panel, ::ui::theme::surface(), 0);
    s.details_close = s.button("Close info", [](Impl& v)
                               { v.toggleDetails(); });
    lv_obj_set_parent(s.details_close, s.details_panel);
    s.details_body = lv_obj_create(s.details_panel);
    lv_obj_set_size(s.details_body, lv_pct(100), 0);
    lv_obj_set_flex_grow(s.details_body, 1);
    lv_obj_set_style_pad_all(s.details_body, 4, 0);
    lv_obj_set_style_border_width(s.details_body, 0, 0);
    lv_obj_set_style_bg_opa(s.details_body, LV_OPA_TRANSP, 0);
    s.details_label = Impl::text(s.details_body, "Not received");
    lv_group_add_obj(app_g, s.details_body);
    lv_obj_add_event_cb(s.details_body, Impl::key, LV_EVENT_KEY, &s);
    Impl::hidden(s.details_panel, true);
    s.updateChannelList();
    Impl::hidden(s.menu_panel, true);
    auto* help = s.button("H Help", [](Impl& v)
                          { ::ui::input::pager::open_help(v.root, app_g); });
    lv_obj_add_event_cb(
        lv_obj_get_child(help, 0), [](lv_event_t* event)
        {
        auto& v = *static_cast<Impl*>(lv_event_get_user_data(event));
                            ::ui::input::pager::open_help(v.root, app_g); },
        LV_EVENT_CLICKED, &s);
    s.button("Back", [](Impl&)
             { ui_request_exit_to_menu(); });
    Impl::text(s.menu_panel, "Experiment profiles / explicit preview load");
    s.experiment_list = lv_dropdown_create(s.menu_panel);
    lv_obj_set_width(s.experiment_list, lv_pct(100));
    lv_group_add_obj(app_g, s.experiment_list);
    lv_obj_add_event_cb(
        s.experiment_list, [](lv_event_t* event)
        {
        auto& self = *static_cast<Impl*>(lv_event_get_user_data(event));
        const auto index = lv_dropdown_get_selected(self.experiment_list);
        if (index >= self.experiment_ids.size()) return;
        OneSegOfdmProfile profile;
        if (!self.profiles.load(self.experiment_ids[index], profile)) return;
        if (profile.synthetic) { lv_dropdown_set_selected(self.source_selector, 2); self.selectSyntheticSource(); }
        if (self.workbench) self.workbench->setWorking(profile.reception, profile.experiment);
        self.notice = "Experiment loaded for preview; Apply to change reception";
        Impl::hidden(self.menu_panel, true);
        self.refresh(); },
        LV_EVENT_VALUE_CHANGED, &s);
    s.profileOptions();
    OneSegOfdmWorkbench::Callbacks lab_callbacks;
    lab_callbacks.apply = [&s](const auto& hardware, const auto& experiment)
    { s.applyWorkbench(hardware, experiment); };
    lab_callbacks.save = [&s](const auto& hardware, const auto& experiment)
    { s.saveExperiment(hardware, experiment); };
    lab_callbacks.load = [&s]
    { s.profileOptions(); s.toggleMenu(); };
    lab_callbacks.factory_reset = [&s]
    {
        if (s.offlineSource()) s.channel = s.live_channel;
        s.recorded_selected = false;
        s.iq_source = {};
        lv_dropdown_set_selected(s.source_selector, 0);
        lv_obj_remove_state(s.iq_loop, LV_STATE_CHECKED);
        s.applyWorkbench(oneseg_factory_profile().reception, {});
    };
    s.workbench = std::make_unique<OneSegOfdmWorkbench>(s.root, app_g, std::move(lab_callbacks));
    s.workbench->setWorking(s.reception, s.experiment);
    s.buildIqControls();
    lv_obj_set_parent(s.viewing, s.workbench->videoHost());
    lv_obj_set_size(s.viewing, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s.viewing, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s.viewing, 0, 0);
    lv_obj_set_style_pad_row(s.viewing, 0, 0);
    lv_obj_set_width(s.broadcast, lv_pct(100));
    lv_obj_set_height(s.broadcast, 29);
    lv_obj_set_style_pad_all(s.broadcast, 0, 0);
    lv_label_set_long_mode(s.broadcast, LV_LABEL_LONG_CLIP);
    lv_obj_set_height(s.controls, 32);
    lv_obj_set_flex_flow(s.controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(s.controls, 0, 0);
    for (uint32_t index = 0; index < lv_obj_get_child_count(s.controls); ++index)
    {
        auto* object = lv_obj_get_child(s.controls, index);
        if (lv_obj_check_type(object, &lv_button_class)) lv_obj_set_height(object, 28);
    }
    ::ui::input::pager::prepare_group(app_g);
    for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
        if (lv_indev_get_type(input) == LV_INDEV_TYPE_KEYPAD) lv_indev_set_group(input, app_g);
    lv_group_focus_obj(s.start_button);
    s.timer = lv_timer_create([](lv_timer_t* timer)
                              { static_cast<Impl*>(lv_timer_get_user_data(timer))->refresh(); },
                              33, &s);
    if (const auto* last = s.saved.lastChannel())
    {
        // Preserve immediate factory live startup; archive enumeration waits
        // until explicit source selection or a completed recording export.
        s.iq_catalog_pending = false;
        s.queueTune(last->channel, last->service);
    }
    s.refresh();
}
} // namespace platform::linux_runtime
