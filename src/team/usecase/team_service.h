#pragma once

#include "../../chat/ports/i_mesh_adapter.h"
#include "../domain/team_types.h"
#include "../ports/i_team_crypto.h"
#include "../ports/i_team_event_sink.h"
#include <vector>

namespace team
{

class TeamService
{
  public:
    TeamService(team::ITeamCrypto& crypto,
                chat::IMeshAdapter& mesh,
                team::ITeamEventSink& sink);

    void setKeys(const TeamKeys& keys);
    void clearKeys();

    void processIncoming();

    bool sendAdvertise(const team::proto::TeamAdvertise& msg,
                       chat::ChannelId channel);
    bool sendJoinRequest(const team::proto::TeamJoinRequest& msg,
                         chat::ChannelId channel, chat::NodeId dest = 0);
    bool sendJoinAccept(const team::proto::TeamJoinAccept& msg,
                        chat::ChannelId channel, chat::NodeId dest);
    bool sendJoinConfirm(const team::proto::TeamJoinConfirm& msg,
                         chat::ChannelId channel, chat::NodeId dest = 0);
    bool sendStatus(const team::proto::TeamStatus& msg,
                    chat::ChannelId channel, chat::NodeId dest = 0);
    bool sendPosition(const std::vector<uint8_t>& payload,
                      chat::ChannelId channel);
    bool sendWaypoint(const std::vector<uint8_t>& payload,
                      chat::ChannelId channel);

  private:
    team::ITeamCrypto& crypto_;
    chat::IMeshAdapter& mesh_;
    team::ITeamEventSink& sink_;
    TeamKeys keys_{};

    bool decodeEncryptedPayload(const chat::MeshIncomingData& data,
                                const uint8_t* key, size_t key_len,
                                team::proto::TeamEncrypted* envelope,
                                std::vector<uint8_t>& out_plain,
                                bool emit_errors);

    bool encodeEncryptedPayload(const std::vector<uint8_t>& plain,
                                const uint8_t* key, size_t key_len,
                                team::proto::TeamEncrypted* envelope,
                                std::vector<uint8_t>& out_wire);

    bool sendMgmtPlain(const team::proto::TeamMgmtType type,
                       const std::vector<uint8_t>& payload,
                       chat::ChannelId channel, chat::NodeId dest);
    bool sendMgmtEncrypted(const team::proto::TeamMgmtType type,
                           const std::vector<uint8_t>& payload,
                           chat::ChannelId channel, chat::NodeId dest);

    void emitError(const chat::MeshIncomingData& data,
                   team::TeamProtocolError error,
                   const team::proto::TeamEncrypted* envelope);
};

} // namespace team
