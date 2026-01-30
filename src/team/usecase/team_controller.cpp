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

bool TeamController::setKeysFromPsk(const TeamId& team_id, uint32_t key_id,
                                    const uint8_t* psk, size_t psk_len)
{
    return service_.setKeysFromPsk(team_id, key_id, psk, psk_len);
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

bool TeamController::onAdvertise(const team::proto::TeamAdvertise& advertise,
                                 chat::ChannelId channel, chat::NodeId dest)
{
    (void)dest;
    return service_.sendAdvertise(advertise, channel);
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

bool TeamController::onJoinDecision(const team::proto::TeamJoinDecision& decision,
                                    chat::ChannelId channel, chat::NodeId dest)
{
    return service_.sendJoinDecision(decision, channel, dest);
}

bool TeamController::onKick(const team::proto::TeamKick& kick,
                            chat::ChannelId channel, chat::NodeId dest)
{
    return service_.sendKick(kick, channel, dest);
}

bool TeamController::onTransferLeader(const team::proto::TeamTransferLeader& transfer,
                                      chat::ChannelId channel, chat::NodeId dest)
{
    return service_.sendTransferLeader(transfer, channel, dest);
}

bool TeamController::onKeyDist(const team::proto::TeamKeyDist& msg,
                               chat::ChannelId channel, chat::NodeId dest)
{
    return service_.sendKeyDist(msg, channel, dest);
}

bool TeamController::onKeyDistPlain(const team::proto::TeamKeyDist& msg,
                                    chat::ChannelId channel, chat::NodeId dest)
{
    return service_.sendKeyDistPlain(msg, channel, dest);
}

bool TeamController::onStatus(const team::proto::TeamStatus& status,
                              chat::ChannelId channel, chat::NodeId dest)
{
    return service_.sendStatus(status, channel, dest);
}

bool TeamController::onStatusPlain(const team::proto::TeamStatus& status,
                                   chat::ChannelId channel, chat::NodeId dest)
{
    return service_.sendStatusPlain(status, channel, dest);
}

bool TeamController::onPosition(const std::vector<uint8_t>& payload,
                                chat::ChannelId channel)
{
    return service_.sendPosition(payload, channel);
}

void TeamController::resetUiState()
{
    state_ = TeamUiState::Idle;
}

} // namespace team
