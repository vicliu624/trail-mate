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
    bool setKeysFromPsk(const TeamId& team_id, uint32_t key_id,
                        const uint8_t* psk, size_t psk_len);

    bool onKick(const team::proto::TeamKick& kick,
                chat::ChannelId channel, chat::NodeId dest = 0);
    bool onTransferLeader(const team::proto::TeamTransferLeader& transfer,
                          chat::ChannelId channel, chat::NodeId dest = 0);
    bool onKeyDist(const team::proto::TeamKeyDist& msg,
                   chat::ChannelId channel, chat::NodeId dest);
    bool onKeyDistPlain(const team::proto::TeamKeyDist& msg,
                        chat::ChannelId channel, chat::NodeId dest);
    bool onStatus(const team::proto::TeamStatus& status,
                  chat::ChannelId channel, chat::NodeId dest = 0);
    bool onStatusPlain(const team::proto::TeamStatus& status,
                       chat::ChannelId channel, chat::NodeId dest = 0);
    bool onPosition(const std::vector<uint8_t>& payload,
                    chat::ChannelId channel);
    bool onTrack(const std::vector<uint8_t>& payload,
                 chat::ChannelId channel);
    bool onChat(const team::proto::TeamChatMessage& msg,
                chat::ChannelId channel);
    bool requestNodeInfo(chat::NodeId dest, bool want_response);
    bool startPkiVerification(chat::NodeId dest);
    bool submitPkiNumber(chat::NodeId dest, uint64_t nonce, uint32_t number);
    TeamService::SendError getLastSendError() const { return service_.getLastSendError(); }

    TeamUiState getState() const { return state_; }
    void resetUiState();

  private:
    TeamService& service_;
    TeamUiState state_ = TeamUiState::Idle;
};

} // namespace team
