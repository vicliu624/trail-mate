#include "ui/oneseg_tv_app.h"
#include "platform/linux/oneseg_channel_name.h"
#include "platform/linux/oneseg_channel_store.h"
#include "platform/linux/oneseg_event_schedule.h"
#include "platform/linux/oneseg_session_scan_port.h"
#include "platform/linux/runtime_paths.h"
#include "platform/ui/screen_runtime.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/input/pager_navigation.h"
#include "ui/ui_theme.h"
#include "ui/widgets/top_bar.h"
#include <algorithm>
#include <chrono>
#include <cstdio>

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
    std::string save_attempt_name;
    lv_obj_t* broadcast = nullptr;
    std::string broadcast_text;
    lv_obj_t *details_panel = nullptr, *details_body = nullptr, *details_label = nullptr,
             *details_close = nullptr, *details_button = nullptr;
    std::string details_text;
    lv_obj_t *root = nullptr, *title = nullptr, *status = nullptr, *video = nullptr, *image = nullptr,
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
    explicit Impl(std::unique_ptr<OneSegTvSession> value) : session(std::move(value))
    {
        saved.load(notice);
        if (const auto* last = saved.lastChannel()) channel = last->channel;
    }
    static void hidden(lv_obj_t* object, bool value)
    {
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
    void toggle()
    {
        if (!session || scanning()) return;
        const auto state = session->snapshot();
        if (state.active)
        {
            pending_channel = desired_service = 0;
            session->requestStop(state.generation);
        }
        else session->start(channel, 320, 240);
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
        if (scanning()) return;
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
        hidden(bar.container, fullscreen);
        hidden(status, fullscreen);
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
    }
    void refresh()
    {
        if (!root) return;
        if (!session)
        {
            lv_label_set_text(status, "TV receiver unavailable in this runtime mode.");
            for (auto* object : {start_button, channel_down, channel_up, programme, station_down, station_up, scan_button, channel_list, retry_button})
                lv_obj_add_state(object, LV_STATE_DISABLED);
            return;
        }
        refreshScan();
        const bool menu_open = !lv_obj_has_flag(menu_panel, LV_OBJ_FLAG_HIDDEN);
        const bool details_open = !lv_obj_has_flag(details_panel, LV_OBJ_FLAG_HIDDEN);
        hidden(video, menu_open || details_open);
        auto state = session->snapshot();
        refreshBroadcast(state);
        refreshDetails(state);
        if (pending_channel && !state.active)
        {
            const unsigned target = pending_channel;
            pending_channel = 0;
            if (state.receive.lease_restore_failed || state.receive.error == OneSegReceiveWorker::Error::RadioRestore)
                notice = "Radio restoration failed; channel switch cancelled";
            else if (!session->start(target, 320, 240)) notice = "Unable to start selected channel";
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
        lv_label_set_text_fmt(status, "%s%s%s\nService %u | IQ pairs %llu | Video %llu | PCM %llu | %s\nParameters: %s%s%s",
                              phase, state.message[0] ? ": " : "", state.message,
                              state.decoder.programmes.selected_program, static_cast<unsigned long long>(state.receive.forwarded_pairs),
                              static_cast<unsigned long long>(state.media.video_frames), static_cast<unsigned long long>(state.media.pcm_samples), timing,
                              state.automatic_parameters ? "Automatic acquisition" : "OneSegCast mode 3 / GI 1:16 / QPSK 2:3 / TI 4",
                              notice.empty() ? "" : "\n", notice.c_str());
        lv_label_set_text_fmt(channel_label, "CH %u", channel);
        lv_label_set_text_fmt(volume_label, "Volume %u%%", state.volume);
        lv_label_set_text(start_label, state.active ? "Stop" : "Start");
        for (auto* object : {channel_down, channel_up})
        {
            lv_obj_remove_state(object, LV_STATE_DISABLED);
        }
        for (auto* object : {station_down, station_up})
            if (saved.channels().empty()) lv_obj_add_state(object, LV_STATE_DISABLED);
            else lv_obj_remove_state(object, LV_STATE_DISABLED);
        if (!state.active || state.decoder.programmes.program_count < 2) lv_obj_add_state(programme, LV_STATE_DISABLED);
        else lv_obj_remove_state(programme, LV_STATE_DISABLED);
        lv_label_set_text(scan_label, scanning() ? "Cancel scan" : "Scan channels");
        if (scanning() || pending_channel || state.phase == Phase::Stopping || state.receive.lease_restore_failed ||
            state.receive.error == OneSegReceiveWorker::Error::RadioRestore)
            lv_obj_add_state(retry_button, LV_STATE_DISABLED);
        else lv_obj_remove_state(retry_button, LV_STATE_DISABLED);
        if (scanning())
            for (auto* object : {start_button, channel_down, channel_up, programme, station_down, station_up, channel_list})
                lv_obj_add_state(object, LV_STATE_DISABLED);
        else lv_obj_remove_state(start_button, LV_STATE_DISABLED);
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
        lv_label_set_text(waiting, scanning() ? "Scanning channels - audio muted" : signal_interrupted                                                                ? "Signal interrupted\nRetry / R to reacquire"
                                                                                : desired_service && displayed.decoder.programmes.selected_program != desired_service ? "Waiting for selected service"
                                                                                                                                                                      : "No fresh decoded picture");
        const auto& table = displayed.decoder.programmes;
        const auto received_name = oneseg_received_channel_name(displayed.service_text, table.selected_program);
        if (!scanning() && visible && table.pmt_valid && table.selected_program &&
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
        if (root)
        {
            platform::ui::screen::enable_sleep();
            lv_obj_delete(root);
        }
        root = nullptr;
        broadcast = nullptr;
        broadcast_text.clear();
        details_panel = details_body = details_label = details_close = details_button = nullptr;
        details_text.clear();
        menu_panel = channel_list = scan_button = scan_label = nullptr;
        lv_image_cache_drop(&descriptor);
        actions.clear();
        pixels.clear();
        drawn_generation = drawn_epoch = drawn_ordinal = 0;
    }
};
OneSegTvApp::OneSegTvApp(std::unique_ptr<OneSegTvSession> session) : impl_(std::make_unique<Impl>(std::move(session))) {}
OneSegTvApp::~OneSegTvApp() { impl_->close(); }
void OneSegTvApp::exit(lv_obj_t*) { impl_->close(); }
void OneSegTvApp::enter(lv_obj_t* parent)
{
    auto& s = *impl_;
    s.close();
    s.actions.reserve(32);
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
    s.status = Impl::text(s.root, "Ready");
    s.broadcast = Impl::text(s.root, "Station: Not received\nNow: Not received\nNext: Not received");
    lv_obj_set_height(s.broadcast, 54);
    lv_label_set_long_mode(s.broadcast, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s.broadcast, &lv_font_montserrat_14, 0);
    s.video = lv_obj_create(s.root);
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
    ::ui::input::pager::prepare_group(app_g);
    for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
        if (lv_indev_get_type(input) == LV_INDEV_TYPE_KEYPAD) lv_indev_set_group(input, app_g);
    lv_group_focus_obj(s.start_button);
    s.timer = lv_timer_create([](lv_timer_t* timer)
                              { static_cast<Impl*>(lv_timer_get_user_data(timer))->refresh(); },
                              33, &s);
    if (const auto* last = s.saved.lastChannel()) s.queueTune(last->channel, last->service);
    s.refresh();
}
} // namespace platform::linux_runtime
