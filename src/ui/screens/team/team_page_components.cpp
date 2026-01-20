/**
 * @file team_page_components.cpp
 * @brief Team page components
 */

#include "team_page_components.h"
#include "team_page_layout.h"
#include "team_page_styles.h"
#include "team_page_input.h"
#include "team_state.h"
#include "../../ui_common.h"
#include "../../../app/app_context.h"
#include "../../../team/protocol/team_mgmt.h"

namespace team
{
namespace ui
{
namespace
{
void update_top_bar_title(const char* title)
{
    if (!title)
    {
        return;
    }
    ::ui::widgets::top_bar_set_title(g_team_state.top_bar_widget, title);
}

void reset_team_ui_state()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (controller)
    {
        controller->resetUiState();
    }
}

void top_bar_back(void*)
{
    reset_team_ui_state();
    team_page_destroy();
    menu_show();
}

void label_set_text(lv_obj_t* btn, const char* text)
{
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
}

void handle_create(lv_event_t*)
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (!controller)
    {
        return;
    }
    team::proto::TeamAdvertise advertise;
    controller->onCreateTeam(advertise, chat::ChannelId::PRIMARY);
    g_team_state.title_override = "Create Team";
    update_top_bar_title(g_team_state.title_override);
    team_page_refresh();
}

void handle_join(lv_event_t*)
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (!controller)
    {
        return;
    }
    team::proto::TeamJoinRequest join_request;
    controller->onJoinTeam(join_request, chat::ChannelId::PRIMARY, 0);
    g_team_state.title_override = "Join Team";
    update_top_bar_title(g_team_state.title_override);
    team_page_refresh();
}

void handle_placeholder(lv_event_t*)
{
    team_page_refresh();
}

} // namespace

void team_page_create(lv_obj_t* parent)
{
    if (g_team_state.root)
    {
        lv_obj_del(g_team_state.root);
        g_team_state.root = nullptr;
    }

    style::init_once();

    g_team_state.root = layout::create_root(parent);
    g_team_state.header = layout::create_header(g_team_state.root);
    g_team_state.content = layout::create_content(g_team_state.root);
    g_team_state.state_container = layout::create_state_container(g_team_state.content);
    g_team_state.idle_container = layout::create_idle_container(g_team_state.state_container);
    g_team_state.active_container = layout::create_active_container(g_team_state.state_container);

    style::apply_root(g_team_state.root);
    style::apply_header(g_team_state.header);
    style::apply_content(g_team_state.content);
    style::apply_state_container(g_team_state.state_container);
    style::apply_idle_container(g_team_state.idle_container);
    style::apply_active_container(g_team_state.active_container);
    style::apply_status_label(g_team_state.idle_status_label);
    style::apply_status_label(g_team_state.active_status_label);

    style::apply_button_primary(g_team_state.create_btn);
    style::apply_button_secondary(g_team_state.join_btn);
    style::apply_button_secondary(g_team_state.share_btn);
    style::apply_button_secondary(g_team_state.leave_btn);
    style::apply_button_secondary(g_team_state.disband_btn);

    ::ui::widgets::TopBarConfig cfg;
    cfg.height = ::ui::widgets::kTopBarHeight;
    ::ui::widgets::top_bar_init(g_team_state.top_bar_widget, g_team_state.header, cfg);
    update_top_bar_title("Team");
    ::ui::widgets::top_bar_set_back_callback(g_team_state.top_bar_widget, top_bar_back, nullptr);
    ui_update_top_bar_battery(g_team_state.top_bar_widget);

    label_set_text(g_team_state.create_btn, "Create Team");
    label_set_text(g_team_state.join_btn, "Join Team");
    label_set_text(g_team_state.share_btn, "Share Team");
    label_set_text(g_team_state.leave_btn, "Leave Team");
    label_set_text(g_team_state.disband_btn, "Disband Team");

    lv_obj_add_event_cb(g_team_state.create_btn, handle_create, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(g_team_state.join_btn, handle_join, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(g_team_state.share_btn, handle_placeholder, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(g_team_state.leave_btn, handle_placeholder, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(g_team_state.disband_btn, handle_placeholder, LV_EVENT_CLICKED, nullptr);

    init_team_input();
    team_page_refresh();
}

void team_page_destroy()
{
    cleanup_team_input();

    if (g_team_state.root)
    {
        lv_obj_del(g_team_state.root);
        g_team_state.root = nullptr;
    }
    g_team_state = TeamPageState{};
}

void team_page_refresh()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    team::TeamUiState state = controller ? controller->getState() : team::TeamUiState::Idle;
    const char* title = "Team";

    bool idle = (state == team::TeamUiState::Idle);

    if (idle)
    {
        lv_obj_clear_flag(g_team_state.idle_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_team_state.idle_container, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(g_team_state.active_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_team_state.active_container, LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
    else
    {
        lv_obj_add_flag(g_team_state.idle_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_team_state.idle_container, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(g_team_state.active_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_team_state.active_container, LV_OBJ_FLAG_IGNORE_LAYOUT);
    }

    if (idle)
    {
        lv_label_set_text(g_team_state.idle_status_label, "You are not in a Team");
    }
    else
    {
        lv_label_set_text(g_team_state.active_status_label, "Team Active");
    }

    if (state == team::TeamUiState::PendingJoin)
    {
        title = "Join Team";
    }
    else if (state == team::TeamUiState::Active && g_team_state.title_override)
    {
        title = g_team_state.title_override;
    }
    update_top_bar_title(title);

    refresh_team_input();
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
