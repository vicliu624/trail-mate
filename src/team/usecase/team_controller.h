#pragma once

#include "../../chat/domain/chat_types.h"
#include "../domain/team_types.h"
#include "../protocol/team_mgmt.h"
#include "team_service.h"

namespace team
{

enum class TeamUiState
{
    Idle,
    PendingJoin,
    Active
};

class TeamController
{
  public:
    explicit TeamController(TeamService& service);

    void setKeys(const TeamKeys& keys);
    void clearKeys();

    bool onCreateTeam(const team::proto::TeamAdvertise& advertise,
                      chat::ChannelId channel);
    bool onJoinTeam(const team::proto::TeamJoinRequest& join_request,
                    chat::ChannelId channel, chat::NodeId dest = 0);
    bool onAcceptJoin(const team::proto::TeamJoinAccept& accept,
                      chat::ChannelId channel, chat::NodeId dest);
    bool onConfirmJoin(const team::proto::TeamJoinConfirm& confirm,
                       chat::ChannelId channel, chat::NodeId dest = 0);

    TeamUiState getState() const { return state_; }
    void resetUiState();

  private:
    TeamService& service_;
    TeamUiState state_ = TeamUiState::Idle;
};

} // namespace team
