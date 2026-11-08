#pragma once

#include "lvgl.h"
#include "ui/screens/team/team_page_read_model.h"
#include "ui/screens/team/team_page_types.h"
#include "ui/widgets/top_bar.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace team
{
namespace ui
{

class ITeamPageLvglNameResolver
{
  public:
    virtual ~ITeamPageLvglNameResolver() = default;
    virtual std::string resolveNodeName(uint32_t node_id) const = 0;
};

struct TeamPageLvglRendererHandlers
{
    lv_event_cb_t create_team = nullptr;
    lv_event_cb_t join_team = nullptr;
    lv_event_cb_t view_team = nullptr;
    lv_event_cb_t invite = nullptr;
    lv_event_cb_t request_keys = nullptr;
    lv_event_cb_t leave = nullptr;
    lv_event_cb_t manage = nullptr;
    lv_event_cb_t member_clicked = nullptr;
    lv_event_cb_t kick = nullptr;
    lv_event_cb_t transfer_leader = nullptr;
    lv_event_cb_t kick_cancel = nullptr;
    lv_event_cb_t kick_confirm = nullptr;
    lv_event_cb_t join_cancel = nullptr;
    lv_event_cb_t join_retry = nullptr;
    lv_event_cb_t kicked_join = nullptr;
    lv_event_cb_t kicked_ok = nullptr;
};

struct TeamPageLvglRendererContext
{
    lv_obj_t* body = nullptr;
    lv_obj_t* actions = nullptr;
    ::ui::widgets::TopBar* top_bar = nullptr;

    lv_obj_t** action_btns = nullptr;
    std::size_t action_btn_count = 0;
    lv_obj_t** action_labels = nullptr;
    std::size_t action_label_count = 0;
    lv_obj_t** detail_label = nullptr;

    std::vector<lv_obj_t*>* list_items = nullptr;
    std::vector<lv_obj_t*>* focusables = nullptr;
    lv_obj_t** default_focus = nullptr;
};

struct TeamPageLvglRendererInput
{
    TeamPage page = TeamPage::StatusNotInTeam;
    TeamPageReadModelInput read_model;
    uint32_t pairing_peer_id = 0;
    bool pairing_available = true;
};

class TeamPageLvglRenderer
{
  public:
    explicit TeamPageLvglRenderer(uint32_t now_s);

    void render(TeamPageLvglRendererContext& context,
                const TeamPageLvglRendererInput& input,
                const TeamPageLvglRendererHandlers& handlers,
                const ITeamPageLvglNameResolver& names) const;

  private:
    void clearContent(TeamPageLvglRendererContext& context) const;
    void registerFocus(TeamPageLvglRendererContext& context,
                       lv_obj_t* obj,
                       bool is_default = false) const;
    void updateTopBarTitle(TeamPageLvglRendererContext& context,
                           const char* title) const;

    lv_obj_t* addLabel(lv_obj_t* parent,
                       const char* text,
                       bool section = false,
                       bool meta = false) const;
    lv_obj_t* createFittedButton(lv_obj_t* parent,
                                 const char* text,
                                 lv_event_cb_t cb) const;
    lv_obj_t* createActionButton(TeamPageLvglRendererContext& context,
                                 const char* text,
                                 lv_event_cb_t cb) const;
    lv_obj_t* createListItem(TeamPageLvglRendererContext& context,
                             const char* left,
                             const char* right) const;
    lv_obj_t* createMemberListItem(TeamPageLvglRendererContext& context,
                                   const TeamMemberRowView& member) const;

    void renderMemberChipRow(TeamPageLvglRendererContext& context,
                             const std::vector<TeamMemberRowView>& rows) const;
    void renderMemberChipsOrEmpty(
        TeamPageLvglRendererContext& context,
        const TeamPageReadModelInput& input) const;

    void renderStatusNotInTeam(TeamPageLvglRendererContext& context,
                               const TeamPageLvglRendererHandlers& handlers,
                               bool pairing_available) const;
    void renderStatusInTeam(TeamPageLvglRendererContext& context,
                            const TeamPageReadModelInput& input,
                            const TeamPageLvglRendererHandlers& handlers) const;
    void renderTeamHome(TeamPageLvglRendererContext& context,
                        const TeamPageReadModelInput& input,
                        const TeamPageLvglRendererHandlers& handlers) const;
    void renderJoinPending(TeamPageLvglRendererContext& context,
                           const TeamPageLvglRendererInput& input,
                           const TeamPageLvglRendererHandlers& handlers,
                           const ITeamPageLvglNameResolver& names) const;
    void renderMembers(TeamPageLvglRendererContext& context,
                       const TeamPageReadModelInput& input,
                       const TeamPageLvglRendererHandlers& handlers) const;
    void renderMemberDetail(TeamPageLvglRendererContext& context,
                            const TeamPageReadModelInput& input,
                            const TeamPageLvglRendererHandlers& handlers) const;
    void renderKickConfirm(TeamPageLvglRendererContext& context,
                           const TeamPageReadModelInput& input,
                           const TeamPageLvglRendererHandlers& handlers) const;
    void renderKickedOut(TeamPageLvglRendererContext& context,
                         const TeamPageLvglRendererHandlers& handlers) const;

    std::string formatLastSeen(TeamRelativeTimeView view) const;
    std::string formatLastUpdate(TeamRelativeTimeView view) const;

    uint32_t now_s_ = 0;
};

} // namespace ui
} // namespace team
