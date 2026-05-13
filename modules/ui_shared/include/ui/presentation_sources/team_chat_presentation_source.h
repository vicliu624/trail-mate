#pragma once

#include "ui_presentation/chat/chat_presentation_source.h"

namespace team
{
namespace ui
{
class ITeamUiStore;
}
} // namespace team

namespace ui::presentation_sources
{

// TeamChatPresentationSource is the Phase 5.6-f read adapter for team-scoped
// chat presentation.
//
// Pattern:
//   Bounded Presentation Context / Projection / Anti-Corruption Adapter.
//
// It projects TeamUiStore/TeamUiSnapshot/TeamChatLogEntry state into
// ui_presentation chat rows using ConversationKind::Team.
//
// It must not:
//   - map Team into chat::ConversationId
//   - use toCoreConversationId
//   - send team messages
//   - access LVGL widgets
//   - build radio packets or direct messages
class TeamChatPresentationSource final : public ui::chat::IChatPresentationSource
{
  public:
    explicit TeamChatPresentationSource(::team::ui::ITeamUiStore& team_store);

    bool buildChatWorkspaceSnapshot(
        const ui::chat::ChatWorkspaceRequest& request,
        ui::chat::ChatWorkspaceSnapshot& out) const override;

  private:
    ::team::ui::ITeamUiStore& team_store_;
};

} // namespace ui::presentation_sources
