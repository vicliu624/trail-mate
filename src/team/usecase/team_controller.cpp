#include "team_controller.h"

namespace team
{

TeamController::TeamController(TeamService& service)
    : service_(service)
{
}

void TeamController::setKeys(const TeamKeys& keys)
{
    service_.setKeys(keys);
}

void TeamController::clearKeys()
{
    service_.clearKeys();
    state_ = TeamUiState::Idle;
}

bool TeamController::onCreateTeam(const team::proto::TeamAdvertise& advertise,
                                  chat::ChannelId channel)
{
    bool ok = service_.sendAdvertise(advertise, channel);
    if (ok)
    {
        state_ = TeamUiState::Active;
    }
    return ok;
}

bool TeamController::onJoinTeam(const team::proto::TeamJoinRequest& join_request,
                                chat::ChannelId channel, chat::NodeId dest)
{
    bool ok = service_.sendJoinRequest(join_request, channel, dest);
    if (ok)
    {
        state_ = TeamUiState::PendingJoin;
    }
    return ok;
}

bool TeamController::onAcceptJoin(const team::proto::TeamJoinAccept& accept,
                                  chat::ChannelId channel, chat::NodeId dest)
{
    bool ok = service_.sendJoinAccept(accept, channel, dest);
    if (ok)
    {
        state_ = TeamUiState::Active;
    }
    return ok;
}

bool TeamController::onConfirmJoin(const team::proto::TeamJoinConfirm& confirm,
                                   chat::ChannelId channel, chat::NodeId dest)
{
    bool ok = service_.sendJoinConfirm(confirm, channel, dest);
    if (ok)
    {
        state_ = TeamUiState::Active;
    }
    return ok;
}

void TeamController::resetUiState()
{
    state_ = TeamUiState::Idle;
}

} // namespace team
