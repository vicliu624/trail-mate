#pragma once

#include "../../chat/ports/i_mesh_adapter.h"
#include "../domain/team_types.h"
#include "../ports/i_team_crypto.h"
#include "../ports/i_team_event_sink.h"
#include "../protocol/team_chat.h"
#include <vector>

namespace team
{

class TeamService
{
  public:
    enum class SendError
    {
        None,
        KeysNotReady,
        EncodeFail,
        EncryptFail,
        MeshSendFail
    };
    TeamService(team::ITeamCrypto& crypto,
                chat::IMeshAdapter& mesh,
                team::ITeamEventSink& sink);

    void setKeys(const TeamKeys& keys);
    void clearKeys();
    bool setKeysFromPsk(const TeamId& team_id, uint32_t key_id,
                        const uint8_t* psk, size_t psk_len);

    void processIncoming();

    bool sendKick(const team::proto::TeamKick& msg,
                  chat::ChannelId channel, chat::NodeId dest = 0, bool want_ack = false);
    bool sendTransferLeader(const team::proto::TeamTransferLeader& msg,
                            chat::ChannelId channel, chat::NodeId dest = 0, bool want_ack = false);
    bool sendKeyDist(const team::proto::TeamKeyDist& msg,
                     chat::ChannelId channel, chat::NodeId dest, bool want_ack = false);
    bool sendKeyDistPlain(const team::proto::TeamKeyDist& msg,
                          chat::ChannelId channel, chat::NodeId dest, bool want_ack = false);
    bool sendStatus(const team::proto::TeamStatus& msg,
                    chat::ChannelId channel, chat::NodeId dest = 0, bool want_ack = false);
    bool sendStatusPlain(const team::proto::TeamStatus& msg,
                         chat::ChannelId channel, chat::NodeId dest = 0, bool want_ack = false);
    bool sendPosition(const std::vector<uint8_t>& payload,
                      chat::ChannelId channel, chat::NodeId dest = 0, bool want_ack = false);
    bool sendWaypoint(const std::vector<uint8_t>& payload,
                      chat::ChannelId channel, chat::NodeId dest = 0, bool want_ack = false);
    bool sendTrack(const std::vector<uint8_t>& payload,
                   chat::ChannelId channel, chat::NodeId dest = 0, bool want_ack = false);
    bool sendChat(const team::proto::TeamChatMessage& msg,
                  chat::ChannelId channel, chat::NodeId dest = 0, bool want_ack = false);
    bool requestNodeInfo(chat::NodeId dest, bool want_response);
    bool startPkiVerification(chat::NodeId dest);
    bool submitPkiNumber(chat::NodeId dest, uint64_t nonce, uint32_t number);
    SendError getLastSendError() const { return last_send_error_; }
    bool hasKeys() const { return keys_.valid; }

  private:
    team::ITeamCrypto& crypto_;
    chat::IMeshAdapter& mesh_;
    team::ITeamEventSink& sink_;
    TeamKeys keys_{};
    SendError last_send_error_ = SendError::None;

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
                       chat::ChannelId channel, chat::NodeId dest, bool want_ack);
    bool sendMgmtEncrypted(const team::proto::TeamMgmtType type,
                           const std::vector<uint8_t>& payload,
                           chat::ChannelId channel, chat::NodeId dest, bool want_ack);

    void emitError(const chat::MeshIncomingData& data,
                   team::TeamProtocolError error,
                   const team::proto::TeamEncrypted* envelope);
};

} // namespace team
