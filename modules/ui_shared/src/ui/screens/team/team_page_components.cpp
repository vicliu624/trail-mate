/**
 * @file team_page_components.cpp
 * @brief Team page components
 */

#include "ui/screens/team/team_page_components.h"
#include "app/app_facade_access.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "sys/clock.h"
#include "sys/event_bus.h"
#include "team/protocol/team_mgmt.h"
#include "team/usecase/team_controller.h"
#include "team/usecase/team_pairing_service.h"
#include "team/usecase/team_service.h"
#include "ui/app_runtime.h"
#include "ui/chat_ui_runtime.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/screens/team/team_page_activity_sink.h"
#include "ui/screens/team/team_page_command_reducer.h"
#include "ui/screens/team/team_page_create_team_action.h"
#include "ui/screens/team/team_page_deferred_dispatch.h"
#include "ui/screens/team/team_page_event_effect_sink.h"
#include "ui/screens/team/team_page_event_reducer.h"
#include "ui/screens/team/team_page_flow_controller.h"
#include "ui/screens/team/team_page_input.h"
#include "ui/screens/team/team_page_key_event_log.h"
#include "ui/screens/team/team_page_key_request_action.h"
#include "ui/screens/team/team_page_kick_confirm_action.h"
#include "ui/screens/team/team_page_layout.h"
#include "ui/screens/team/team_page_lvgl_renderer.h"
#include "ui/screens/team/team_page_pairing_command_action.h"
#include "ui/screens/team/team_page_read_model.h"
#include "ui/screens/team/team_page_request_keys_action.h"
#include "ui/screens/team/team_page_runtime_port.h"
#include "ui/screens/team/team_page_state_store.h"
#include "ui/screens/team/team_page_styles.h"
#include "ui/screens/team/team_page_transfer_leader_action.h"
#include "ui/team_presentation/team_member_label.h"
#include "ui/ui_common.h"
#include "ui/widgets/top_bar.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace team
{
namespace ui
{
namespace
{
constexpr int kActionBtnPadH = 10;
constexpr uint8_t kKeyRoleMember = 1;

struct TeamPageLvglContext
{
    lv_obj_t* root = nullptr;
    lv_obj_t* page_obj = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* body = nullptr;
    lv_obj_t* actions = nullptr;

    lv_obj_t* action_btns[3] = {nullptr, nullptr, nullptr};
    lv_obj_t* action_labels[3] = {nullptr, nullptr, nullptr};
    lv_obj_t* detail_label = nullptr;

    std::vector<lv_obj_t*> list_items;
    std::vector<lv_obj_t*> focusables;
    lv_obj_t* default_focus = nullptr;

    ::ui::widgets::TopBar top_bar_widget;
    lv_group_t* group = nullptr;
    lv_group_t* modal_group = nullptr;
    lv_group_t* prev_group = nullptr;
    lv_obj_t* leave_confirm_modal = nullptr;
};

struct TeamPageState
{
    TeamPage page = TeamPage::StatusNotInTeam;
    std::vector<TeamPage> nav_stack;
    int selected_member_index = -1;

    bool in_team = false;
    bool pending_join = false;
    bool kicked_out = false;
    bool self_is_leader = false;
    uint32_t last_event_seq = 0;
    TeamPairingRole pairing_role = TeamPairingRole::None;
    TeamPairingState pairing_state = TeamPairingState::Idle;
    uint32_t pairing_peer_id = 0;
    std::string pairing_team_name;

    TeamId team_id{};
    bool has_team_id = false;

    std::string team_name;
    uint32_t security_round = 0;
    uint32_t last_update_s = 0;
    std::array<uint8_t, team::proto::kTeamChannelPskSize> team_psk{};
    bool has_team_psk = false;
    bool waiting_new_keys = false;
    uint32_t pending_join_started_s = 0;
    uint32_t team_chat_unread = 0;

    std::vector<TeamMemberUi> members;
};

struct TeamPageControllerContext
{
    TeamPageState state;
    TeamPageLvglContext lvgl;
    TeamPageStateStore state_store;
    TeamPageDeferredDispatchQueue deferred_dispatch;
    uint32_t rng_state = 0;
};

TeamPageControllerContext s_page_context;

TeamPageControllerContext& team_page_context()
{
    return s_page_context;
}

TeamPageState& team_page_state()
{
    return team_page_context().state;
}

TeamPageLvglContext& team_page_lvgl_context()
{
    return team_page_context().lvgl;
}

void reset_team_page_state()
{
    team_page_context().state = TeamPageState{};
}

void reset_team_page_lvgl_context()
{
    team_page_context().lvgl = TeamPageLvglContext{};
}

TeamPageInputContext input_context_from_lvgl()
{
    TeamPageInputContext context;
    context.root = team_page_lvgl_context().root;
    context.body = team_page_lvgl_context().body;
    context.top_bar = &team_page_lvgl_context().top_bar_widget;
    context.focusables = &team_page_lvgl_context().focusables;
    context.default_focus = &team_page_lvgl_context().default_focus;
    return context;
}

uint8_t next_random_byte()
{
    uint32_t& rng_state = team_page_context().rng_state;
    if (rng_state == 0)
    {
        rng_state = sys::millis_now() ^ 0xA5A55A5Au;
        if (rng_state == 0)
        {
            rng_state = 0x13579BDFu;
        }
    }
    rng_state ^= (rng_state << 13);
    rng_state ^= (rng_state >> 17);
    rng_state ^= (rng_state << 5);
    return static_cast<uint8_t>(rng_state & 0xFFu);
}

uint32_t now_secs();
bool is_team_ui_active();
bool is_team_chat_visible();
std::string resolve_node_label(uint32_t node_id);
bool is_pairing_active();
void sync_pairing_from_service();
void fill_status_members(team::proto::TeamStatus& status);
void add_keydist_pending(uint32_t node_id, uint32_t key_id);
void mark_keydist_confirmed(uint32_t node_id, uint32_t key_id);
void schedule_status_broadcast(uint8_t repeats, uint32_t delay_s);

class TeamPageRuntimeKeyEventWriter final : public ITeamPageKeyEventWriter
{
  public:
    bool appendKeyEvent(const TeamId& team_id,
                        TeamKeyEventType type,
                        uint32_t event_seq,
                        uint32_t timestamp_s,
                        const uint8_t* payload,
                        size_t payload_size) override
    {
        return team_ui_append_key_event(team_id,
                                        type,
                                        event_seq,
                                        timestamp_s,
                                        payload,
                                        payload_size);
    }
};

TeamPageKeyEventState key_event_state_from_page()
{
    TeamPageKeyEventState state;
    state.team_id = team_page_state().team_id;
    state.has_team_id = team_page_state().has_team_id;
    state.last_event_seq = team_page_state().last_event_seq;
    state.security_round = team_page_state().security_round;
    return state;
}

void apply_key_event_state_to_page(const TeamPageKeyEventState& state)
{
    team_page_state().last_event_seq = state.last_event_seq;
}

TeamPageKeyEventLog current_key_event_log()
{
    static TeamPageRuntimeKeyEventWriter writer;
    return TeamPageKeyEventLog(writer, now_secs());
}

template <typename Append>
bool append_key_event_to_page(Append append)
{
    auto state = key_event_state_from_page();
    const auto log = current_key_event_log();
    const bool ok = append(log, state);
    apply_key_event_state_to_page(state);
    return ok;
}

TeamPageRuntimePort current_runtime_port()
{
    static TeamPageKeyStorePortAdapter key_store;
    static TeamPageControllerPortAdapter controller(nullptr);
    static TeamPagePairingPortAdapter pairing(nullptr);
    controller =
        TeamPageControllerPortAdapter(app::teamFacade().getTeamController());
    pairing =
        TeamPagePairingPortAdapter(app::teamFacade().getTeamPairing());
    return TeamPageRuntimePort(&controller, &pairing, &key_store);
}

void notify_send_failed(const char* action, bool needs_keys)
{
    const char* msg = needs_keys ? "Send failed (keys not ready)" : "Send failed";
    if (action && action[0])
    {
        const std::string notice = std::string(::ui::i18n::tr(action)) + ": " + ::ui::i18n::tr(msg);
        ::ui::feedback::show_notice(notice.c_str(), 2000);
        return;
    }
    ::ui::feedback::show_notice(msg, 2000);
}

void notify_send_failed_detail(const char* action, team::TeamService::SendError err)
{
    const char* reason = "send failed";
    switch (err)
    {
    case team::TeamService::SendError::KeysNotReady:
        reason = "keys not ready";
        break;
    case team::TeamService::SendError::EncodeFail:
        reason = "encode failed";
        break;
    case team::TeamService::SendError::EncryptFail:
        reason = "encrypt failed";
        break;
    case team::TeamService::SendError::MeshSendFail:
        reason = "queue full";
        break;
    case team::TeamService::SendError::UnsupportedByProtocol:
        reason = "unsupported protocol";
        break;
    default:
        break;
    }
    const char* action_text = (action && action[0]) ? action : "Send";
    const std::string notice = std::string(::ui::i18n::tr(action_text)) + ": " + ::ui::i18n::tr(reason);
    ::ui::feedback::show_notice(notice.c_str(), 2000);
}

const char* kick_confirm_failure_action_text(
    TeamPageKickConfirmFailureAction action)
{
    switch (action)
    {
    case TeamPageKickConfirmFailureAction::Kick:
        return "Kick";
    case TeamPageKickConfirmFailureAction::KeyDist:
        return "KeyDist";
    case TeamPageKickConfirmFailureAction::Keys:
        return "Keys";
    case TeamPageKickConfirmFailureAction::Status:
        return "Status";
    default:
        return "Send";
    }
}

void apply_kick_confirm_failures(
    const TeamPageKickConfirmEffects& effects)
{
    for (const auto& failure : effects.failures)
    {
        const char* action =
            kick_confirm_failure_action_text(failure.action);
        if (failure.kind ==
            TeamPageKickConfirmFailureKind::SendFailedDetail)
        {
            notify_send_failed_detail(action, failure.error);
        }
        else
        {
            notify_send_failed(action, failure.needs_keys);
        }
    }
}

void apply_transfer_leader_failures(
    const TeamPageTransferLeaderEffects& effects)
{
    for (const auto& failure : effects.failures)
    {
        (void)failure;
        notify_send_failed("Transfer", true);
    }
}

void apply_create_team_failures(const TeamPageCreateTeamEffects& effects)
{
    for (const auto& failure : effects.failures)
    {
        if (failure.action == TeamPageCreateTeamFailureAction::Keys)
        {
            notify_send_failed("Keys", failure.needs_keys);
            continue;
        }
        if (failure.kind == TeamPageCreateTeamFailureKind::PairingNotReady)
        {
            ::ui::feedback::show_notice("Pairing not ready", 2000);
        }
        else if (failure.kind ==
                 TeamPageCreateTeamFailureKind::PairingInitFailed)
        {
            ::ui::feedback::show_notice("Pairing init failed", 2000);
        }
    }
}

void apply_pairing_command_failures(
    const TeamPagePairingCommandEffects& effects)
{
    for (const auto& failure : effects.failures)
    {
        switch (failure.kind)
        {
        case TeamPagePairingCommandFailureKind::LeaderRequired:
            ::ui::feedback::show_notice("Only leader can pair", 2000);
            break;
        case TeamPagePairingCommandFailureKind::PairingNotReady:
            ::ui::feedback::show_notice("Pairing not ready", 2000);
            break;
        case TeamPagePairingCommandFailureKind::PairingNotAvailable:
            ::ui::feedback::show_notice("Pairing not available", 2000);
            break;
        case TeamPagePairingCommandFailureKind::PairingInitFailed:
        default:
            ::ui::feedback::show_notice("Pairing init failed", 2000);
            break;
        }
    }
}

class TeamPageNodeLabelResolver final : public ITeamPageLvglNameResolver,
                                        public ITeamPageMemberNameResolver
{
  public:
    std::string resolveNodeName(uint32_t node_id) const override
    {
        return resolve_node_label(node_id);
    }

    std::string resolveMemberName(uint32_t node_id) const override
    {
        return resolve_node_label(node_id);
    }
};

const TeamPageNodeLabelResolver& team_page_name_resolver()
{
    static TeamPageNodeLabelResolver names;
    return names;
}

TeamPageEventState event_state_from_page()
{
    TeamPageEventState state;
    state.in_team = team_page_state().in_team;
    state.pending_join = team_page_state().pending_join;
    state.pending_join_started_s = team_page_state().pending_join_started_s;
    state.kicked_out = team_page_state().kicked_out;
    state.self_is_leader = team_page_state().self_is_leader;
    state.last_event_seq = team_page_state().last_event_seq;
    state.pairing_role = team_page_state().pairing_role;
    state.pairing_state = team_page_state().pairing_state;
    state.pairing_peer_id = team_page_state().pairing_peer_id;
    state.pairing_team_name = team_page_state().pairing_team_name;
    state.team_id = team_page_state().team_id;
    state.has_team_id = team_page_state().has_team_id;
    state.team_name = team_page_state().team_name;
    state.security_round = team_page_state().security_round;
    state.last_update_s = team_page_state().last_update_s;
    state.team_psk = team_page_state().team_psk;
    state.has_team_psk = team_page_state().has_team_psk;
    state.waiting_new_keys = team_page_state().waiting_new_keys;
    state.members = team_page_state().members;
    return state;
}

void apply_event_state_to_page(const TeamPageEventState& state)
{
    team_page_state().in_team = state.in_team;
    team_page_state().pending_join = state.pending_join;
    team_page_state().pending_join_started_s = state.pending_join_started_s;
    team_page_state().kicked_out = state.kicked_out;
    team_page_state().self_is_leader = state.self_is_leader;
    team_page_state().last_event_seq = state.last_event_seq;
    team_page_state().pairing_role = state.pairing_role;
    team_page_state().pairing_state = state.pairing_state;
    team_page_state().pairing_peer_id = state.pairing_peer_id;
    team_page_state().pairing_team_name = state.pairing_team_name;
    team_page_state().team_id = state.team_id;
    team_page_state().has_team_id = state.has_team_id;
    team_page_state().team_name = state.team_name;
    team_page_state().security_round = state.security_round;
    team_page_state().last_update_s = state.last_update_s;
    team_page_state().team_psk = state.team_psk;
    team_page_state().has_team_psk = state.has_team_psk;
    team_page_state().waiting_new_keys = state.waiting_new_keys;
    team_page_state().members = state.members;
}

TeamPageFlowState flow_state_from_page()
{
    TeamPageFlowState state;
    state.page = team_page_state().page;
    state.nav_stack = team_page_state().nav_stack;
    state.in_team = team_page_state().in_team;
    state.pending_join = team_page_state().pending_join;
    state.kicked_out = team_page_state().kicked_out;
    state.pending_join_started_s = team_page_state().pending_join_started_s;
    state.pairing_state = team_page_state().pairing_state;
    return state;
}

void apply_flow_state_to_page(const TeamPageFlowState& state)
{
    team_page_state().page = state.page;
    team_page_state().nav_stack = state.nav_stack;
    team_page_state().pending_join_started_s = state.pending_join_started_s;
}

TeamPageFlowController current_flow_controller()
{
    return TeamPageFlowController();
}

TeamPageEventReducer current_event_reducer()
{
    TeamPageEventContext context;
    context.now_s = now_secs();
    context.self_node_id = app::messagingFacade().getSelfNodeId();
    return TeamPageEventReducer(context, team_page_name_resolver());
}

template <typename Reduce>
TeamPageEventEffects reduce_event_state(Reduce reduce)
{
    auto state = event_state_from_page();
    auto reducer = current_event_reducer();
    const auto effects = reduce(reducer, state);
    apply_event_state_to_page(state);
    return effects;
}

void apply_keydist_effect(const TeamPageEventEffects& effects)
{
    if (effects.keydist_confirmed)
    {
        mark_keydist_confirmed(effects.keydist_member_id,
                               effects.keydist_key_id);
    }
}

void apply_event_navigation_requests(
    const TeamPageEventEffectResult& result)
{
    if (result.request_kicked_out_page)
    {
        team_page_state().page = TeamPage::KickedOut;
    }
    if (result.request_status_in_team_page)
    {
        team_page_state().page = TeamPage::StatusInTeam;
    }
    if (result.request_status_not_in_team_page)
    {
        team_page_state().page = TeamPage::StatusNotInTeam;
    }
    if (result.clear_nav_stack)
    {
        team_page_state().nav_stack.clear();
    }
}

class TeamPageEventDeferredAdapter final : public ITeamPageEventDeferred
{
  public:
    void confirmKeyDist(uint32_t node_id, uint32_t key_id) override
    {
        mark_keydist_confirmed(node_id, key_id);
    }

    void scheduleStatusBroadcast(uint8_t repeats,
                                 uint32_t delay_s) override
    {
        schedule_status_broadcast(repeats, delay_s);
    }
};

class TeamPageEventNotifierAdapter final : public ITeamPageEventNotifier
{
  public:
    void showMessage(const char* message) override
    {
        ::ui::feedback::show_notice(message, 2000);
    }

    void notifySendFailed(const char* action, bool needs_keys) override
    {
        notify_send_failed(action, needs_keys);
    }
};

TeamPageEventEffectResult apply_event_effects(
    const TeamPageEventEffects& effects)
{
    static TeamPageEventDeferredAdapter deferred;
    static TeamPageEventNotifierAdapter notifier;
    auto key_state = key_event_state_from_page();
    const auto result = TeamPageEventEffectSink().applyEffects(
        event_state_from_page(),
        key_state,
        effects,
        current_event_reducer(),
        current_runtime_port(),
        current_key_event_log(),
        deferred,
        notifier);
    apply_key_event_state_to_page(key_state);
    apply_event_navigation_requests(result);
    return result;
}

TeamPageActivityState activity_state_from_page()
{
    TeamPageActivityState state;
    state.team_id = team_page_state().team_id;
    state.has_team_id = team_page_state().has_team_id;
    state.team_chat_unread = team_page_state().team_chat_unread;
    return state;
}

void apply_activity_state_to_page(const TeamPageActivityState& state)
{
    team_page_state().team_chat_unread = state.team_chat_unread;
}

TeamPageActivitySink current_activity_sink()
{
    static TeamPageActivityStoreAdapter store;
    static TeamPageGpsTrackLoaderAdapter gps_track_loader;
    static TeamPageUnreadPublisherAdapter unread_publisher;
    TeamPageActivityContext context;
    context.now_s = now_secs();
    context.self_node_id = app::messagingFacade().getSelfNodeId();
    context.team_chat_visible = is_team_chat_visible();
    return TeamPageActivitySink(store,
                                store,
                                gps_track_loader,
                                unread_publisher,
                                context);
}

template <typename Consume>
void consume_activity(Consume consume)
{
    auto state = activity_state_from_page();
    const auto sink = current_activity_sink();
    consume(sink, state);
    apply_activity_state_to_page(state);
}

void apply_event_cleanup_effects(const TeamPageEventEffects& effects)
{
    if (effects.clear_keydist_pending)
    {
        team_page_context().deferred_dispatch.clearKeyDist();
    }
    if (effects.clear_status_pending)
    {
        team_page_context().deferred_dispatch.clearStatusBroadcasts();
    }
}

void apply_event_runtime_effects(const TeamPageEventEffects& effects)
{
    const auto runtime = current_runtime_port();
    if (effects.clear_keys)
    {
        runtime.clearKeys();
    }
    if (effects.stop_pairing)
    {
        runtime.stopPairing();
    }
    apply_event_cleanup_effects(effects);
}

TeamPagePairingUpdate pairing_update_from_event(
    const team::TeamPairingEvent& event)
{
    TeamPagePairingUpdate update;
    update.role = event.role;
    update.state = event.state;
    update.team_id = event.team_id;
    update.has_team_id = event.has_team_id;
    update.key_id = event.key_id;
    update.peer_id = event.peer_id;
    update.has_team_name = event.has_team_name;
    if (event.has_team_name)
    {
        update.team_name = event.team_name;
    }
    return update;
}

TeamPagePairingUpdate pairing_update_from_status(
    const team::TeamPairingStatus& status)
{
    TeamPagePairingUpdate update;
    update.role = status.role;
    update.state = status.state;
    update.team_id = status.team_id;
    update.has_team_id = status.has_team_id;
    update.key_id = status.key_id;
    update.peer_id = status.peer_id;
    update.has_team_name = status.has_team_name;
    if (status.has_team_name)
    {
        update.team_name = status.team_name;
    }
    return update;
}

TeamPageCommandState command_state_from_page()
{
    TeamPageCommandState state;
    state.in_team = team_page_state().in_team;
    state.pending_join = team_page_state().pending_join;
    state.pending_join_started_s = team_page_state().pending_join_started_s;
    state.kicked_out = team_page_state().kicked_out;
    state.self_is_leader = team_page_state().self_is_leader;
    state.last_event_seq = team_page_state().last_event_seq;
    state.pairing_role = team_page_state().pairing_role;
    state.pairing_state = team_page_state().pairing_state;
    state.pairing_peer_id = team_page_state().pairing_peer_id;
    state.pairing_team_name = team_page_state().pairing_team_name;
    state.team_id = team_page_state().team_id;
    state.has_team_id = team_page_state().has_team_id;
    state.team_name = team_page_state().team_name;
    state.security_round = team_page_state().security_round;
    state.last_update_s = team_page_state().last_update_s;
    state.team_psk = team_page_state().team_psk;
    state.has_team_psk = team_page_state().has_team_psk;
    state.waiting_new_keys = team_page_state().waiting_new_keys;
    state.selected_member_index = team_page_state().selected_member_index;
    state.members = team_page_state().members;
    return state;
}

void apply_command_state_to_page(const TeamPageCommandState& state)
{
    team_page_state().in_team = state.in_team;
    team_page_state().pending_join = state.pending_join;
    team_page_state().pending_join_started_s = state.pending_join_started_s;
    team_page_state().kicked_out = state.kicked_out;
    team_page_state().self_is_leader = state.self_is_leader;
    team_page_state().last_event_seq = state.last_event_seq;
    team_page_state().pairing_role = state.pairing_role;
    team_page_state().pairing_state = state.pairing_state;
    team_page_state().pairing_peer_id = state.pairing_peer_id;
    team_page_state().pairing_team_name = state.pairing_team_name;
    team_page_state().team_id = state.team_id;
    team_page_state().has_team_id = state.has_team_id;
    team_page_state().team_name = state.team_name;
    team_page_state().security_round = state.security_round;
    team_page_state().last_update_s = state.last_update_s;
    team_page_state().team_psk = state.team_psk;
    team_page_state().has_team_psk = state.has_team_psk;
    team_page_state().waiting_new_keys = state.waiting_new_keys;
    team_page_state().selected_member_index = state.selected_member_index;
    team_page_state().members = state.members;
}

TeamPageCommandReducer current_command_reducer()
{
    TeamPageCommandContext context;
    context.now_s = now_secs();
    context.self_node_id = app::messagingFacade().getSelfNodeId();
    return TeamPageCommandReducer(context);
}

template <typename Reduce>
TeamPageCommandEffects reduce_command_state(Reduce reduce)
{
    auto state = command_state_from_page();
    auto reducer = current_command_reducer();
    const auto effects = reduce(reducer, state);
    apply_command_state_to_page(state);
    return effects;
}

void apply_command_cleanup_effects(const TeamPageCommandEffects& effects)
{
    if (effects.clear_keydist_pending)
    {
        team_page_context().deferred_dispatch.clearKeyDist();
    }
    if (effects.clear_status_pending)
    {
        team_page_context().deferred_dispatch.clearStatusBroadcasts();
    }
}

void apply_command_runtime_effects(const TeamPageCommandEffects& effects)
{
    const auto runtime = current_runtime_port();
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

    apply_command_cleanup_effects(effects);
}

class TeamPageRandomByteAdapter final
    : public ITeamPageKickConfirmRandom,
      public ITeamPageCreateTeamRandom
{
  public:
    uint8_t nextByte() override
    {
        return next_random_byte();
    }
};

class TeamPageKickConfirmDeferredAdapter final
    : public ITeamPageKickConfirmDeferred
{
  public:
    void enqueueKeyDist(uint32_t node_id, uint32_t key_id) override
    {
        add_keydist_pending(node_id, key_id);
    }
};

TeamPageDeferredDispatchState deferred_dispatch_state_from_page()
{
    TeamPageDeferredDispatchState state;
    state.in_team = team_page_state().in_team;
    state.has_team_id = team_page_state().has_team_id;
    state.self_is_leader = team_page_state().self_is_leader;
    state.team_id = team_page_state().team_id;
    state.security_round = team_page_state().security_round;
    state.team_psk = team_page_state().team_psk;
    state.has_team_psk = team_page_state().has_team_psk;
    return state;
}

void apply_deferred_dispatch_failures(
    const TeamPageDeferredDispatchEffects& effects)
{
    for (const auto& failure : effects.failures)
    {
        const char* action =
            failure.action == TeamPageDeferredDispatchAction::KeyDist
                ? "KeyDist"
                : "Status";
        if (failure.kind ==
            TeamPageDeferredDispatchFailureKind::SendFailedDetail)
        {
            notify_send_failed_detail(action, failure.error);
        }
        else
        {
            notify_send_failed(action, failure.needs_keys);
        }
    }
}

void add_keydist_pending(uint32_t node_id, uint32_t key_id)
{
    team_page_context().deferred_dispatch.enqueueKeyDist(
        node_id,
        key_id,
        now_secs());
}

void mark_keydist_confirmed(uint32_t node_id, uint32_t key_id)
{
    team_page_context().deferred_dispatch.confirmKeyDist(node_id, key_id);
}

void schedule_status_broadcast(uint8_t repeats, uint32_t delay_s)
{
    team_page_context().deferred_dispatch.scheduleStatusBroadcast(
        repeats,
        now_secs(),
        delay_s);
}

void process_keydist_retries()
{
    const auto runtime = current_runtime_port();
    TeamPageDeferredDispatchRuntimeAdapter dispatch_port(runtime);
    const auto effects = team_page_context().deferred_dispatch.processKeyDistRetries(
        deferred_dispatch_state_from_page(),
        dispatch_port,
        now_secs());
    apply_deferred_dispatch_failures(effects);
}

void process_status_broadcasts()
{
    const auto state = deferred_dispatch_state_from_page();
    const uint32_t now_s = now_secs();
    if (!team_page_context().deferred_dispatch.statusDue(state, now_s))
    {
        return;
    }
    const auto runtime = current_runtime_port();
    TeamPageDeferredDispatchRuntimeAdapter dispatch_port(runtime);
    team::proto::TeamStatus status{};
    status.key_id = team_page_state().security_round;
    if (state.self_is_leader)
    {
        fill_status_members(status);
    }
    const auto effects = team_page_context().deferred_dispatch.processStatusBroadcasts(
        state,
        status,
        dispatch_port,
        now_s);
    if (effects.sent_status)
    {
        std::printf("[Team] status rebroadcast members=%u leader=%08lX\n",
                    static_cast<unsigned>(status.members.size()),
                    static_cast<unsigned long>(status.leader_id));
    }
    apply_deferred_dispatch_failures(effects);
}

void update_top_bar_title(const char* title)
{
    if (!title)
    {
        return;
    }
    ::ui::widgets::top_bar_set_title(team_page_lvgl_context().top_bar_widget, title);
}

void reset_team_ui_state()
{
    current_runtime_port().resetControllerUi();
}

void enter_kicked_out_state()
{
    const auto effects = reduce_command_state(
        [](TeamPageCommandReducer& reducer, TeamPageCommandState& state)
        {
            return reducer.reduceKickedOut(state);
        });
    apply_command_runtime_effects(effects);
    team_page_state().page = TeamPage::KickedOut;
    team_page_state().nav_stack.clear();
}

void modal_prepare_group()
{
    if (!team_page_lvgl_context().modal_group)
    {
        team_page_lvgl_context().modal_group = lv_group_create();
    }
    lv_group_remove_all_objs(team_page_lvgl_context().modal_group);
    team_page_lvgl_context().prev_group = lv_group_get_default();
    set_default_group(team_page_lvgl_context().modal_group);
}

void modal_restore_group()
{
    lv_group_t* restore = team_page_lvgl_context().prev_group;
    if (!restore)
    {
        restore = team_page_lvgl_context().group;
    }
    if (restore)
    {
        set_default_group(restore);
    }
    team_page_lvgl_context().prev_group = nullptr;
}

lv_obj_t* create_modal_root(int width, int height)
{
    lv_obj_t* screen = lv_screen_active();
    lv_coord_t screen_w = lv_obj_get_width(screen);
    lv_coord_t screen_h = lv_obj_get_height(screen);

    lv_obj_t* bg = lv_obj_create(screen);
    lv_obj_set_size(bg, screen_w, screen_h);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x3A2A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    const auto modal_size = ::ui::page_profile::resolve_modal_size(width, height, screen);
    lv_obj_t* win = lv_obj_create(bg);
    lv_obj_set_size(win, modal_size.width, modal_size.height);
    lv_obj_center(win);
    lv_obj_set_style_bg_color(win, lv_color_hex(0xFFF7E9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_hex(0xD9B06A), LV_PART_MAIN);
    lv_obj_set_style_radius(win, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(win, ::ui::page_profile::resolve_modal_pad(), LV_PART_MAIN);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    return bg;
}

void close_leave_confirm_modal()
{
    if (team_page_lvgl_context().leave_confirm_modal)
    {
        lv_obj_del(team_page_lvgl_context().leave_confirm_modal);
        team_page_lvgl_context().leave_confirm_modal = nullptr;
    }
    modal_restore_group();
}

void leave_confirm_modal_key_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        close_leave_confirm_modal();
        lv_event_stop_processing(e);
    }
}

uint32_t now_secs()
{
    return sys::millis_now() / 1000U;
}

lv_obj_t* create_modal_button(lv_obj_t* parent, const char* text, lv_event_cb_t cb)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_height(btn, ::ui::page_profile::current().list_item_height);
    style::apply_button_secondary(btn);
    lv_obj_set_style_pad_hor(btn, kActionBtnPadH, LV_PART_MAIN);
    lv_obj_t* label = lv_label_create(btn);
    ::ui::i18n::set_label_text(label, text);
    lv_obj_add_event_cb(btn, leave_confirm_modal_key_cb, LV_EVENT_KEY, nullptr);
    lv_obj_update_layout(label);
    lv_coord_t width = lv_obj_get_width(label) + (kActionBtnPadH * 2);
    if (width < ::ui::page_profile::resolve_compact_button_min_width())
    {
        width = ::ui::page_profile::resolve_compact_button_min_width();
    }
    lv_obj_set_width(btn, width);
    lv_obj_center(label);
    if (cb)
    {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    }
    return btn;
}

TeamPageReadModelInput current_read_model_input()
{
    TeamPageReadModelInput input;
    input.in_team = team_page_state().in_team;
    input.pending_join = team_page_state().pending_join;
    input.kicked_out = team_page_state().kicked_out;
    input.self_is_leader = team_page_state().self_is_leader;
    input.waiting_new_keys = team_page_state().waiting_new_keys;
    input.team_id = team_page_state().team_id;
    input.has_team_id = team_page_state().has_team_id;
    input.team_name = team_page_state().team_name;
    input.security_round = team_page_state().security_round;
    input.last_update_s = team_page_state().last_update_s;
    input.has_team_psk = team_page_state().has_team_psk;
    input.pairing_role = team_page_state().pairing_role;
    input.pairing_state = team_page_state().pairing_state;
    input.pairing_team_name = team_page_state().pairing_team_name;
    input.members = team_page_state().members;
    input.selected_member_index = team_page_state().selected_member_index;
    return input;
}

TeamPageReadModel current_read_model()
{
    const uint32_t epoch_s = sys::epoch_seconds_now();
    return TeamPageReadModel(epoch_s >= 1577836800U ? epoch_s : now_secs());
}

std::string resolve_node_label(uint32_t node_id)
{
    return ::ui::team_presentation::shortTeamMemberLabel(node_id);
}

TeamPageColorContext current_color_context()
{
    return TeamPageColorContext(app::messagingFacade().getSelfNodeId());
}

TeamPagePersistentState persistent_state_from_page()
{
    TeamPagePersistentState state;
    state.in_team = team_page_state().in_team;
    state.pending_join = team_page_state().pending_join;
    state.pending_join_started_s = team_page_state().pending_join_started_s;
    state.kicked_out = team_page_state().kicked_out;
    state.self_is_leader = team_page_state().self_is_leader;
    state.last_event_seq = team_page_state().last_event_seq;
    state.team_chat_unread = team_page_state().team_chat_unread;
    state.team_id = team_page_state().team_id;
    state.has_team_id = team_page_state().has_team_id;
    state.team_name = team_page_state().team_name;
    state.security_round = team_page_state().security_round;
    state.last_update_s = team_page_state().last_update_s;
    state.team_psk = team_page_state().team_psk;
    state.has_team_psk = team_page_state().has_team_psk;
    state.members = team_page_state().members;
    return state;
}

void apply_persistent_state_to_page(const TeamPagePersistentState& state)
{
    team_page_state().in_team = state.in_team;
    team_page_state().pending_join = state.pending_join;
    team_page_state().pending_join_started_s = state.pending_join_started_s;
    team_page_state().kicked_out = state.kicked_out;
    team_page_state().self_is_leader = state.self_is_leader;
    team_page_state().last_event_seq = state.last_event_seq;
    team_page_state().team_chat_unread = state.team_chat_unread;
    team_page_state().team_id = state.team_id;
    team_page_state().has_team_id = state.has_team_id;
    team_page_state().team_name = state.team_name;
    team_page_state().security_round = state.security_round;
    team_page_state().last_update_s = state.last_update_s;
    team_page_state().team_psk = state.team_psk;
    team_page_state().has_team_psk = state.has_team_psk;
    team_page_state().members = state.members;
}

bool is_team_ui_active()
{
    return team_page_lvgl_context().root && lv_obj_is_valid(team_page_lvgl_context().root);
}

bool is_team_chat_visible()
{
    chat::ui::IChatUiRuntime* ui = app::runtimeFacade().getChatUiRuntime();
    if (!ui)
    {
        return false;
    }
    auto state = ui->getState();
    if (state != chat::ui::ChatUiState::Conversation &&
        state != chat::ui::ChatUiState::Compose)
    {
        return false;
    }
    return ui->isTeamConversationActive();
}

bool is_pairing_active()
{
    return TeamPageFlowController::isPairingActive(
        team_page_state().pairing_state);
}

void sync_pairing_from_service()
{
    const auto runtime = current_runtime_port();
    if (!runtime.hasPairing())
    {
        return;
    }
    const auto update = pairing_update_from_status(runtime.pairingStatus());
    reduce_event_state(
        [&](TeamPageEventReducer& reducer, TeamPageEventState& state)
        {
            return reducer.reducePairingStatus(state, update);
        });
}

void load_state_from_store()
{
    auto state = persistent_state_from_page();
    if (team_page_context().state_store.loadOnce(team_ui_snapshot_store(),
                                                 state,
                                                 current_color_context()))
    {
        apply_persistent_state_to_page(state);
    }
}

void refresh_state_from_store()
{
    auto state = persistent_state_from_page();
    if (team_page_context().state_store.refresh(team_ui_snapshot_store(),
                                                state,
                                                current_color_context()))
    {
        apply_persistent_state_to_page(state);
    }
}

void save_state_to_store()
{
    team_page_context().state_store.save(team_ui_snapshot_store(),
                                         persistent_state_from_page());
}

void fill_status_members(team::proto::TeamStatus& status)
{
    current_event_reducer().fillStatusMembers(event_state_from_page(), status);
}

void handle_team_error(const team::TeamErrorEvent& ev)
{
    const auto effects = reduce_event_state(
        [&](TeamPageEventReducer& reducer, TeamPageEventState& state)
        {
            return reducer.reduceError(state, ev);
        });
    if (effects.show_key_mismatch)
    {
        ::ui::feedback::show_notice("Team keys mismatch", 2000);
    }
}

void handle_team_status(const team::TeamStatusEvent& ev)
{
    const auto effects = reduce_event_state(
        [&](TeamPageEventReducer& reducer, TeamPageEventState& state)
        {
            return reducer.reduceStatus(state, ev);
        });
    if (!effects.accepted)
    {
        return;
    }
    apply_event_effects(effects);
}

void handle_team_position(const team::TeamPositionEvent& ev)
{
    const auto effects = reduce_event_state(
        [&](TeamPageEventReducer& reducer, TeamPageEventState& state)
        {
            return reducer.reduceActivity(state, ev.ctx, ev.ctx.from);
        });
    if (!effects.accepted)
    {
        return;
    }
    apply_keydist_effect(effects);
    consume_activity(
        [&](const TeamPageActivitySink& sink,
            TeamPageActivityState& state)
        {
            sink.consumePosition(state, ev);
        });
}

void handle_team_waypoint(const team::TeamWaypointEvent& ev)
{
    const auto effects = reduce_event_state(
        [&](TeamPageEventReducer& reducer, TeamPageEventState& state)
        {
            return reducer.reduceActivity(state, ev.ctx, ev.ctx.from);
        });
    if (!effects.accepted)
    {
        return;
    }
    apply_keydist_effect(effects);
    consume_activity(
        [&](const TeamPageActivitySink& sink,
            TeamPageActivityState& state)
        {
            sink.consumeWaypoint(state, ev);
        });
}

void handle_team_track(const team::TeamTrackEvent& ev)
{
    const auto effects = reduce_event_state(
        [&](TeamPageEventReducer& reducer, TeamPageEventState& state)
        {
            return reducer.reduceActivity(state, ev.ctx, ev.ctx.from);
        });
    if (!effects.accepted)
    {
        return;
    }
    apply_keydist_effect(effects);
    consume_activity(
        [&](const TeamPageActivitySink& sink,
            TeamPageActivityState& state)
        {
            sink.consumeTrack(state, ev);
        });
}

void handle_team_chat(const team::TeamChatEvent& ev)
{
    uint32_t from_id = ev.msg.header.from != 0 ? ev.msg.header.from : ev.ctx.from;
    const auto effects = reduce_event_state(
        [&](TeamPageEventReducer& reducer, TeamPageEventState& state)
        {
            return reducer.reduceActivity(state, ev.ctx, from_id);
        });
    if (!effects.accepted)
    {
        return;
    }
    apply_keydist_effect(effects);
    consume_activity(
        [&](const TeamPageActivitySink& sink,
            TeamPageActivityState& state)
        {
            sink.consumeChat(state, ev);
        });
}

void handle_team_kick(const team::TeamKickEvent& ev)
{
    const auto effects = reduce_event_state(
        [&](TeamPageEventReducer& reducer, TeamPageEventState& state)
        {
            return reducer.reduceKick(state, ev);
        });
    if (!effects.accepted)
    {
        return;
    }
    if (effects.member_kicked_key_event)
    {
        append_key_event_to_page(
            [&](const TeamPageKeyEventLog& log,
                TeamPageKeyEventState& state)
            {
                return log.appendMemberKicked(state,
                                              effects.member_kicked_id);
            });
    }
    if (effects.epoch_rotated)
    {
        append_key_event_to_page(
            [&](const TeamPageKeyEventLog& log,
                TeamPageKeyEventState& state)
            {
                return log.appendEpochRotated(state, effects.epoch_key_id);
            });
    }
    apply_event_runtime_effects(effects);
    if (effects.request_kicked_out_page)
    {
        team_page_state().page = TeamPage::KickedOut;
    }
    if (effects.clear_nav_stack)
    {
        team_page_state().nav_stack.clear();
    }
}

void handle_team_transfer_leader(const team::TeamTransferLeaderEvent& ev)
{
    uint32_t target = ev.msg.target;
    append_key_event_to_page(
        [&](const TeamPageKeyEventLog& log,
            TeamPageKeyEventState& state)
        {
            return log.appendLeaderTransferred(state, target);
        });
    reduce_event_state(
        [&](TeamPageEventReducer& reducer, TeamPageEventState& state)
        {
            return reducer.reduceTransferLeader(state, ev);
        });
}

void handle_team_key_dist(const team::TeamKeyDistEvent& ev)
{
    const auto effects = reduce_event_state(
        [&](TeamPageEventReducer& reducer, TeamPageEventState& state)
        {
            return reducer.reduceKeyDist(state, ev);
        });
    if (!effects.accepted)
    {
        return;
    }
    apply_event_effects(effects);
}

void handle_team_key_request(const team::TeamKeyRequestEvent& ev)
{
    const auto effects = TeamPageKeyRequestAction().handleRequest(
        command_state_from_page(),
        ev,
        current_runtime_port());
    if (effects.sent_keydist)
    {
        add_keydist_pending(ev.msg.requester_id != 0 ? ev.msg.requester_id
                                                     : ev.ctx.from,
                            team_page_state().security_round);
        ::ui::feedback::show_notice("Sent team keys", 2000);
        return;
    }

    for (const auto& failure : effects.failures)
    {
        if (failure.kind ==
            TeamPageKeyRequestFailureKind::SendFailedDetail)
        {
            notify_send_failed_detail("KeyDist", failure.error);
        }
    }
}

void handle_team_pairing(const team::TeamPairingEvent& ev)
{
    std::printf("[TeamUI] pairing event role=%u state=%u peer=%08lX in_team=%u leader=%u members=%u\n",
                static_cast<unsigned>(ev.role),
                static_cast<unsigned>(ev.state),
                static_cast<unsigned long>(ev.peer_id),
                team_page_state().in_team ? 1 : 0,
                team_page_state().self_is_leader ? 1 : 0,
                static_cast<unsigned>(team_page_state().members.size()));
    const auto update = pairing_update_from_event(ev);
    const auto effects = reduce_event_state(
        [&](TeamPageEventReducer& reducer, TeamPageEventState& state)
        {
            return reducer.reducePairing(state, update);
        });
    if (!effects.accepted)
    {
        return;
    }

    const auto result = apply_event_effects(effects);
    if (result.appended_member_accepted)
    {
        std::printf("[TeamUI] leader accept member=%08lX members=%u\n",
                    static_cast<unsigned long>(effects.member_accepted_id),
                    static_cast<unsigned>(team_page_state().members.size()));
    }
}

void nav_to(TeamPage page, bool push = true);
void nav_back();
void render_page();
void handle_page_transition(TeamPage next_page);
void nav_reset(TeamPage page);

void top_bar_back(void*)
{
    nav_back();
}

void nav_to(TeamPage page, bool push)
{
    auto state = flow_state_from_page();
    current_flow_controller().navigateTo(state, page, push);
    apply_flow_state_to_page(state);
    handle_page_transition(team_page_state().page);
    render_page();
}

void nav_back()
{
    auto state = flow_state_from_page();
    const auto result = current_flow_controller().navigateBack(state);
    if (result.request_exit)
    {
        ui_request_exit_to_menu();
        return;
    }
    apply_flow_state_to_page(state);
    handle_page_transition(team_page_state().page);
    render_page();
}

void nav_reset(TeamPage page)
{
    auto state = flow_state_from_page();
    current_flow_controller().resetTo(state, page);
    apply_flow_state_to_page(state);
    handle_page_transition(team_page_state().page);
    render_page();
}

void handle_page_transition(TeamPage next_page)
{
    (void)next_page;
}

bool require_pairing_service()
{
    if (app::teamFacade().getTeamPairing())
    {
        return true;
    }
    ::ui::feedback::show_notice("Team pairing is unavailable on this device", 2000);
    return false;
}

bool start_pairing_command(TeamPagePairingCommandRole role)
{
    if (!require_pairing_service())
    {
        return false;
    }
    auto state = command_state_from_page();
    const auto effects = TeamPagePairingCommandAction().startPairing(
        state,
        current_command_reducer(),
        current_runtime_port(),
        role,
        app::messagingFacade().getSelfNodeId());
    apply_command_state_to_page(state);
    apply_pairing_command_failures(effects);
    save_state_to_store();
    if (effects.started_pairing)
    {
        nav_to(TeamPage::JoinPending);
    }
    return effects.started_pairing;
}

void handle_create(lv_event_t*)
{
    if (!require_pairing_service())
    {
        return;
    }
    auto command_state = command_state_from_page();
    auto key_state = key_event_state_from_page();
    TeamPageRandomByteAdapter random;
    const auto effects = TeamPageCreateTeamAction().createTeam(
        command_state,
        key_state,
        current_command_reducer(),
        current_runtime_port(),
        current_key_event_log(),
        random,
        app::messagingFacade().getSelfNodeId());
    apply_command_state_to_page(command_state);
    apply_key_event_state_to_page(key_state);
    apply_create_team_failures(effects);
    save_state_to_store();
    if (effects.started_pairing)
    {
        nav_to(TeamPage::JoinPending);
    }
    else
    {
        nav_reset(TeamPage::StatusInTeam);
    }
}

void handle_join(lv_event_t*)
{
    start_pairing_command(TeamPagePairingCommandRole::Member);
}

void handle_view_team(lv_event_t*)
{
    nav_to(TeamPage::TeamHome);
}

void handle_invite(lv_event_t*)
{
    start_pairing_command(TeamPagePairingCommandRole::Leader);
}

void perform_leave()
{
    const auto effects = reduce_command_state(
        [](TeamPageCommandReducer& reducer, TeamPageCommandState& state)
        {
            return reducer.reduceLeave(state);
        });
    apply_command_runtime_effects(effects);
    save_state_to_store();
    nav_reset(TeamPage::StatusNotInTeam);
}

void handle_leave(lv_event_t*)
{
    if (team_page_lvgl_context().leave_confirm_modal)
    {
        return;
    }
    modal_prepare_group();
    team_page_lvgl_context().leave_confirm_modal = create_modal_root(260, 140);
    lv_obj_t* win = lv_obj_get_child(team_page_lvgl_context().leave_confirm_modal, 0);

    lv_obj_t* title_label = lv_label_create(win);
    ::ui::i18n::set_label_text(title_label, "Leave team?");
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* desc_label = lv_label_create(win);
    ::ui::i18n::set_label_text(desc_label, "This clears local keys.");
    lv_obj_align(desc_label, LV_ALIGN_TOP_MID, 0, ::ui::page_profile::current().large_touch_hitbox ? 40 : 28);

    lv_obj_t* btn_row = lv_obj_create(win);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* cancel_btn = create_modal_button(btn_row, "Cancel", nullptr);
    lv_obj_add_event_cb(
        cancel_btn, [](lv_event_t*)
        { close_leave_confirm_modal(); },
        LV_EVENT_CLICKED, nullptr);

    lv_obj_t* leave_btn = create_modal_button(btn_row, "Leave", nullptr);
    lv_obj_add_event_cb(
        leave_btn, [](lv_event_t*)
        {
        close_leave_confirm_modal();
        perform_leave(); },
        LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(team_page_lvgl_context().modal_group, cancel_btn);
    lv_group_add_obj(team_page_lvgl_context().modal_group, leave_btn);
    lv_group_focus_obj(cancel_btn);
}

void handle_manage(lv_event_t*)
{
    if (!team_page_state().self_is_leader)
    {
        ::ui::feedback::show_notice("Only leader can manage", 2000);
        return;
    }
    nav_to(TeamPage::Members);
}

void handle_member_clicked(lv_event_t* e)
{
    lv_obj_t* item = (lv_obj_t*)lv_event_get_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(item);
    team_page_state().selected_member_index = index;
    nav_to(TeamPage::MemberDetail);
}

void handle_kick(lv_event_t*)
{
    nav_to(TeamPage::KickConfirm);
}

void handle_kick_confirm(lv_event_t*)
{
    auto state = command_state_from_page();
    auto reducer = current_command_reducer();
    const auto runtime = current_runtime_port();
    TeamPageRandomByteAdapter random;
    TeamPageKickConfirmDeferredAdapter deferred;
    const auto effects = TeamPageKickConfirmAction().confirmKick(
        state,
        reducer,
        runtime,
        random,
        deferred,
        app::messagingFacade().getSelfNodeId());
    apply_command_state_to_page(state);
    apply_kick_confirm_failures(effects);
    save_state_to_store();
    nav_reset(TeamPage::StatusInTeam);
}

void handle_kick_cancel(lv_event_t*)
{
    nav_back();
}

void handle_transfer_leader(lv_event_t*)
{
    auto command_state = command_state_from_page();
    auto key_state = key_event_state_from_page();
    const auto effects = TeamPageTransferLeaderAction().transferLeader(
        command_state,
        key_state,
        current_command_reducer(),
        current_runtime_port(),
        current_key_event_log());
    apply_command_state_to_page(command_state);
    apply_key_event_state_to_page(key_state);
    apply_transfer_leader_failures(effects);
    save_state_to_store();
    nav_reset(TeamPage::TeamHome);
}

void handle_request_keydist(lv_event_t*)
{
    const auto effects =
        TeamPageRequestKeysAction().requestKeys(
            command_state_from_page(),
            current_runtime_port(),
            app::messagingFacade().getSelfNodeId());
    if (effects.sent_request)
    {
        ::ui::feedback::show_notice("Requested team keys", 2000);
    }
    else if (effects.send_failed)
    {
        notify_send_failed_detail("Request Keys", effects.error);
    }
}

void handle_join_cancel(lv_event_t*)
{
    const auto effects = reduce_command_state(
        [](TeamPageCommandReducer& reducer, TeamPageCommandState& state)
        {
            return reducer.reduceJoinCanceled(state);
        });
    apply_command_runtime_effects(effects);
    save_state_to_store();
    if (team_page_state().in_team)
    {
        nav_reset(TeamPage::StatusInTeam);
    }
    else
    {
        nav_reset(TeamPage::StatusNotInTeam);
    }
}

void handle_join_retry(lv_event_t*)
{
    if (team_page_state().self_is_leader)
    {
        start_pairing_command(TeamPagePairingCommandRole::Leader);
    }
    else
    {
        start_pairing_command(TeamPagePairingCommandRole::Member);
    }
}

void handle_kicked_join(lv_event_t*)
{
    if (!require_pairing_service())
    {
        return;
    }
    reduce_command_state(
        [](TeamPageCommandReducer& reducer, TeamPageCommandState& state)
        {
            return reducer.reduceClearKickedOut(state);
        });
    save_state_to_store();
    nav_reset(TeamPage::StatusNotInTeam);
}

void handle_kicked_ok(lv_event_t*)
{
    reduce_command_state(
        [](TeamPageCommandReducer& reducer, TeamPageCommandState& state)
        {
            return reducer.reduceClearKickedOut(state);
        });
    save_state_to_store();
    nav_reset(TeamPage::StatusNotInTeam);
}

void render_page()
{
    const auto read_model_input = current_read_model_input();
    if (team_page_state().page == TeamPage::MemberDetail &&
        !current_read_model().buildSelectedMember(read_model_input).valid)
    {
        nav_to(TeamPage::Members, false);
        return;
    }

    TeamPageLvglRendererContext context;
    context.body = team_page_lvgl_context().body;
    context.actions = team_page_lvgl_context().actions;
    context.top_bar = &team_page_lvgl_context().top_bar_widget;
    context.action_btns = team_page_lvgl_context().action_btns;
    context.action_btn_count = 3;
    context.action_labels = team_page_lvgl_context().action_labels;
    context.action_label_count = 3;
    context.detail_label = &team_page_lvgl_context().detail_label;
    context.list_items = &team_page_lvgl_context().list_items;
    context.focusables = &team_page_lvgl_context().focusables;
    context.default_focus = &team_page_lvgl_context().default_focus;

    TeamPageLvglRendererHandlers handlers;
    handlers.create_team = handle_create;
    handlers.join_team = handle_join;
    handlers.view_team = handle_view_team;
    handlers.invite = handle_invite;
    handlers.request_keys = handle_request_keydist;
    handlers.leave = handle_leave;
    handlers.manage = handle_manage;
    handlers.member_clicked = handle_member_clicked;
    handlers.kick = handle_kick;
    handlers.transfer_leader = handle_transfer_leader;
    handlers.kick_cancel = handle_kick_cancel;
    handlers.kick_confirm = handle_kick_confirm;
    handlers.join_cancel = handle_join_cancel;
    handlers.join_retry = handle_join_retry;
    handlers.kicked_join = handle_kicked_join;
    handlers.kicked_ok = handle_kicked_ok;

    TeamPageLvglRendererInput input;
    input.page = team_page_state().page;
    input.read_model = read_model_input;
    input.pairing_peer_id = team_page_state().pairing_peer_id;
    input.pairing_available = app::teamFacade().getTeamPairing() != nullptr;

    const uint32_t epoch_s = sys::epoch_seconds_now();
    TeamPageLvglRenderer(epoch_s >= 1577836800U ? epoch_s : now_secs())
        .render(context, input, handlers, team_page_name_resolver());

    ui_update_top_bar_battery(team_page_lvgl_context().top_bar_widget);
    refresh_team_input(input_context_from_lvgl());
}

void sync_from_controller()
{
    refresh_state_from_store();
    sync_pairing_from_service();
    auto state = flow_state_from_page();
    current_flow_controller().syncRuntime(state, now_secs());
    apply_flow_state_to_page(state);
}
} // namespace

void team_page_create(lv_obj_t* parent)
{
    if (team_page_lvgl_context().root)
    {
        lv_obj_del(team_page_lvgl_context().root);
        team_page_lvgl_context().root = nullptr;
    }

    style::init_once();
    load_state_from_store();
    sync_pairing_from_service();

    auto flow_state = flow_state_from_page();
    current_flow_controller().selectInitialPage(flow_state);
    apply_flow_state_to_page(flow_state);

    team_page_lvgl_context().root = layout::create_root(parent);
    team_page_lvgl_context().header = layout::create_header(team_page_lvgl_context().root);
    team_page_lvgl_context().content = layout::create_content(team_page_lvgl_context().root);
    team_page_lvgl_context().body = layout::create_body(team_page_lvgl_context().content);
    team_page_lvgl_context().actions = layout::create_actions(team_page_lvgl_context().content);

    style::apply_root(team_page_lvgl_context().root);
    style::apply_header(team_page_lvgl_context().header);
    style::apply_content(team_page_lvgl_context().content);
    style::apply_body(team_page_lvgl_context().body);
    style::apply_actions(team_page_lvgl_context().actions);

    ::ui::widgets::TopBarConfig cfg;
    cfg.height = ::ui::page_profile::current().top_bar_height;
    ::ui::widgets::top_bar_init(team_page_lvgl_context().top_bar_widget, team_page_lvgl_context().header, cfg);
    update_top_bar_title(::ui::i18n::tr("Team"));
    ::ui::widgets::top_bar_set_back_callback(team_page_lvgl_context().top_bar_widget, top_bar_back, nullptr);
    ui_update_top_bar_battery(team_page_lvgl_context().top_bar_widget);

    init_team_input(input_context_from_lvgl());
    team_page_refresh();
}

void team_page_destroy()
{
    cleanup_team_input();

    close_leave_confirm_modal();
    if (team_page_lvgl_context().modal_group)
    {
        lv_group_del(team_page_lvgl_context().modal_group);
        team_page_lvgl_context().modal_group = nullptr;
    }
    team_page_context().deferred_dispatch.clearAll();

    if (team_page_lvgl_context().root)
    {
        lv_obj_del(team_page_lvgl_context().root);
        team_page_lvgl_context().root = nullptr;
    }
    reset_team_page_state();
    reset_team_page_lvgl_context();
    team_page_context().state_store.resetLoaded();
}

void team_page_refresh()
{
    sync_from_controller();
    render_page();
}

bool team_page_handle_event(sys::Event* event)
{
    if (!event)
    {
        return false;
    }
    load_state_from_store();

    bool changed = false;

    switch (event->type)
    {
    case sys::EventType::TeamKick:
        handle_team_kick(static_cast<sys::TeamKickEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamTransferLeader:
        handle_team_transfer_leader(static_cast<sys::TeamTransferLeaderEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamKeyDist:
        handle_team_key_dist(static_cast<sys::TeamKeyDistEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamKeyRequest:
        handle_team_key_request(static_cast<sys::TeamKeyRequestEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamPairing:
        handle_team_pairing(static_cast<sys::TeamPairingEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::SystemTick:
    {
        process_keydist_retries();
        process_status_broadcasts();
        break;
    }
    case sys::EventType::TeamStatus:
        handle_team_status(static_cast<sys::TeamStatusEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamPosition:
        handle_team_position(static_cast<sys::TeamPositionEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamWaypoint:
        handle_team_waypoint(static_cast<sys::TeamWaypointEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamTrack:
        handle_team_track(static_cast<sys::TeamTrackEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamChat:
        handle_team_chat(static_cast<sys::TeamChatEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamError:
        handle_team_error(static_cast<sys::TeamErrorEvent*>(event)->data);
        changed = true;
        break;
    default:
        break;
    }

    if (changed)
    {
        save_state_to_store();
        if (is_team_ui_active())
        {
            team_page_refresh();
        }
    }

    return true;
}

} // namespace ui
} // namespace team

void team_page_create(lv_obj_t* parent)
{
    team::ui::team_page_create(parent);
}

void team_page_destroy()
{
    team::ui::team_page_destroy();
}

void team_page_refresh()
{
    team::ui::team_page_refresh();
}

bool team_page_handle_event(sys::Event* event)
{
    return team::ui::team_page_handle_event(event);
}
