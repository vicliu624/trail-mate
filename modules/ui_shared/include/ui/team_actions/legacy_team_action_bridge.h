#pragma once

#include "platform/ui/team_ui_store_runtime.h"
#include "ui/presentation_sources/team_chat_action_sink.h"
#include "ui/team_actions/team_action_sink.h"

#include <stdint.h>

namespace ui::team_actions
{

// LegacyTeamActionBridge is a Phase 7.2 anti-corruption bridge.
//
// Pattern:
//   Command Sink / Anti-Corruption Adapter / Strangler Fig.
//
// It translates presentation-level Team actions into existing Team runtime
// helpers.
//
// It must not:
//   - build ChatWorkspaceSnapshot
//   - access LVGL widgets
//   - map Team to DirectPeer/Channel
//   - use LegacyChatActionSink
//   - expose raw packet encoding to ChatUiController
class LegacyTeamActionBridge final : public ITeamActionSink
{
  public:
    LegacyTeamActionBridge(
        ::team::ui::ITeamUiStore& team_store,
        ::ui::presentation_sources::ITeamChatCommandPort* command_port,
        ITeamLocationSource* location_source = nullptr,
        uint8_t team_channel_raw = 2);

    ui::UiActionResult sendTeamAction(
        const TeamActionRequest& request) override;

  private:
    ui::UiActionResult sendText(const TeamActionRequest& request);
    ui::UiActionResult sendLocationMarker(const TeamActionRequest& request);
    ui::UiActionResult sendCommand(const TeamActionRequest& request);

    ::team::ui::ITeamUiStore& team_store_;
    ::ui::presentation_sources::ITeamChatCommandPort* command_port_ = nullptr;
    ITeamLocationSource* location_source_ = nullptr;
    uint8_t team_channel_raw_ = 2;
    uint32_t next_message_id_ = 1;
};

} // namespace ui::team_actions
