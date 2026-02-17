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

bool TeamController::onKick(const team::proto::TeamKick& kick,
                            chat::ChannelId channel, chat::NodeId dest, bool want_ack)
{
    return service_.sendKick(kick, channel, dest, want_ack);
}

bool TeamController::onTransferLeader(const team::proto::TeamTransferLeader& transfer,
                                      chat::ChannelId channel, chat::NodeId dest, bool want_ack)
{
    return service_.sendTransferLeader(transfer, channel, dest, want_ack);
}

bool TeamController::onKeyDist(const team::proto::TeamKeyDist& msg,
                               chat::ChannelId channel, chat::NodeId dest, bool want_ack)
{
    return service_.sendKeyDist(msg, channel, dest, want_ack);
}

bool TeamController::onKeyDistPlain(const team::proto::TeamKeyDist& msg,
                                    chat::ChannelId channel, chat::NodeId dest, bool want_ack)
{
    return service_.sendKeyDistPlain(msg, channel, dest, want_ack);
}

bool TeamController::onStatus(const team::proto::TeamStatus& status,
                              chat::ChannelId channel, chat::NodeId dest, bool want_ack)
{
    return service_.sendStatus(status, channel, dest, want_ack);
}

bool TeamController::onStatusPlain(const team::proto::TeamStatus& status,
                                   chat::ChannelId channel, chat::NodeId dest, bool want_ack)
{
    return service_.sendStatusPlain(status, channel, dest, want_ack);
}

bool TeamController::onPosition(const std::vector<uint8_t>& payload,
                                chat::ChannelId channel, chat::NodeId dest, bool want_ack)
{
    return service_.sendPosition(payload, channel, dest, want_ack);
}

bool TeamController::onWaypoint(const std::vector<uint8_t>& payload,
                                chat::ChannelId channel, chat::NodeId dest, bool want_ack)
{
    return service_.sendWaypoint(payload, channel, dest, want_ack);
}

bool TeamController::onTrack(const std::vector<uint8_t>& payload,
                             chat::ChannelId channel, chat::NodeId dest, bool want_ack)
{
    return service_.sendTrack(payload, channel, dest, want_ack);
}

bool TeamController::onChat(const team::proto::TeamChatMessage& msg,
                            chat::ChannelId channel, chat::NodeId dest, bool want_ack)
{
    return service_.sendChat(msg, channel, dest, want_ack);
}

bool TeamController::requestNodeInfo(chat::NodeId dest, bool want_response)
{
    return service_.requestNodeInfo(dest, want_response);
}

bool TeamController::startPkiVerification(chat::NodeId dest)
{
    return service_.startPkiVerification(dest);
}

bool TeamController::submitPkiNumber(chat::NodeId dest, uint64_t nonce, uint32_t number)
{
    return service_.submitPkiNumber(dest, nonce, number);
}

void TeamController::resetUiState()
{
    state_ = TeamUiState::Idle;
}

} // namespace team
