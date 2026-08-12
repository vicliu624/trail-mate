#if defined(ARDUINO_T_DECK_PRO)

#include "ui/tdeck_pro/text_app_adapter.h"

#include "app/app_facade_access.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat_presentation_adapters/chat_conversation_mapper.h"
#include "esp_random.h"
#include "platform/ui/pack_repository_runtime.h"
#include "platform/ui/sstv_runtime.h"
#include "platform/ui/team_ui_chat_log_store.h"
#include "platform/ui/team_ui_snapshot_store.h"
#include "platform/ui/tracker_runtime.h"
#include "platform/ui/usb_support_runtime.h"
#include "platform/ui/walkie_runtime.h"
#include "sys/clock.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/presentation_sources/chat_presentation_source.h"
#include "ui/presentation_sources/runtime_chat_action_sink.h"
#include "ui/presentation_sources/runtime_device_status_source.h"
#include "ui/presentation_sources/runtime_gps_status_source.h"
#include "ui/presentation_sources/runtime_map_workspace_source.h"
#include "ui/presentation_sources/runtime_mesh_status_source.h"
#include "ui/presentation_sources/runtime_settings_source.h"
#include "ui/presentation_sources/team_chat_action_sink.h"
#include "ui/presentation_sources/team_chat_presentation_source.h"
#include "ui/screens/energy_sweep/energy_sweep_page_runtime.h"
#include "ui/screens/team/team_page_command_reducer.h"
#include "ui/screens/team/team_page_create_team_action.h"
#include "ui/screens/team/team_page_key_event_log.h"
#include "ui/screens/team/team_page_pairing_command_action.h"
#include "ui/screens/team/team_page_runtime_port.h"
#include "ui/tdeck_pro/text_font.h"
#include "ui/tdeck_pro/text_shell.h"
#include "ui/team_actions/team_runtime_adapters.h"
#include "ui_presentation/chat/chat_workspace_model.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace ui::tdeck_pro
{
namespace
{

constexpr lv_coord_t kScreenWidth = 240;
constexpr lv_coord_t kScreenHeight = 320;
constexpr lv_coord_t kMargin = 8;
constexpr lv_coord_t kContentWidth = kScreenWidth - (kMargin * 2);
constexpr lv_coord_t kHeaderRuleY = 32;
constexpr lv_coord_t kBodyTop = 42;
constexpr lv_coord_t kLineHeight = 17;
constexpr lv_coord_t kActionTop = 232;
constexpr lv_coord_t kFooterRuleY = 278;
constexpr lv_coord_t kFooterTop = 280;
constexpr lv_coord_t kButtonTop = 298;
constexpr size_t kMaxLines = 10;
constexpr size_t kMaxActions = 6;

enum class Action : unsigned char
{
    Back,
    Refresh,
    CenterOnSelf,
    ZoomIn,
    ZoomOut,
    ToggleTerrain,
    ToggleGps,
    ToggleTracker,
    ToggleWalkie,
    ToggleWalkieMonitor,
    ToggleSstv,
    ToggleUsb,
    TeamCreate,
    TeamJoin,
    TeamLeave,
    TeamChat,
    ContactsPrevious,
    ContactsNext,
    ContactsOpenChat,
    ContactsToggleView,
    ExtensionsPrevious,
    ExtensionsNext,
    ExtensionsRefresh,
    ExtensionsApply,
    ExtensionsRemove,
    ProbeStartStop,
    ProbePrevious,
    ProbeNext,
    ProbeApply,
    ProbeSync,
    ChatPrevious,
    ChatNext,
    ChatOpen,
    ChatList,
    ChatType,
    ChatSend,
};

struct ActionButton
{
    lv_obj_t* button = nullptr;
    lv_obj_t* label = nullptr;
    Action action = Action::Refresh;
};

struct State
{
    TextAppAdapter* adapter = nullptr;
    lv_obj_t* root = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* subtitle = nullptr;
    lv_obj_t* lines[kMaxLines]{};
    lv_obj_t* footer = nullptr;
    lv_obj_t* compose = nullptr;
    ActionButton actions[kMaxActions]{};
    size_t action_count = 0;
    ::ui::chat::ChatWorkspaceSnapshot chat_snapshot{};
    ::team::ui::TeamUiSnapshot team_snapshot{};
    ::chat::contacts::PeerDirectoryItem contacts_selected_peer{};
    std::vector<::ui::runtime::packs::PackageRecord> extension_packages;
    std::vector<::ui::runtime::packs::InstalledPackageRecord> extension_installed_packages;
    ::ui::runtime::packs::PackageInstallStatus extension_install_status{};
    ::energy_sweep::ui::runtime::TextSnapshot probe_snapshot{};
    std::string extension_error;
    size_t chat_selected_index = 0;
    size_t contacts_selected_index = 0;
    size_t extension_selected_index = 0;
    bool contacts_nearby_view = false;
    bool chat_messages_open = false;
    bool compose_editing = false;
    bool team_chat_open = false;
    bool extensions_seeded = false;
    bool extension_catalog_loaded = false;
    bool extension_install_was_busy = false;
    bool probe_apply_pending = false;
    char scratch[96]{};
    char notice[64]{};
};

State s_state;

std::string s_last_handled_extension_install_id;
::ui::runtime::packs::PackageInstallPhase s_last_handled_extension_install_phase =
    ::ui::runtime::packs::PackageInstallPhase::Idle;

struct TeamActionScratch
{
    ::team::ui::TeamPageCommandState command_state{};
    ::team::ui::TeamPageKeyEventState key_event_state{};
    ::team::ui::TeamPageCreateTeamEffects create_effects{};
    ::team::ui::TeamPagePairingCommandEffects pairing_effects{};
    ::team::ui::TeamPageCommandEffects command_effects{};
};

TeamActionScratch s_team_action_scratch;

struct ChatRuntime
{
    ::chat::ChatService* service = nullptr;
    std::unique_ptr<::ui::presentation_sources::ChatPresentationSource> source;
    std::unique_ptr<::ui::presentation_sources::RuntimeChatActionSink> sink;
    std::unique_ptr<::ui::chat::ChatWorkspaceModel> model;

    ::ui::chat::ChatWorkspaceModel* ensure()
    {
        if (!::app::hasAppFacade())
        {
            return nullptr;
        }

        auto& facade = ::app::messagingFacade();
        ::chat::ChatService& current_service = facade.getChatService();
        if (model && service == &current_service)
        {
            return model.get();
        }

        service = &current_service;
        source = std::unique_ptr<::ui::presentation_sources::ChatPresentationSource>(
            new ::ui::presentation_sources::ChatPresentationSource(
                current_service,
                &facade.getContactService(),
                nullptr,
                facade.getMeshAdapter()));
        sink = std::unique_ptr<::ui::presentation_sources::RuntimeChatActionSink>(
            new ::ui::presentation_sources::RuntimeChatActionSink(current_service));
        model = std::unique_ptr<::ui::chat::ChatWorkspaceModel>(
            new ::ui::chat::ChatWorkspaceModel(*source, *sink));
        return model.get();
    }
};

ChatRuntime s_chat_runtime;

struct ChatRoute
{
    ::ui::chat::ConversationId conversation{};
    bool pending = false;
};

ChatRoute s_chat_route;

struct TeamChatRuntime
{
    ::team::TeamController* controller = nullptr;
    std::unique_ptr<::ui::presentation_sources::TeamChatPresentationSource> source;
    std::unique_ptr<::ui::presentation_sources::ITeamChatCommandPort> command_port;
    std::unique_ptr<::ui::presentation_sources::TeamChatActionSink> sink;
    std::unique_ptr<::ui::chat::ChatWorkspaceModel> model;

    ::ui::chat::ChatWorkspaceModel* ensure()
    {
        if (!::app::hasAppFacade())
        {
            return nullptr;
        }

        ::team::TeamController* const current_controller =
            ::app::teamFacade().getTeamController();
        if (model && controller == current_controller)
        {
            return model.get();
        }

        controller = current_controller;
        source = std::unique_ptr<::ui::presentation_sources::TeamChatPresentationSource>(
            new ::ui::presentation_sources::TeamChatPresentationSource(
                ::team::ui::team_ui_snapshot_store(),
                ::team::ui::team_ui_chat_log_store()));
        command_port.reset();
        if (controller != nullptr)
        {
            command_port = std::unique_ptr<::ui::presentation_sources::ITeamChatCommandPort>(
                new ::ui::team_actions::TeamControllerChatCommandPort(*controller));
        }
        sink = std::unique_ptr<::ui::presentation_sources::TeamChatActionSink>(
            new ::ui::presentation_sources::TeamChatActionSink(
                ::team::ui::team_ui_snapshot_store(),
                ::team::ui::team_ui_chat_log_store(),
                command_port.get()));
        model = std::unique_ptr<::ui::chat::ChatWorkspaceModel>(
            new ::ui::chat::ChatWorkspaceModel(*source, *sink));
        return model.get();
    }
};

TeamChatRuntime s_team_chat_runtime;

class TeamRandom final : public ::team::ui::ITeamPageCreateTeamRandom
{
  public:
    uint8_t nextByte() override
    {
        return static_cast<uint8_t>(esp_random() & 0xFFU);
    }
};

class TeamKeyEventWriter final : public ::team::ui::ITeamPageKeyEventWriter
{
  public:
    bool appendKeyEvent(const ::team::TeamId& team_id,
                        ::team::ui::TeamKeyEventType type,
                        uint32_t event_seq,
                        uint32_t timestamp_s,
                        const uint8_t* payload,
                        size_t payload_size) override
    {
        return ::team::ui::team_ui_append_key_event(
            team_id, type, event_seq, timestamp_s, payload, payload_size);
    }
};

uint32_t team_now_s()
{
    return ::sys::millis_now() / 1000U;
}

void copy_team_command_state(const ::team::ui::TeamUiSnapshot& snapshot,
                             ::team::ui::TeamPageCommandState& state)
{
    state.in_team = snapshot.in_team;
    state.pending_join = snapshot.pending_join;
    state.pending_join_started_s = snapshot.pending_join_started_s;
    state.kicked_out = snapshot.kicked_out;
    state.self_is_leader = snapshot.self_is_leader;
    state.last_event_seq = snapshot.last_event_seq;
    state.team_id = snapshot.team_id;
    state.has_team_id = snapshot.has_team_id;
    state.team_name = snapshot.team_name;
    state.security_round = snapshot.security_round;
    state.last_update_s = snapshot.last_update_s;
    state.team_psk = snapshot.team_psk;
    state.has_team_psk = snapshot.has_team_psk;
    state.members = snapshot.members;
}

void save_team_command_state(const ::team::ui::TeamPageCommandState& state,
                             uint32_t unread)
{
    ::team::ui::TeamUiSnapshot& snapshot = s_state.team_snapshot;
    snapshot.in_team = state.in_team;
    snapshot.pending_join = state.pending_join;
    snapshot.pending_join_started_s = state.pending_join_started_s;
    snapshot.kicked_out = state.kicked_out;
    snapshot.self_is_leader = state.self_is_leader;
    snapshot.last_event_seq = state.last_event_seq;
    snapshot.team_chat_unread = unread;
    snapshot.team_id = state.team_id;
    snapshot.has_team_id = state.has_team_id;
    snapshot.team_name = state.team_name;
    snapshot.security_round = state.security_round;
    snapshot.last_update_s = state.last_update_s;
    snapshot.team_psk = state.team_psk;
    snapshot.has_team_psk = state.has_team_psk;
    snapshot.members = state.members;
    ::team::ui::team_ui_snapshot_store().save(snapshot);
}

::team::ui::TeamPageRuntimePort team_runtime_port()
{
    static ::team::ui::TeamPageKeyStorePortAdapter key_store;
    static ::team::ui::TeamPageControllerPortAdapter controller(nullptr);
    static ::team::ui::TeamPagePairingPortAdapter pairing(nullptr);
    controller = ::team::ui::TeamPageControllerPortAdapter(
        ::app::teamFacade().getTeamController());
    pairing = ::team::ui::TeamPagePairingPortAdapter(
        ::app::teamFacade().getTeamPairing());
    return ::team::ui::TeamPageRuntimePort(&controller, &pairing, &key_store);
}

void apply_team_runtime_effects(const ::team::ui::TeamPageCommandEffects& effects,
                                const ::team::ui::TeamPageRuntimePort& runtime)
{
    if (effects.clear_keys)
    {
        runtime.clearKeys();
    }
    if (effects.reset_controller_ui)
    {
        runtime.resetControllerUi();
    }
    if (effects.stop_pairing)
    {
        runtime.stopPairing();
    }
}

bool valid(const lv_obj_t* object)
{
    return object != nullptr && lv_obj_is_valid(const_cast<lv_obj_t*>(object));
}

void style_paper(lv_obj_t* object)
{
    lv_obj_set_style_bg_color(object, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(object, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* create_text(lv_obj_t* parent, lv_coord_t width, lv_text_align_t align = LV_TEXT_ALIGN_LEFT)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, text_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(label, 0, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

void set_text(lv_obj_t* label, const char* text)
{
    if (!valid(label))
    {
        return;
    }

    const char* const safe = text ? text : "";
    const char* const previous = lv_label_get_text(label);
    if (previous != nullptr && std::strcmp(previous, safe) == 0)
    {
        return;
    }
    lv_label_set_text(label, safe);
    ::ui::fonts::apply_localized_font(label, safe, text_font());
}

void set_line(size_t index, const char* text)
{
    if (index >= kMaxLines)
    {
        return;
    }
    set_text(s_state.lines[index], text);
}

void set_linef(size_t index, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vsnprintf(s_state.scratch, sizeof(s_state.scratch), format, args);
    va_end(args);
    set_line(index, s_state.scratch);
}

void clear_lines_from(size_t index)
{
    for (; index < kMaxLines; ++index)
    {
        set_line(index, "");
    }
}

void set_notice(const char* text)
{
    std::snprintf(s_state.notice, sizeof(s_state.notice), "%s", text ? text : "");
}

const char* state_text(ui::device::CapabilityDisplayState state)
{
    switch (state)
    {
    case ui::device::CapabilityDisplayState::Off:
        return "OFF";
    case ui::device::CapabilityDisplayState::Starting:
        return "STARTING";
    case ui::device::CapabilityDisplayState::Ready:
        return "READY";
    case ui::device::CapabilityDisplayState::Degraded:
        return "DEGRADED";
    case ui::device::CapabilityDisplayState::Error:
        return "ERROR";
    case ui::device::CapabilityDisplayState::Simulated:
        return "SIMULATED";
    case ui::device::CapabilityDisplayState::Unsupported:
    default:
        return "--";
    }
}

void render_map()
{
    static ::ui::presentation_sources::RuntimeMapWorkspaceSource source(
        ::ui::presentation_sources::runtime_gps_status_source(),
        ::ui::presentation_sources::runtime_map_workspace_state(),
        &::team::ui::team_ui_snapshot_store());

    ::ui::map::MapWorkspaceRequest request{};
    const auto& state = ::ui::presentation_sources::runtime_map_workspace_state();
    request.requested_viewport = state.last_viewport;
    request.active_tool = state.active_tool;

    ::ui::map::MapWorkspaceSnapshot map{};
    (void)source.buildMapWorkspaceSnapshot(request, map);

    ::ui::gps::GpsStatusSnapshot gps{};
    (void)::ui::presentation_sources::runtime_gps_status_source().buildGpsStatusSnapshot(gps);

    set_linef(0, "GPS %s  %s", gps.fix_label.c_str(), gps.satellite_label.c_str());
    set_linef(1, "POS %s", gps.coordinate_label.c_str());
    if (gps.has_altitude)
    {
        set_linef(2, "ALT %.0fm  SPD %.1fm/s", static_cast<double>(gps.altitude_m),
                  static_cast<double>(gps.speed_mps));
    }
    else
    {
        set_linef(2, "ALT --  SPD %.1fm/s", static_cast<double>(gps.speed_mps));
    }
    set_linef(3, "MAP %s", map.status_line.c_str());
    set_linef(4, "CENTER %.5f, %.5f", map.viewport.center_lat, map.viewport.center_lon);
    set_linef(5, "ZOOM %u", static_cast<unsigned>(map.viewport.zoom));
    set_linef(6,
              "LAYERS O:%s T:%s C:%s",
              map.layers.osm ? "ON" : "OFF",
              map.layers.terrain ? "ON" : "OFF",
              map.layers.contour ? "ON" : "OFF");
    if (map.team.available)
    {
        set_linef(7,
                  "TEAM %u ONLINE  %u STALE",
                  static_cast<unsigned>(map.team.visible_members),
                  static_cast<unsigned>(map.team.stale_members));
    }
    else
    {
        set_line(7, "TEAM --");
    }
    set_line(8, s_state.notice[0] != '\0' ? s_state.notice : "CENTER / ZOOM / TERRAIN");
    clear_lines_from(9);
}

void render_sky_plot()
{
    ::ui::gps::GpsStatusSnapshot gps{};
    (void)::ui::presentation_sources::runtime_gps_status_source().buildGpsStatusSnapshot(gps);

    set_linef(0, "RECEIVER %s", gps.receiver_enabled ? "ENABLED" : "OFF");
    set_linef(1, "POWER %s", gps.receiver_powered ? "ON" : "OFF");
    set_linef(2, "FIX %s", gps.fix_label.c_str());
    set_linef(3, "SATELLITES %s", gps.satellite_label.c_str());
    set_linef(4, "HDOP %.1f", static_cast<double>(gps.hdop));
    set_linef(5, "TIME %s", gps.time_label.c_str());
    set_linef(6, "POSITION %s", gps.coordinate_label.c_str());
    set_line(7, s_state.notice[0] != '\0' ? s_state.notice : "SATELLITE STATUS VIEW");
    clear_lines_from(8);
}

void render_network()
{
    ::ui::device::DeviceStatusSnapshot device{};
    ::ui::mesh::MeshStatusSnapshot mesh{};
    (void)::ui::presentation_sources::runtime_device_status_source().buildDeviceStatusSnapshot(device);
    (void)::ui::presentation_sources::runtime_mesh_status_source().buildMeshStatusSnapshot(mesh);

    set_linef(0, "PROTOCOL %s", device.active_protocol.c_str());
    set_linef(1, "REGION %s", device.region.c_str());
    set_linef(2, "MODEM %s", device.modem_preset.c_str());
    set_linef(3, "LORA %s", state_text(device.lora));
    set_linef(4, "RADIO %s", mesh.radio_label.c_str());
    set_linef(5, "NODES %lu", static_cast<unsigned long>(mesh.known_nodes));
    set_linef(6, "UNREAD %lu", static_cast<unsigned long>(mesh.unread_messages));
    set_linef(7, "TEAM %s", mesh.team_linked ? "LINKED" : "--");
    set_line(8, s_state.notice[0] != '\0' ? s_state.notice : mesh.status_line.c_str());
    clear_lines_from(9);
}

void render_settings()
{
    ::ui::settings::SettingsSnapshot snapshot{};
    (void)::ui::presentation_sources::runtime_settings_source().buildSettingsSnapshot(snapshot);

    size_t line = 0;
    for (size_t section = 0; section < snapshot.section_count && line < kMaxLines; ++section)
    {
        const auto& current = snapshot.sections[section];
        set_linef(line++, "[%s]", current.title.c_str());
        for (size_t option = 0; option < current.option_count && line < kMaxLines; ++option)
        {
            const auto& item = current.options[option];
            set_linef(line++, "%s : %s", item.label.c_str(), item.value_label.c_str());
        }
    }
    if (line < kMaxLines)
    {
        set_line(line++, s_state.notice[0] != '\0' ? s_state.notice : "SELECT TO TOGGLE GPS");
    }
    clear_lines_from(line);
}

const char* sstv_state_text(::platform::ui::sstv::State state)
{
    switch (state)
    {
    case ::platform::ui::sstv::State::Waiting:
        return "WAITING";
    case ::platform::ui::sstv::State::Receiving:
        return "RECEIVING";
    case ::platform::ui::sstv::State::Complete:
        return "COMPLETE";
    case ::platform::ui::sstv::State::Error:
        return "ERROR";
    case ::platform::ui::sstv::State::Idle:
    default:
        return "IDLE";
    }
}

void render_tracker()
{
    const bool supported = ::platform::ui::tracker::is_supported();
    const bool recording = supported && ::platform::ui::tracker::is_recording();
    std::string path;
    const bool has_path = supported && ::platform::ui::tracker::current_path(path);

    set_linef(0, "TRACKER %s", supported ? "READY" : "UNAVAILABLE");
    set_linef(1, "RECORDING %s", recording ? "ON" : "OFF");
    set_linef(2, "FORMAT GPX / CSV / BIN");
    set_linef(3, "PATH %s", has_path ? path.c_str() : "--");
    set_line(4, s_state.notice[0] != '\0' ? s_state.notice : "SELECT TO START OR STOP");
    clear_lines_from(5);
}

void render_walkie()
{
    const bool supported = ::platform::ui::walkie::is_supported();
    const auto status = ::platform::ui::walkie::get_status();

    set_linef(0, "WALKIE %s", supported ? (status.active ? "ACTIVE" : "READY") : "UNAVAILABLE");
    set_linef(1, "MONITOR %s", status.monitor_enabled ? "ON" : "OFF");
    set_linef(2, "TX %s  RX %u", status.tx ? "ON" : "OFF", static_cast<unsigned>(status.rx_level));
    set_linef(3, "FREQ %.3fMHz", static_cast<double>(status.freq_mhz));
    set_line(4, s_state.notice[0] != '\0' ? s_state.notice : "START / MONITOR CONTROLS");
    clear_lines_from(5);
}

void render_sstv()
{
    const bool supported = ::platform::ui::sstv::is_supported();
    const auto status = ::platform::ui::sstv::get_status();
    const char* path = ::platform::ui::sstv::last_saved_path();

    set_linef(0, "SSTV %s", supported ? (status.state == ::platform::ui::sstv::State::Idle ? "READY" : "ACTIVE") : "UNAVAILABLE");
    set_linef(1, "STATE %s", sstv_state_text(status.state));
    set_linef(2, "MODE %s", ::platform::ui::sstv::mode_name());
    set_linef(3, "PROGRESS %.0f%%  LINE %u", static_cast<double>(status.progress * 100.0f),
              static_cast<unsigned>(status.line));
    set_linef(4, "IMAGE %s", status.has_image ? "READY" : "--");
    set_linef(5, "PATH %s", path && path[0] != '\0' ? path : "--");
    set_line(6, s_state.notice[0] != '\0' ? s_state.notice : "START / STOP RECEIVER");
    clear_lines_from(7);
}

void render_usb_storage()
{
    const bool supported = ::platform::ui::usb_support::is_supported();
    const auto status = ::platform::ui::usb_support::get_status();

    set_linef(0, "USB STORAGE %s", supported ? "READY" : "UNAVAILABLE");
    set_linef(1, "MASS STORAGE %s", status.active ? "ACTIVE" : "OFF");
    set_linef(2, "STOP REQUEST %s", status.stop_requested ? "YES" : "NO");
    set_linef(3, "STATUS %s", status.message && status.message[0] != '\0' ? status.message : "--");
    set_line(4, s_state.notice[0] != '\0' ? s_state.notice : "START / STOP USB DISK");
    clear_lines_from(5);
}

bool is_extension_package(const ::ui::runtime::packs::PackageRecord& package)
{
    return package.package_type.empty() || package.package_type == "installed" ||
           package.package_type == "locale-bundle" ||
           package.package_type == "content-bundle" ||
           package.package_type == "input-bundle";
}

void seed_installed_extensions()
{
    if (s_state.extensions_seeded)
    {
        return;
    }

    s_state.extensions_seeded = true;
    s_state.extension_error.clear();
    s_state.extension_installed_packages.clear();
    if (!::ui::runtime::packs::load_installed_packages(s_state.extension_installed_packages,
                                                       s_state.extension_error))
    {
        return;
    }

    s_state.extension_packages.clear();
    for (const auto& installed : s_state.extension_installed_packages)
    {
        s_state.extension_packages.emplace_back();
        auto& package = s_state.extension_packages.back();
        package.id = installed.id;
        package.package_type = "installed";
        package.version = installed.version;
        package.display_name = installed.id;
        package.installed = true;
        package.compatible_firmware = true;
        package.compatible_memory_profile = true;
        package.installed_record = installed;
    }
}

void sync_extension_install_status()
{
    s_state.extension_install_status = ::ui::runtime::packs::install_status();
    s_state.extension_install_was_busy = s_state.extension_install_status.busy;
    if (s_state.extension_install_status.busy)
    {
        return;
    }

    const auto phase = s_state.extension_install_status.phase;
    if (phase != ::ui::runtime::packs::PackageInstallPhase::Succeeded &&
        phase != ::ui::runtime::packs::PackageInstallPhase::Failed)
    {
        return;
    }
    if (s_last_handled_extension_install_phase == phase &&
        s_last_handled_extension_install_id == s_state.extension_install_status.package_id)
    {
        return;
    }
    s_last_handled_extension_install_phase = phase;
    s_last_handled_extension_install_id = s_state.extension_install_status.package_id;

    if (phase == ::ui::runtime::packs::PackageInstallPhase::Succeeded)
    {
        // Package installation runs on its own worker task.  This user-driven
        // state check is deliberately not a display timer: it is the point at
        // which the new locale/font fallback chain becomes live.
        ::ui::i18n::reload_language();
        s_state.extension_catalog_loaded = false;
        s_state.extensions_seeded = false;
        set_notice("PACKAGE INSTALLED; FONT ACTIVE");
        return;
    }

    if (phase == ::ui::runtime::packs::PackageInstallPhase::Failed)
    {
        set_notice(s_state.extension_install_status.message.empty()
                       ? "PACKAGE INSTALL FAILED"
                       : s_state.extension_install_status.message.c_str());
    }
}

void load_extension_catalog()
{
    sync_extension_install_status();
    s_state.extension_catalog_loaded = false;
    if (!::ui::runtime::packs::is_supported())
    {
        s_state.extension_error = "PACKAGE REPOSITORY UNSUPPORTED";
        set_notice(s_state.extension_error.c_str());
        return;
    }

    s_state.extension_error.clear();
    s_state.extension_packages.clear();
    if (!::ui::runtime::packs::fetch_catalog(s_state.extension_packages, s_state.extension_error))
    {
        s_state.extensions_seeded = false;
        seed_installed_extensions();
        set_notice(s_state.extension_error.empty() ? "CATALOG LOAD FAILED"
                                                   : s_state.extension_error.c_str());
        return;
    }

    s_state.extension_packages.erase(
        std::remove_if(s_state.extension_packages.begin(),
                       s_state.extension_packages.end(),
                       [](const ::ui::runtime::packs::PackageRecord& package)
                       { return !is_extension_package(package); }),
        s_state.extension_packages.end());
    s_state.extension_catalog_loaded = true;
    if (s_state.extension_packages.empty())
    {
        s_state.extension_selected_index = 0;
        set_notice("NO EXTENSION PACKAGES");
        return;
    }
    if (s_state.extension_selected_index >= s_state.extension_packages.size())
    {
        s_state.extension_selected_index = s_state.extension_packages.size() - 1U;
    }
    set_notice("CATALOG UPDATED");
}

const ::ui::runtime::packs::PackageRecord* selected_extension()
{
    if (s_state.extension_selected_index >= s_state.extension_packages.size())
    {
        return nullptr;
    }
    return &s_state.extension_packages[s_state.extension_selected_index];
}

void render_extensions()
{
    seed_installed_extensions();
    sync_extension_install_status();
    // Completion invalidates the cached package records. Reload the durable
    // installed index now, as part of this explicit render, rather than
    // scheduling a background EPD refresh.
    seed_installed_extensions();

    if (!::ui::runtime::packs::is_supported())
    {
        set_line(0, "PACKAGE REPOSITORY UNSUPPORTED");
        set_line(1, "THIS TARGET HAS NO PACK STORAGE");
        clear_lines_from(2);
        return;
    }

    set_linef(0,
              "PACKS %u  CATALOG %s",
              static_cast<unsigned>(s_state.extension_packages.size()),
              s_state.extension_catalog_loaded ? "LOADED" : "OFFLINE");
    const ::ui::runtime::packs::PackageRecord* const package = selected_extension();
    if (package == nullptr)
    {
        set_line(1, "NO PACKAGE SELECTED");
        set_line(2, "RELOAD NEEDS WI-FI");
        set_line(3,
                 s_state.extension_error.empty() ? "INSTALLED PACKS SHOWN OFFLINE"
                                                 : s_state.extension_error.c_str());
        set_line(4, s_state.notice[0] != '\0' ? s_state.notice : "RELOAD TO FETCH CATALOG");
        clear_lines_from(5);
        return;
    }

    set_linef(1,
              "SELECT %u / %u",
              static_cast<unsigned>(s_state.extension_selected_index + 1U),
              static_cast<unsigned>(s_state.extension_packages.size()));
    set_linef(2, "NAME %s", package->display_name.empty() ? package->id.c_str() : package->display_name.c_str());
    set_linef(3, "ID %s", package->id.c_str());
    set_linef(4, "VERSION %s", package->version.empty() ? "--" : package->version.c_str());
    if (!package->compatible_firmware || !package->compatible_memory_profile)
    {
        set_line(5, "STATE INCOMPATIBLE");
    }
    else if (package->update_available)
    {
        set_line(5, "STATE UPDATE AVAILABLE");
    }
    else
    {
        set_line(5, package->installed ? "STATE INSTALLED" : "STATE AVAILABLE");
    }
    set_linef(6,
              "LOCALE %u FONT %u IME %u",
              static_cast<unsigned>(package->provided_locale_ids.size()),
              static_cast<unsigned>(package->provided_font_ids.size()),
              static_cast<unsigned>(package->provided_ime_ids.size()));
    if (s_state.extension_install_status.busy)
    {
        set_linef(7,
                  "INSTALL %d%% %s",
                  s_state.extension_install_status.progress_percent,
                  s_state.extension_install_status.message.c_str());
    }
    else if (!s_state.extension_install_status.message.empty())
    {
        set_linef(7, "INSTALL %s", s_state.extension_install_status.message.c_str());
    }
    else
    {
        set_line(7, "INSTALL IDLE");
    }
    set_line(8, s_state.extension_error.empty() ? "FONT PACKS ACTIVATE AFTER INSTALL"
                                                : s_state.extension_error.c_str());
    set_line(9,
             s_state.notice[0] != '\0'
                 ? s_state.notice
                 : "PREV/NEXT SELECT  RELOAD / INSTALL");
}

void render_protocol_probe()
{
    if (!::energy_sweep::ui::runtime::text_snapshot(s_state.probe_snapshot))
    {
        set_line(0, "PROBE RUNTIME UNAVAILABLE");
        set_line(1, "CHECK RADIO SUPPORT");
        set_line(2, s_state.notice[0] != '\0' ? s_state.notice : "BACK TO RETURN");
        clear_lines_from(3);
        return;
    }

    const auto& probe = s_state.probe_snapshot;
    set_linef(0, "RADIO %s", probe.available ? "READY" : "UNAVAILABLE");
    set_linef(1, "STATE %s", probe.status);
    set_linef(2, "PROFILE %s", probe.current_profile[0] != '\0' ? probe.current_profile : "--");
    set_linef(3,
              "STEP %lu / %lu  PASS %lu",
              static_cast<unsigned long>(probe.candidate_index + 1U),
              static_cast<unsigned long>(probe.candidate_count),
              static_cast<unsigned long>(probe.completed_passes));
    set_linef(4,
              "FOUND %lu  EVIDENCE %lu",
              static_cast<unsigned long>(probe.observation_count),
              static_cast<unsigned long>(probe.evidence_count));
    set_linef(5, "CRC FRAMES %lu", static_cast<unsigned long>(probe.crc_frame_count));
    set_linef(6,
              "SELECTED %s",
              probe.selected_profile[0] != '\0' ? probe.selected_profile : "--");
    set_line(7, probe.scanning ? "RADIO SCANS; DISPLAY STAYS QUIET" : "START TO PROBE KNOWN PROFILES");
    set_line(8, "SYNC UPDATES THIS PAPER VIEW");
    set_line(9,
             s_state.notice[0] != '\0'
                 ? s_state.notice
                 : (probe.has_selection ? "APPLY NEEDS TWO CONFIRMED PRESSES" : "WAIT FOR EVIDENCE"));
}

const char* pairing_state_text(::team::TeamPairingState state)
{
    switch (state)
    {
    case ::team::TeamPairingState::LeaderBeacon:
        return "WAITING FOR MEMBER";
    case ::team::TeamPairingState::MemberScanning:
        return "SCANNING FOR TEAM";
    case ::team::TeamPairingState::JoinSent:
        return "JOIN REQUEST SENT";
    case ::team::TeamPairingState::WaitingKey:
        return "WAITING FOR KEYS";
    case ::team::TeamPairingState::Completed:
        return "PAIRING COMPLETE";
    case ::team::TeamPairingState::Failed:
        return "PAIRING FAILED";
    case ::team::TeamPairingState::Idle:
    default:
        return "IDLE";
    }
}

void render_team()
{
    if (!::team::ui::team_ui_snapshot_store().load(s_state.team_snapshot))
    {
        set_line(0, "TEAM DATA UNAVAILABLE");
        set_line(1, "WAIT FOR TEAM RUNTIME");
        set_line(2, s_state.notice[0] != '\0' ? s_state.notice : "REFRESH TO RETRY");
        clear_lines_from(3);
        return;
    }

    const ::team::ui::TeamUiSnapshot& snapshot = s_state.team_snapshot;

    if (!snapshot.in_team && !snapshot.pending_join)
    {
        set_line(0, snapshot.kicked_out ? "TEAM MEMBERSHIP ENDED" : "NOT IN A TEAM");
        set_line(1, "CREATE A LOCAL TEAM OR JOIN");
        set_line(2, "PAIRING USES THE EXISTING RADIO FLOW");
        set_line(3, s_state.notice[0] != '\0' ? s_state.notice : "CREATE / JOIN");
        clear_lines_from(4);
        return;
    }

    const char* const title = snapshot.team_name.empty() ? "TEAM" : snapshot.team_name.c_str();
    set_linef(0, "TEAM %s", title);
    set_linef(1,
              "ROLE %s  MEMBERS %u",
              snapshot.self_is_leader ? "LEADER" : "MEMBER",
              static_cast<unsigned>(snapshot.members.size()));
    set_linef(2,
              "KEYS %s  ROUND %lu",
              snapshot.has_team_psk ? "READY" : "WAITING",
              static_cast<unsigned long>(snapshot.security_round));
    set_linef(3, "UNREAD %lu", static_cast<unsigned long>(snapshot.team_chat_unread));
    if (snapshot.pending_join)
    {
        const ::team::TeamPairingStatus pairing = ::app::teamFacade().getTeamPairing() != nullptr
                                                      ? ::app::teamFacade().getTeamPairing()->getStatus()
                                                      : ::team::TeamPairingStatus{};
        set_linef(4, "PAIRING %s", pairing_state_text(pairing.state));
    }
    else
    {
        set_line(4, "STATUS ACTIVE");
    }

    size_t line = 5;
    for (const auto& member : snapshot.members)
    {
        if (line >= 9)
        {
            break;
        }
        const char* const name = member.name.empty() ? "UNKNOWN" : member.name.c_str();
        set_linef(line++,
                  "%c %s %s",
                  member.leader ? '*' : ' ',
                  name,
                  member.online ? "ONLINE" : "OFFLINE");
    }
    while (line < 9)
    {
        set_line(line++, "");
    }
    set_line(9, s_state.notice[0] != '\0' ? s_state.notice : "CHAT / LEAVE");
}

::ui::chat::ChatWorkspaceModel* active_chat_model()
{
    if (s_state.adapter != nullptr && s_state.adapter->page_kind() == TextAppPageKind::Team &&
        s_state.team_chat_open)
    {
        return s_team_chat_runtime.ensure();
    }
    return s_chat_runtime.ensure();
}

void render_team_chat()
{
    ::ui::chat::ChatWorkspaceModel* const model = s_team_chat_runtime.ensure();
    if (model == nullptr || !model->buildSnapshot(s_state.chat_snapshot))
    {
        set_line(0, "TEAM CHAT UNAVAILABLE");
        set_line(1, "WAIT FOR TEAM RUNTIME");
        set_line(2, s_state.notice[0] != '\0' ? s_state.notice : "TEAM TO RETURN");
        clear_lines_from(3);
        return;
    }

    if (s_state.chat_snapshot.conversation_count == 0)
    {
        set_line(0, "NO ACTIVE TEAM CHAT");
        set_line(1, "CREATE OR JOIN A TEAM FIRST");
        set_line(2, s_state.notice[0] != '\0' ? s_state.notice : "TEAM TO RETURN");
        clear_lines_from(3);
        return;
    }

    const ::ui::chat::ConversationId id = s_state.chat_snapshot.conversations[0].id;
    if (!s_state.chat_snapshot.conversations[0].selected)
    {
        if (!model->selectConversation(id).ok)
        {
            set_line(0, "TEAM CHAT UNAVAILABLE");
            set_line(1, "CONVERSATION NOT READY");
            clear_lines_from(2);
            return;
        }
        (void)model->markRead(id);
        (void)model->buildSnapshot(s_state.chat_snapshot);
    }

    const auto& snapshot = s_state.chat_snapshot;
    set_linef(0, "TEAM CHAT %s", snapshot.workspace_title.c_str());
    size_t line = 1;
    for (size_t index = 0; index < snapshot.message_count && line < 9; ++index)
    {
        const auto& message = snapshot.messages[index];
        set_linef(line++,
                  "%c %s: %s",
                  message.outgoing ? '>' : '<',
                  message.sender_label.c_str(),
                  message.text.c_str());
    }
    if (snapshot.message_count == 0 && line < 9)
    {
        set_line(line++, "NO TEAM MESSAGES");
    }
    while (line < 9)
    {
        set_line(line++, "");
    }
    set_line(9,
             s_state.notice[0] != '\0'
                 ? s_state.notice
                 : (s_state.compose_editing ? "TYPE TEXT, THEN SELECT SEND" : "TYPE THEN SEND"));
}

const char* contact_display_name(const ::chat::contacts::PeerDirectoryItem& peer)
{
    if (!peer.display_name.empty())
    {
        return peer.display_name.c_str();
    }
    if (peer.long_name[0] != '\0')
    {
        return peer.long_name;
    }
    if (peer.short_name[0] != '\0')
    {
        return peer.short_name;
    }
    return "UNKNOWN";
}

void render_contacts()
{
    if (!::app::hasAppFacade())
    {
        set_line(0, "CONTACT SERVICE UNAVAILABLE");
        set_line(1, "WAIT FOR MESSAGING RUNTIME");
        clear_lines_from(2);
        return;
    }

    const ::chat::contacts::ContactService& service =
        ::app::messagingFacade().getContactService();
    const std::vector<::chat::contacts::PeerDirectoryItem>& peers =
        s_state.contacts_nearby_view ? service.getNearby() : service.getContacts();
    const char* const view = s_state.contacts_nearby_view ? "NEARBY" : "CONTACTS";
    set_linef(0, "%s %u", view, static_cast<unsigned>(peers.size()));

    if (peers.empty())
    {
        set_line(1,
                 s_state.contacts_nearby_view ? "NO NEARBY NODES" : "NO SAVED CONTACTS");
        set_line(2, "VIEW TO SWITCH LIST");
        set_line(3, s_state.notice[0] != '\0' ? s_state.notice : "REFRESH TO RETRY");
        clear_lines_from(4);
        return;
    }

    if (s_state.contacts_selected_index >= peers.size())
    {
        s_state.contacts_selected_index = peers.size() - 1U;
    }
    set_linef(1,
              "SELECT %u / %u",
              static_cast<unsigned>(s_state.contacts_selected_index + 1U),
              static_cast<unsigned>(peers.size()));

    size_t line = 2;
    for (size_t index = 0; index < peers.size() && line < 9; ++index)
    {
        const auto& peer = peers[index];
        set_linef(line++,
                  "%c %s %s %s",
                  index == s_state.contacts_selected_index ? '>' : ' ',
                  contact_display_name(peer),
                  peer.is_contact ? "SAVED" : "NODE",
                  peer.is_ignored ? "IGN" : "");
    }
    while (line < 9)
    {
        set_line(line++, "");
    }
    set_line(9,
             s_state.notice[0] != '\0'
                 ? s_state.notice
                 : "PREV/NEXT SELECT  CHAT OPENS MESSAGE");
}

bool select_contact()
{
    if (!::app::hasAppFacade())
    {
        return false;
    }

    const ::chat::contacts::ContactService& service =
        ::app::messagingFacade().getContactService();
    const std::vector<::chat::contacts::PeerDirectoryItem>& peers =
        s_state.contacts_nearby_view ? service.getNearby() : service.getContacts();
    if (peers.empty() || s_state.contacts_selected_index >= peers.size())
    {
        return false;
    }
    s_state.contacts_selected_peer = peers[s_state.contacts_selected_index];
    return true;
}

void set_action(size_t index, const char* label, Action action)
{
    if (index >= s_state.action_count)
    {
        return;
    }

    ActionButton& entry = s_state.actions[index];
    entry.action = action;
    set_text(entry.label, ::ui::i18n::tr(label));
}

void set_action_visible(size_t index, bool visible)
{
    if (index >= s_state.action_count || !valid(s_state.actions[index].button))
    {
        return;
    }

    ActionButton& entry = s_state.actions[index];
    if (visible)
    {
        lv_obj_clear_flag(entry.button, LV_OBJ_FLAG_HIDDEN);
        if (app_g != nullptr && lv_obj_get_group(entry.button) != app_g)
        {
            lv_group_add_obj(app_g, entry.button);
        }
        return;
    }
    if (app_g != nullptr && lv_obj_get_group(entry.button) == app_g)
    {
        lv_group_remove_obj(entry.button);
    }
    lv_obj_add_flag(entry.button, LV_OBJ_FLAG_HIDDEN);
}

void focus_action(size_t index)
{
    if (index < s_state.action_count && valid(s_state.actions[index].button))
    {
        lv_group_focus_obj(s_state.actions[index].button);
    }
}

void hide_compose()
{
    if (!valid(s_state.compose))
    {
        return;
    }

    lv_obj_add_flag(s_state.compose, LV_OBJ_FLAG_HIDDEN);
    if (s_state.compose_editing && app_g != nullptr)
    {
        lv_group_remove_obj(s_state.compose);
    }
    s_state.compose_editing = false;
}

void render_chat()
{
    ::ui::chat::ChatWorkspaceModel* const model = s_chat_runtime.ensure();
    if (model != nullptr && s_chat_route.pending)
    {
        const ::ui::chat::ConversationId route = s_chat_route.conversation;
        s_chat_route = ChatRoute{};
        if (model->selectConversation(route).ok)
        {
            (void)model->markRead(route);
            s_state.chat_messages_open = true;
            set_notice("CONTACT CONVERSATION OPEN");
        }
        else
        {
            set_notice("CONVERSATION UNAVAILABLE");
        }
    }
    if (model == nullptr || !model->buildSnapshot(s_state.chat_snapshot))
    {
        set_line(0, "CHAT SERVICE UNAVAILABLE");
        set_line(1, "WAIT FOR MESSAGING RUNTIME");
        set_line(2, s_state.notice[0] != '\0' ? s_state.notice : "");
        clear_lines_from(3);
        return;
    }

    const auto& snapshot = s_state.chat_snapshot;
    if (!s_state.chat_messages_open)
    {
        if (snapshot.conversation_count == 0)
        {
            set_line(0, "NO CONVERSATIONS");
            set_line(1, "RADIO MESSAGES APPEAR HERE");
            set_line(2, s_state.notice[0] != '\0' ? s_state.notice : "OPEN AFTER A MESSAGE ARRIVES");
            clear_lines_from(3);
            return;
        }

        if (s_state.chat_selected_index >= snapshot.conversation_count)
        {
            s_state.chat_selected_index = snapshot.conversation_count - 1;
        }
        set_linef(0,
                  "CONVERSATIONS %u  SELECT %u",
                  static_cast<unsigned>(snapshot.conversation_count),
                  static_cast<unsigned>(s_state.chat_selected_index + 1U));

        size_t line = 1;
        for (size_t index = 0; index < snapshot.conversation_count && line < 9; ++index)
        {
            const auto& conversation = snapshot.conversations[index];
            set_linef(line++,
                      "%c %02u %s %s%s",
                      index == s_state.chat_selected_index ? '>' : ' ',
                      static_cast<unsigned>(index + 1U),
                      conversation.title.c_str(),
                      conversation.unread_count > 0 ? "NEW " : "",
                      conversation.subtitle.c_str());
        }
        set_line(9, s_state.notice[0] != '\0' ? s_state.notice : "PREV/NEXT SELECT  OPEN READ");
        return;
    }

    set_linef(0, "CHAT %s", snapshot.workspace_title.c_str());
    size_t line = 1;
    for (size_t index = 0; index < snapshot.message_count && line < 9; ++index)
    {
        const auto& message = snapshot.messages[index];
        set_linef(line++,
                  "%c %s: %s",
                  message.outgoing ? '>' : '<',
                  message.sender_label.c_str(),
                  message.text.c_str());
    }
    if (snapshot.message_count == 0 && line < 9)
    {
        set_line(line++, "NO MESSAGES IN THIS CONVERSATION");
    }
    while (line < 9)
    {
        set_line(line++, "");
    }
    set_line(9,
             s_state.notice[0] != '\0'
                 ? s_state.notice
                 : (s_state.compose_editing ? "TYPE TEXT, THEN SELECT SEND" : "TYPE THEN SEND"));
}

void configure_chat_actions()
{
    if (s_state.adapter == nullptr || s_state.adapter->page_kind() != TextAppPageKind::Chat ||
        s_state.action_count < 5)
    {
        return;
    }

    if (s_state.chat_messages_open)
    {
        set_action(0, "OLDER", Action::ChatPrevious);
        set_action(1, "NEWER", Action::ChatNext);
        set_action(2, "TYPE", Action::ChatType);
        set_action(3, "SEND", Action::ChatSend);
        set_action(4, "LIST", Action::ChatList);
        return;
    }

    set_action(0, "PREV", Action::ChatPrevious);
    set_action(1, "NEXT", Action::ChatNext);
    set_action(2, "OPEN", Action::ChatOpen);
    set_action(3, "SYNC", Action::Refresh);
    set_action(4, "BACK", Action::Back);
}

void configure_team_actions()
{
    if (s_state.adapter == nullptr || s_state.adapter->page_kind() != TextAppPageKind::Team ||
        s_state.action_count < 5)
    {
        return;
    }

    if (s_state.team_chat_open)
    {
        set_action(0, "OLDER", Action::ChatPrevious);
        set_action(1, "NEWER", Action::ChatNext);
        set_action(2, "TYPE", Action::ChatType);
        set_action(3, "SEND", Action::ChatSend);
        set_action(4, "TEAM", Action::ChatList);
        for (size_t index = 0; index < 5; ++index)
        {
            set_action_visible(index, true);
        }
        return;
    }

    const bool joined = s_state.team_snapshot.in_team || s_state.team_snapshot.pending_join;
    set_action(0, joined ? "CHAT" : "CREATE", joined ? Action::TeamChat : Action::TeamCreate);
    set_action(1, joined ? "LEAVE" : "JOIN", joined ? Action::TeamLeave : Action::TeamJoin);
    set_action(2, "REFRESH", Action::Refresh);
    set_action(3, "BACK", Action::Back);
    for (size_t index = 0; index < 4; ++index)
    {
        set_action_visible(index, true);
    }
    set_action_visible(4, false);
}

void configure_contacts_actions()
{
    if (s_state.adapter == nullptr || s_state.adapter->page_kind() != TextAppPageKind::Contacts ||
        s_state.action_count < 5)
    {
        return;
    }

    set_action(0, "PREV", Action::ContactsPrevious);
    set_action(1, "NEXT", Action::ContactsNext);
    set_action(2, "CHAT", Action::ContactsOpenChat);
    set_action(3, "VIEW", Action::ContactsToggleView);
    set_action(4, "BACK", Action::Back);
    for (size_t index = 0; index < 5; ++index)
    {
        set_action_visible(index, true);
    }
}

void configure_extensions_actions()
{
    if (s_state.adapter == nullptr || s_state.adapter->page_kind() != TextAppPageKind::Extensions ||
        s_state.action_count < 5)
    {
        return;
    }

    const ::ui::runtime::packs::PackageRecord* const package = selected_extension();
    const bool can_apply = package != nullptr && package->compatible_firmware &&
                           package->compatible_memory_profile &&
                           (!package->installed || package->update_available) &&
                           !s_state.extension_install_status.busy;
    set_action(0, "PREV", Action::ExtensionsPrevious);
    set_action(1, "NEXT", Action::ExtensionsNext);
    set_action(2, "RELOAD", Action::ExtensionsRefresh);
    if (package != nullptr && package->installed && !package->update_available)
    {
        set_action(3, "REMOVE", Action::ExtensionsRemove);
    }
    else
    {
        set_action(3,
                   can_apply && package->update_available ? "UPDATE" : "INSTALL",
                   Action::ExtensionsApply);
    }
    set_action(4, "BACK", Action::Back);
    for (size_t index = 0; index < 5; ++index)
    {
        set_action_visible(index, true);
    }
}

void configure_protocol_probe_actions()
{
    if (s_state.adapter == nullptr || s_state.adapter->page_kind() != TextAppPageKind::ProtocolProbe ||
        s_state.action_count < 6)
    {
        return;
    }

    set_action(0,
               s_state.probe_snapshot.scanning ? "STOP" : "START",
               Action::ProbeStartStop);
    set_action(1, "PREV", Action::ProbePrevious);
    set_action(2, "NEXT", Action::ProbeNext);
    set_action(3, s_state.probe_apply_pending ? "CONFIRM" : "APPLY", Action::ProbeApply);
    set_action(4, "SYNC", Action::ProbeSync);
    set_action(5, "BACK", Action::Back);
    for (size_t index = 0; index < 6; ++index)
    {
        set_action_visible(index, true);
    }
}

void refresh_page()
{
    if (s_state.adapter == nullptr || !valid(s_state.root))
    {
        return;
    }

    switch (s_state.adapter->page_kind())
    {
    case TextAppPageKind::Map:
        render_map();
        break;
    case TextAppPageKind::SkyPlot:
        render_sky_plot();
        break;
    case TextAppPageKind::Network:
        render_network();
        break;
    case TextAppPageKind::Settings:
        render_settings();
        break;
    case TextAppPageKind::Tracker:
        render_tracker();
        break;
    case TextAppPageKind::Walkie:
        render_walkie();
        break;
    case TextAppPageKind::Sstv:
        render_sstv();
        break;
    case TextAppPageKind::UsbStorage:
        render_usb_storage();
        break;
    case TextAppPageKind::Chat:
        render_chat();
        configure_chat_actions();
        break;
    case TextAppPageKind::Team:
        if (s_state.team_chat_open)
        {
            render_team_chat();
        }
        else
        {
            render_team();
        }
        configure_team_actions();
        break;
    case TextAppPageKind::Contacts:
        render_contacts();
        configure_contacts_actions();
        break;
    case TextAppPageKind::Extensions:
        render_extensions();
        configure_extensions_actions();
        break;
    case TextAppPageKind::ProtocolProbe:
        render_protocol_probe();
        configure_protocol_probe_actions();
        break;
    }
}

void apply_button_focus(ActionButton& action, bool focused)
{
    if (!valid(action.button) || !valid(action.label))
    {
        return;
    }

    lv_obj_set_style_bg_color(action.button, focused ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(action.label, focused ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
}

ActionButton* action_for(lv_obj_t* button)
{
    for (size_t index = 0; index < s_state.action_count; ++index)
    {
        if (s_state.actions[index].button == button)
        {
            return &s_state.actions[index];
        }
    }
    return nullptr;
}

void run_action(Action action)
{
    switch (action)
    {
    case Action::Back:
        ui_request_exit_to_menu();
        return;
    case Action::Refresh:
        set_notice("UPDATED");
        break;
    case Action::CenterOnSelf:
    {
        static ::ui::presentation_sources::RuntimeMapActionSink sink(
            ::ui::presentation_sources::runtime_gps_status_source(),
            ::ui::presentation_sources::runtime_map_workspace_state());
        set_notice(sink.centerOnSelf().ok ? "CENTERED ON SELF" : "NO GPS FIX");
        break;
    }
    case Action::ZoomIn:
    case Action::ZoomOut:
    {
        static ::ui::presentation_sources::RuntimeMapActionSink sink(
            ::ui::presentation_sources::runtime_gps_status_source(),
            ::ui::presentation_sources::runtime_map_workspace_state());
        auto& state = ::ui::presentation_sources::runtime_map_workspace_state();
        ::ui::map::MapViewport viewport = state.last_viewport;
        if (viewport.zoom == 0)
        {
            viewport.zoom = 12;
        }
        if (action == Action::ZoomIn && viewport.zoom < 22)
        {
            ++viewport.zoom;
        }
        if (action == Action::ZoomOut && viewport.zoom > 1)
        {
            --viewport.zoom;
        }
        set_notice(sink.setViewport(viewport).ok ? "MAP ZOOM UPDATED" : "ZOOM UNAVAILABLE");
        break;
    }
    case Action::ToggleTerrain:
    {
        static ::ui::presentation_sources::RuntimeMapActionSink sink(
            ::ui::presentation_sources::runtime_gps_status_source(),
            ::ui::presentation_sources::runtime_map_workspace_state());
        auto& state = ::ui::presentation_sources::runtime_map_workspace_state();
        const bool enabled = !state.layers.terrain;
        set_notice(sink.setLayer(::ui::map::MapLayerKind::Terrain, enabled).ok
                       ? (enabled ? "TERRAIN ON" : "TERRAIN OFF")
                       : "LAYER UNAVAILABLE");
        break;
    }
    case Action::ToggleGps:
    {
        ::ui::settings::SettingsSnapshot settings{};
        (void)::ui::presentation_sources::runtime_settings_source().buildSettingsSnapshot(settings);
        bool enabled = false;
        if (settings.section_count > 0 && settings.sections[0].option_count > 0)
        {
            enabled = std::strcmp(settings.sections[0].options[0].value_label.c_str(), "ON") == 0;
        }
        ::ui::settings::SettingsPatchView patch{};
        ::ui::copyText(patch.key, "gps_enabled");
        ::ui::copyText(patch.value, enabled ? "OFF" : "ON");
        set_notice(::ui::presentation_sources::runtime_settings_action_sink().applySetting(patch).ok
                       ? (enabled ? "GPS DISABLED" : "GPS ENABLED")
                       : "GPS CHANGE REJECTED");
        break;
    }
    case Action::ToggleTracker:
        if (!::platform::ui::tracker::is_supported())
        {
            set_notice("TRACKER UNAVAILABLE");
        }
        else if (::platform::ui::tracker::is_recording())
        {
            ::platform::ui::tracker::stop_recording();
            set_notice("TRACK RECORDING STOPPED");
        }
        else
        {
            set_notice(::platform::ui::tracker::start_recording() ? "TRACK RECORDING STARTED"
                                                                  : "TRACKER START FAILED");
        }
        break;
    case Action::ToggleWalkie:
        if (!::platform::ui::walkie::is_supported())
        {
            set_notice("WALKIE UNAVAILABLE");
        }
        else if (::platform::ui::walkie::is_active())
        {
            ::platform::ui::walkie::stop();
            set_notice("WALKIE STOPPED");
        }
        else
        {
            set_notice(::platform::ui::walkie::start() ? "WALKIE STARTED" : "WALKIE START FAILED");
        }
        break;
    case Action::ToggleWalkieMonitor:
    {
        const auto status = ::platform::ui::walkie::get_status();
        set_notice(::platform::ui::walkie::set_monitor_enabled(!status.monitor_enabled)
                       ? (!status.monitor_enabled ? "MONITOR ON" : "MONITOR OFF")
                       : "MONITOR CHANGE FAILED");
        break;
    }
    case Action::ToggleSstv:
        if (!::platform::ui::sstv::is_supported())
        {
            set_notice("SSTV UNAVAILABLE");
        }
        else if (::platform::ui::sstv::is_active())
        {
            ::platform::ui::sstv::stop();
            set_notice("SSTV STOPPED");
        }
        else
        {
            set_notice(::platform::ui::sstv::start() ? "SSTV STARTED" : "SSTV START FAILED");
        }
        break;
    case Action::ToggleUsb:
    {
        const auto status = ::platform::ui::usb_support::get_status();
        if (!::platform::ui::usb_support::is_supported())
        {
            set_notice("USB STORAGE UNAVAILABLE");
        }
        else if (status.active)
        {
            ::platform::ui::usb_support::stop();
            set_notice("USB STORAGE STOP REQUESTED");
        }
        else
        {
            set_notice(::platform::ui::usb_support::start() ? "USB STORAGE STARTED"
                                                            : "USB STORAGE START FAILED");
        }
        break;
    }
    case Action::TeamCreate:
    {
        if (!::app::hasAppFacade())
        {
            set_notice("TEAM RUNTIME UNAVAILABLE");
            break;
        }

        if (!::team::ui::team_ui_snapshot_store().load(s_state.team_snapshot))
        {
            s_state.team_snapshot = ::team::ui::TeamUiSnapshot{};
        }
        copy_team_command_state(s_state.team_snapshot, s_team_action_scratch.command_state);
        s_team_action_scratch.key_event_state.team_id = s_team_action_scratch.command_state.team_id;
        s_team_action_scratch.key_event_state.has_team_id =
            s_team_action_scratch.command_state.has_team_id;
        s_team_action_scratch.key_event_state.last_event_seq =
            s_team_action_scratch.command_state.last_event_seq;
        s_team_action_scratch.key_event_state.security_round =
            s_team_action_scratch.command_state.security_round;

        const ::team::ui::TeamPageCommandContext context{
            team_now_s(), ::app::messagingFacade().getSelfNodeId()};
        const ::team::ui::TeamPageCommandReducer reducer(context);
        const ::team::ui::TeamPageRuntimePort runtime = team_runtime_port();
        static TeamKeyEventWriter event_writer;
        static TeamRandom random;
        const ::team::ui::TeamPageKeyEventLog event_log(event_writer, team_now_s());
        s_team_action_scratch.create_effects = ::team::ui::TeamPageCreateTeamAction().createTeam(
            s_team_action_scratch.command_state,
            s_team_action_scratch.key_event_state,
            reducer,
            runtime,
            event_log,
            random,
            ::app::messagingFacade().getSelfNodeId());
        save_team_command_state(s_team_action_scratch.command_state,
                                s_state.team_snapshot.team_chat_unread);
        if (s_team_action_scratch.create_effects.command.clear_keys ||
            s_team_action_scratch.create_effects.command.reset_controller_ui ||
            s_team_action_scratch.create_effects.command.stop_pairing)
        {
            apply_team_runtime_effects(s_team_action_scratch.create_effects.command, runtime);
        }
        set_notice(s_team_action_scratch.create_effects.accepted
                       ? (s_team_action_scratch.create_effects.started_pairing
                              ? "TEAM CREATED; INVITE BEACON ACTIVE"
                              : "TEAM CREATED")
                       : "TEAM CREATE REJECTED");
        break;
    }
    case Action::TeamJoin:
    {
        if (!::app::hasAppFacade())
        {
            set_notice("TEAM RUNTIME UNAVAILABLE");
            break;
        }

        if (!::team::ui::team_ui_snapshot_store().load(s_state.team_snapshot))
        {
            s_state.team_snapshot = ::team::ui::TeamUiSnapshot{};
        }
        copy_team_command_state(s_state.team_snapshot, s_team_action_scratch.command_state);
        const ::team::ui::TeamPageCommandContext context{
            team_now_s(), ::app::messagingFacade().getSelfNodeId()};
        const ::team::ui::TeamPageCommandReducer reducer(context);
        const ::team::ui::TeamPageRuntimePort runtime = team_runtime_port();
        s_team_action_scratch.pairing_effects =
            ::team::ui::TeamPagePairingCommandAction().startPairing(
                s_team_action_scratch.command_state,
                reducer,
                runtime,
                ::team::ui::TeamPagePairingCommandRole::Member,
                ::app::messagingFacade().getSelfNodeId());
        save_team_command_state(s_team_action_scratch.command_state,
                                s_state.team_snapshot.team_chat_unread);
        set_notice(s_team_action_scratch.pairing_effects.started_pairing
                       ? "TEAM SCAN ACTIVE"
                       : "TEAM JOIN UNAVAILABLE");
        break;
    }
    case Action::TeamLeave:
    {
        if (!::team::ui::team_ui_snapshot_store().load(s_state.team_snapshot))
        {
            set_notice("NO TEAM TO LEAVE");
            break;
        }
        copy_team_command_state(s_state.team_snapshot, s_team_action_scratch.command_state);
        const ::team::ui::TeamPageCommandContext context{
            team_now_s(), ::app::messagingFacade().getSelfNodeId()};
        const ::team::ui::TeamPageCommandReducer reducer(context);
        const ::team::ui::TeamPageRuntimePort runtime = team_runtime_port();
        s_team_action_scratch.command_effects =
            reducer.reduceLeave(s_team_action_scratch.command_state);
        apply_team_runtime_effects(s_team_action_scratch.command_effects, runtime);
        save_team_command_state(s_team_action_scratch.command_state, 0);
        set_notice("LEFT TEAM");
        break;
    }
    case Action::TeamChat:
        s_state.team_chat_open = true;
        s_state.chat_messages_open = true;
        s_state.chat_selected_index = 0;
        set_notice("TEAM CHAT OPEN");
        break;
    case Action::ContactsPrevious:
    case Action::ContactsNext:
    {
        if (!::app::hasAppFacade())
        {
            set_notice("CONTACT SERVICE UNAVAILABLE");
            break;
        }
        const ::chat::contacts::ContactService& service =
            ::app::messagingFacade().getContactService();
        const std::vector<::chat::contacts::PeerDirectoryItem>& peers =
            s_state.contacts_nearby_view ? service.getNearby() : service.getContacts();
        if (peers.empty())
        {
            set_notice("NO CONTACTS IN THIS VIEW");
            break;
        }
        if (action == Action::ContactsPrevious)
        {
            s_state.contacts_selected_index = s_state.contacts_selected_index == 0
                                                  ? peers.size() - 1U
                                                  : s_state.contacts_selected_index - 1U;
        }
        else
        {
            s_state.contacts_selected_index =
                (s_state.contacts_selected_index + 1U) % peers.size();
        }
        set_notice("");
        break;
    }
    case Action::ContactsToggleView:
        s_state.contacts_nearby_view = !s_state.contacts_nearby_view;
        s_state.contacts_selected_index = 0;
        set_notice(s_state.contacts_nearby_view ? "NEARBY NODES" : "SAVED CONTACTS");
        break;
    case Action::ContactsOpenChat:
    {
        if (!select_contact())
        {
            set_notice("NO CONTACT SELECTED");
            break;
        }
        const ::chat::contacts::PeerDirectoryItem& peer = s_state.contacts_selected_peer;
        const ::chat::MeshProtocol configured_protocol =
            ::chat::infra::normalizeMeshProtocol(
                ::app::configFacade().readConfig().mesh_protocol);
        const ::chat::MeshProtocol protocol = ::chat::infra::meshProtocolFromNodeProtocol(
            peer.protocol,
            configured_protocol);

        if (protocol != configured_protocol)
        {
            set_notice("SWITCH PROTOCOL BEFORE CHAT");
            break;
        }

        ::chat::ConversationId core_id(::chat::ChannelId::PRIMARY, peer.node_id, protocol);
        if (protocol == ::chat::MeshProtocol::Reticulum &&
            ::chat::hasReticulumDestinationIdentity(peer.reticulum_identity))
        {
            core_id.peer = 0;
            core_id.reticulum_identity = peer.reticulum_identity;
        }
        s_chat_route.conversation =
            ::chat_presentation_adapters::toUiConversationId(core_id);
        s_chat_route.pending = s_chat_route.conversation.isValid();
        if (!s_chat_route.pending || !::ui::tdeck_pro::text_shell::launch_app_by_stable_id("chat"))
        {
            s_chat_route = ChatRoute{};
            set_notice("CHAT ROUTE UNAVAILABLE");
            break;
        }
        return;
    }
    case Action::ExtensionsPrevious:
    case Action::ExtensionsNext:
        if (s_state.extension_packages.empty())
        {
            set_notice("NO PACKAGE SELECTED");
            break;
        }
        if (action == Action::ExtensionsPrevious)
        {
            s_state.extension_selected_index = s_state.extension_selected_index == 0
                                                   ? s_state.extension_packages.size() - 1U
                                                   : s_state.extension_selected_index - 1U;
        }
        else
        {
            s_state.extension_selected_index =
                (s_state.extension_selected_index + 1U) % s_state.extension_packages.size();
        }
        set_notice("");
        break;
    case Action::ExtensionsRefresh:
        load_extension_catalog();
        break;
    case Action::ExtensionsApply:
    {
        const ::ui::runtime::packs::PackageRecord* const package = selected_extension();
        if (package == nullptr)
        {
            set_notice("NO PACKAGE SELECTED");
            break;
        }
        if (!package->compatible_firmware || !package->compatible_memory_profile)
        {
            set_notice("PACKAGE INCOMPATIBLE");
            break;
        }
        if (package->installed && !package->update_available)
        {
            set_notice("PACKAGE ALREADY INSTALLED");
            break;
        }
        s_state.extension_error.clear();
        if (!::ui::runtime::packs::start_install_package(*package, s_state.extension_error))
        {
            set_notice(s_state.extension_error.empty() ? "PACKAGE INSTALL REJECTED"
                                                       : s_state.extension_error.c_str());
            break;
        }
        s_state.extension_install_was_busy = true;
        s_last_handled_extension_install_id.clear();
        s_last_handled_extension_install_phase = ::ui::runtime::packs::PackageInstallPhase::Idle;
        s_state.extension_install_status = ::ui::runtime::packs::install_status();
        set_notice("PACKAGE INSTALL STARTED");
        break;
    }
    case Action::ExtensionsRemove:
    {
        const ::ui::runtime::packs::PackageRecord* const package = selected_extension();
        if (package == nullptr || !package->installed)
        {
            set_notice("NO INSTALLED PACKAGE SELECTED");
            break;
        }
        s_state.extension_error.clear();
        if (!::ui::runtime::packs::uninstall_package(*package, s_state.extension_error))
        {
            set_notice(s_state.extension_error.empty() ? "PACKAGE REMOVE FAILED"
                                                       : s_state.extension_error.c_str());
            break;
        }
        s_state.extension_catalog_loaded = false;
        s_state.extensions_seeded = false;
        set_notice("PACKAGE REMOVED; FONT FALLBACK UPDATED");
        break;
    }
    case Action::ProbeStartStop:
        if (s_state.probe_snapshot.scanning)
        {
            ::energy_sweep::ui::runtime::text_stop();
            s_state.probe_apply_pending = false;
            set_notice("PROBE STOPPED");
        }
        else
        {
            s_state.probe_apply_pending = false;
            set_notice(::energy_sweep::ui::runtime::text_start() ? "PROBE STARTED" : "PROBE START FAILED");
        }
        break;
    case Action::ProbePrevious:
    case Action::ProbeNext:
        if (::energy_sweep::ui::runtime::text_select_observation_delta(
                action == Action::ProbePrevious ? -1 : 1))
        {
            s_state.probe_apply_pending = false;
            set_notice("PROFILE SELECTED");
        }
        else
        {
            set_notice("NO PROFILE EVIDENCE");
        }
        break;
    case Action::ProbeApply:
        if (!s_state.probe_snapshot.has_selection)
        {
            set_notice("NO PROFILE SELECTED");
            break;
        }
        if (!s_state.probe_apply_pending)
        {
            s_state.probe_apply_pending = true;
            set_notice("PRESS CONFIRM TO APPLY PROFILE");
            break;
        }
        s_state.probe_apply_pending = false;
        set_notice(::energy_sweep::ui::runtime::text_apply_selected() ? "PROFILE APPLIED" : "APPLY FAILED");
        break;
    case Action::ProbeSync:
        set_notice("SNAPSHOT UPDATED");
        break;
    case Action::ChatPrevious:
    {
        ::ui::chat::ChatWorkspaceModel* const model = active_chat_model();
        if (model == nullptr)
        {
            set_notice("CHAT SERVICE UNAVAILABLE");
        }
        else if (s_state.chat_messages_open)
        {
            const uint16_t offset = model->messageOffset();
            model->setMessageOffset(offset < 10U ? 0U : static_cast<uint16_t>(offset - 10U));
            set_notice("OLDER MESSAGES");
        }
        else if (s_state.chat_snapshot.conversation_count > 0)
        {
            s_state.chat_selected_index = s_state.chat_selected_index == 0
                                              ? s_state.chat_snapshot.conversation_count - 1U
                                              : s_state.chat_selected_index - 1U;
            set_notice("");
        }
        break;
    }
    case Action::ChatNext:
    {
        ::ui::chat::ChatWorkspaceModel* const model = active_chat_model();
        if (model == nullptr)
        {
            set_notice("CHAT SERVICE UNAVAILABLE");
        }
        else if (s_state.chat_messages_open)
        {
            const uint16_t offset = model->messageOffset();
            model->setMessageOffset(static_cast<uint16_t>(offset + 10U));
            set_notice("NEWER MESSAGES");
        }
        else if (s_state.chat_snapshot.conversation_count > 0)
        {
            s_state.chat_selected_index =
                (s_state.chat_selected_index + 1U) % s_state.chat_snapshot.conversation_count;
            set_notice("");
        }
        break;
    }
    case Action::ChatOpen:
    {
        ::ui::chat::ChatWorkspaceModel* const model = active_chat_model();
        if (model == nullptr || s_state.chat_snapshot.conversation_count == 0 ||
            s_state.chat_selected_index >= s_state.chat_snapshot.conversation_count)
        {
            set_notice("NO CONVERSATION SELECTED");
            break;
        }
        const auto id = s_state.chat_snapshot.conversations[s_state.chat_selected_index].id;
        if (!model->selectConversation(id).ok)
        {
            set_notice("CONVERSATION UNAVAILABLE");
            break;
        }
        (void)model->markRead(id);
        s_state.chat_messages_open = true;
        hide_compose();
        set_notice("CONVERSATION OPEN");
        break;
    }
    case Action::ChatList:
        if (s_state.adapter != nullptr && s_state.adapter->page_kind() == TextAppPageKind::Team &&
            s_state.team_chat_open)
        {
            s_state.team_chat_open = false;
            s_state.chat_messages_open = false;
            hide_compose();
            set_notice("TEAM STATUS");
            break;
        }
        s_state.chat_messages_open = false;
        hide_compose();
        set_notice("CONVERSATION LIST");
        break;
    case Action::ChatType:
        if (!valid(s_state.compose) || !s_state.chat_snapshot.composer_enabled)
        {
            set_notice("COMPOSER UNAVAILABLE");
            break;
        }
        lv_obj_clear_flag(s_state.compose, LV_OBJ_FLAG_HIDDEN);
        if (!s_state.compose_editing && app_g != nullptr)
        {
            lv_group_add_obj(app_g, s_state.compose);
        }
        s_state.compose_editing = true;
        lv_group_focus_obj(s_state.compose);
        set_notice("TYPE MESSAGE THEN SELECT SEND");
        break;
    case Action::ChatSend:
    {
        ::ui::chat::ChatWorkspaceModel* const model = active_chat_model();
        const char* const text = valid(s_state.compose) ? lv_textarea_get_text(s_state.compose) : nullptr;
        if (model == nullptr || text == nullptr || text[0] == '\0')
        {
            set_notice("TYPE A MESSAGE FIRST");
            break;
        }
        if (model->sendMessage(text).ok)
        {
            lv_textarea_set_text(s_state.compose, "");
            hide_compose();
            set_notice("MESSAGE QUEUED");
            focus_action(3);
        }
        else
        {
            set_notice("SEND REJECTED");
        }
        break;
    }
    }
    refresh_page();
}

void on_button_event(lv_event_t* event)
{
    lv_obj_t* button = lv_event_get_target_obj(event);
    ActionButton* action = action_for(button);
    if (action == nullptr)
    {
        return;
    }

    switch (lv_event_get_code(event))
    {
    case LV_EVENT_FOCUSED:
        apply_button_focus(*action, true);
        break;
    case LV_EVENT_DEFOCUSED:
        apply_button_focus(*action, false);
        break;
    case LV_EVENT_CLICKED:
        run_action(action->action);
        break;
    case LV_EVENT_KEY:
        if (lv_event_get_key(event) == LV_KEY_ESC)
        {
            ui_request_exit_to_menu();
        }
        break;
    default:
        break;
    }
}

void add_action(const char* label, Action action, lv_coord_t x, lv_coord_t y, lv_coord_t width)
{
    if (s_state.action_count >= kMaxActions || !valid(s_state.root))
    {
        return;
    }

    ActionButton& entry = s_state.actions[s_state.action_count++];
    entry.action = action;
    entry.button = lv_btn_create(s_state.root);
    lv_obj_set_pos(entry.button, x, y);
    lv_obj_set_size(entry.button, width, 18);
    lv_obj_set_style_bg_color(entry.button, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(entry.button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(entry.button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(entry.button, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(entry.button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(entry.button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(entry.button, 0, LV_PART_MAIN);
    lv_obj_clear_flag(entry.button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(entry.button, on_button_event, LV_EVENT_ALL, nullptr);

    entry.label = create_text(entry.button, width - 2, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(entry.label);
    set_text(entry.label, ::ui::i18n::tr(label));
    lv_group_add_obj(app_g, entry.button);
}

void create_root(lv_obj_t* parent, TextAppAdapter& adapter)
{
    s_state = State{};
    s_state.adapter = &adapter;
    s_state.root = lv_obj_create(parent);
    lv_obj_set_pos(s_state.root, 0, 0);
    lv_obj_set_size(s_state.root, kScreenWidth, kScreenHeight);
    style_paper(s_state.root);

    s_state.title = create_text(s_state.root, 160);
    lv_obj_set_pos(s_state.title, kMargin, kMargin);
    set_text(s_state.title, adapter.name());

    s_state.subtitle = create_text(s_state.root, 56, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_pos(s_state.subtitle, kScreenWidth - kMargin - 56, kMargin);
    set_text(s_state.subtitle, "TEXT UI");

    lv_obj_t* rule = lv_obj_create(s_state.root);
    lv_obj_set_pos(rule, kMargin, kHeaderRuleY);
    lv_obj_set_size(rule, kContentWidth, 1);
    lv_obj_set_style_bg_color(rule, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(rule, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t index = 0; index < kMaxLines; ++index)
    {
        s_state.lines[index] = create_text(s_state.root, kContentWidth);
        lv_obj_set_pos(s_state.lines[index], kMargin, kBodyTop + static_cast<lv_coord_t>(index) * kLineHeight);
    }

    switch (adapter.page_kind())
    {
    case TextAppPageKind::Map:
        add_action("CENTER", Action::CenterOnSelf, kMargin, kActionTop, 70);
        add_action("ZOOM+", Action::ZoomIn, 84, kActionTop, 70);
        add_action("ZOOM-", Action::ZoomOut, 160, kActionTop, 72);
        add_action("TERRAIN", Action::ToggleTerrain, kMargin, kActionTop + 22, 90);
        break;
    case TextAppPageKind::SkyPlot:
    case TextAppPageKind::Settings:
        add_action("GPS ON/OFF", Action::ToggleGps, kMargin, kActionTop, 118);
        break;
    case TextAppPageKind::Network:
        add_action("REFRESH", Action::Refresh, kMargin, kActionTop, 86);
        break;
    case TextAppPageKind::Tracker:
        add_action("START/STOP", Action::ToggleTracker, kMargin, kActionTop, 112);
        break;
    case TextAppPageKind::Walkie:
        add_action("START/STOP", Action::ToggleWalkie, kMargin, kActionTop, 112);
        add_action("MONITOR", Action::ToggleWalkieMonitor, 126, kActionTop, 106);
        break;
    case TextAppPageKind::Sstv:
        add_action("START/STOP", Action::ToggleSstv, kMargin, kActionTop, 112);
        break;
    case TextAppPageKind::UsbStorage:
        add_action("START/STOP", Action::ToggleUsb, kMargin, kActionTop, 112);
        break;
    case TextAppPageKind::Chat:
        add_action("PREV", Action::ChatPrevious, kMargin, kActionTop, 48);
        add_action("NEXT", Action::ChatNext, 62, kActionTop, 48);
        add_action("OPEN", Action::ChatOpen, 116, kActionTop, 54);
        add_action("SYNC", Action::Refresh, 176, kActionTop, 56);
        add_action("BACK", Action::Back, kMargin, kActionTop + 22, 48);
        break;
    case TextAppPageKind::Team:
        add_action("CREATE", Action::TeamCreate, kMargin, kActionTop, 70);
        add_action("JOIN", Action::TeamJoin, 84, kActionTop, 54);
        add_action("REFRESH", Action::Refresh, 144, kActionTop, 86);
        add_action("BACK", Action::Back, kMargin, kActionTop + 22, 48);
        add_action("", Action::Refresh, 62, kActionTop + 22, 48);
        break;
    case TextAppPageKind::Contacts:
        add_action("PREV", Action::ContactsPrevious, kMargin, kActionTop, 48);
        add_action("NEXT", Action::ContactsNext, 62, kActionTop, 48);
        add_action("CHAT", Action::ContactsOpenChat, 116, kActionTop, 54);
        add_action("VIEW", Action::ContactsToggleView, 176, kActionTop, 56);
        add_action("BACK", Action::Back, kMargin, kActionTop + 22, 48);
        break;
    case TextAppPageKind::Extensions:
        add_action("PREV", Action::ExtensionsPrevious, kMargin, kActionTop, 48);
        add_action("NEXT", Action::ExtensionsNext, 62, kActionTop, 48);
        add_action("RELOAD", Action::ExtensionsRefresh, 116, kActionTop, 54);
        add_action("APPLY", Action::ExtensionsApply, 176, kActionTop, 56);
        add_action("BACK", Action::Back, kMargin, kActionTop + 22, 48);
        break;
    case TextAppPageKind::ProtocolProbe:
        add_action("START", Action::ProbeStartStop, kMargin, kActionTop, 70);
        add_action("PREV", Action::ProbePrevious, 84, kActionTop, 48);
        add_action("NEXT", Action::ProbeNext, 138, kActionTop, 48);
        add_action("APPLY", Action::ProbeApply, kMargin, kActionTop + 22, 70);
        add_action("SYNC", Action::ProbeSync, 84, kActionTop + 22, 48);
        add_action("BACK", Action::Back, 138, kActionTop + 22, 48);
        break;
    }

    lv_obj_t* footer_rule = lv_obj_create(s_state.root);
    lv_obj_set_pos(footer_rule, kMargin, kFooterRuleY);
    lv_obj_set_size(footer_rule, kContentWidth, 1);
    lv_obj_set_style_bg_color(footer_rule, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(footer_rule, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(footer_rule, 0, LV_PART_MAIN);
    lv_obj_clear_flag(footer_rule, LV_OBJ_FLAG_SCROLLABLE);

    s_state.footer = create_text(s_state.root, kContentWidth);
    lv_obj_set_pos(s_state.footer, kMargin, kFooterTop);
    set_text(s_state.footer, "UP/DN  ENT ACT  ESC BACK");
    if (adapter.page_kind() != TextAppPageKind::Chat && adapter.page_kind() != TextAppPageKind::Team &&
        adapter.page_kind() != TextAppPageKind::Contacts &&
        adapter.page_kind() != TextAppPageKind::Extensions &&
        adapter.page_kind() != TextAppPageKind::ProtocolProbe)
    {
        add_action("BACK", Action::Back, kMargin, kButtonTop, 58);
    }

    if (adapter.page_kind() == TextAppPageKind::Chat || adapter.page_kind() == TextAppPageKind::Team)
    {
        s_state.compose = lv_textarea_create(s_state.root);
        lv_obj_set_pos(s_state.compose, 62, kActionTop + 22);
        lv_obj_set_size(s_state.compose, 170, 18);
        lv_obj_set_style_text_font(s_state.compose, text_font(), LV_PART_MAIN);
        lv_obj_set_style_text_color(s_state.compose, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_state.compose, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_state.compose, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_state.compose, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_state.compose, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_radius(s_state.compose, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_left(s_state.compose, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_right(s_state.compose, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_top(s_state.compose, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(s_state.compose, 0, LV_PART_MAIN);
        lv_textarea_set_one_line(s_state.compose, true);
        lv_textarea_set_max_length(s_state.compose, 120);
        lv_textarea_set_placeholder_text(s_state.compose, "MESSAGE");
        lv_obj_add_flag(s_state.compose, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_state.action_count > 0)
    {
        lv_group_focus_obj(s_state.actions[0].button);
    }

    refresh_page();
}

void destroy_root(lv_obj_t* parent)
{
    if (app_g != nullptr)
    {
        lv_group_remove_all_objs(app_g);
    }
    if (valid(s_state.root))
    {
        lv_obj_del(s_state.root);
    }
    s_state = State{};
    (void)parent;
}

} // namespace

TextAppAdapter::TextAppAdapter(const char* stable_id,
                               const char* name,
                               TextAppPageKind page_kind,
                               ui::AppLaunchMode launch_mode)
    : stable_id_(stable_id ? stable_id : ""),
      name_(name ? name : ""),
      page_kind_(page_kind),
      launch_mode_(launch_mode)
{
}

const char* TextAppAdapter::stable_id() const
{
    return stable_id_;
}

const char* TextAppAdapter::name() const
{
    return name_;
}

const lv_image_dsc_t* TextAppAdapter::icon() const
{
    return nullptr;
}

ui::AppLaunchMode TextAppAdapter::launch_mode() const
{
    return launch_mode_;
}

void TextAppAdapter::enter(lv_obj_t* parent)
{
    if (parent == nullptr)
    {
        return;
    }
    if (app_g != nullptr)
    {
        lv_group_remove_all_objs(app_g);
        set_default_group(app_g);
    }
    create_root(parent, *this);
}

void TextAppAdapter::exit(lv_obj_t* parent)
{
    if (page_kind_ == TextAppPageKind::ProtocolProbe)
    {
        ::energy_sweep::ui::runtime::text_stop();
    }
    destroy_root(parent);
}

} // namespace ui::tdeck_pro

#endif // defined(ARDUINO_T_DECK_PRO)
