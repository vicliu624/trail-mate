#pragma once

#include "../../../../core_chat/include/chat/ports/i_mesh_adapter.h"
#include "../domain/team_types.h"
#include "../ports/i_team_crypto.h"
#include "../ports/i_team_event_sink.h"
#include "../ports/i_team_runtime.h"
#include "../protocol/team_chat.h"
#include "../protocol/team_waypoint.h"
#include <vector>

namespace team
{

class TeamService
{
  public:
    class IncomingDataObserver
    {
      public:
        virtual ~IncomingDataObserver() = default;
        virtual void onIncomingData(const chat::MeshIncomingData& msg) = 0;
    };

    class UnhandledAppDataObserver
    {
      public:
        virtual ~UnhandledAppDataObserver() = default;
        virtual void onUnhandledAppData(const chat::MeshIncomingData& msg) = 0;
    };

    enum class SendError
    {
        None,
        KeysNotReady,
        EncodeFail,
        EncryptFail,
        MeshSendFail,
        UnsupportedByProtocol
    };
    TeamService(team::ITeamCrypto& crypto,
                chat::IMeshAdapter& mesh,
                team::ITeamEventSink& sink,
                team::ITeamRuntime& runtime);

    void setKeys(const TeamKeys& keys);
    void clearKeys();
    bool setKeysFromPsk(const TeamId& team_id, uint32_t key_id,
                        const uint8_t* psk, size_t psk_len);

    void processIncoming();

    void addIncomingDataObserver(IncomingDataObserver* observer);
    void removeIncomingDataObserver(IncomingDataObserver* observer);
    void setUnhandledAppDataObserver(UnhandledAppDataObserver* observer)
    {
        unhandled_app_data_observer_ = observer;
    }

    bool sendKick(const team::proto::TeamKick& msg,
                  chat::ChannelId channel, chat::NodeId dest = 0,
                  bool want_ack = false, bool want_response = false);
    bool sendTransferLeader(const team::proto::TeamTransferLeader& msg,
                            chat::ChannelId channel, chat::NodeId dest = 0,
                            bool want_ack = false, bool want_response = false);
    bool sendKeyDist(const team::proto::TeamKeyDist& msg,
                     chat::ChannelId channel, chat::NodeId dest,
                     bool want_ack = false, bool want_response = false);
    bool sendKeyDistPlain(const team::proto::TeamKeyDist& msg,
                          chat::ChannelId channel, chat::NodeId dest,
                          bool want_ack = false, bool want_response = false);
    bool sendStatus(const team::proto::TeamStatus& msg,
                    chat::ChannelId channel, chat::NodeId dest = 0,
                    bool want_ack = false, bool want_response = false);
    bool sendStatusPlain(const team::proto::TeamStatus& msg,
                         chat::ChannelId channel, chat::NodeId dest = 0,
                         bool want_ack = false, bool want_response = false);
    bool sendPosition(const std::vector<uint8_t>& payload,
                      chat::ChannelId channel, chat::NodeId dest = 0,
                      bool want_ack = false, bool want_response = false);
    bool sendWaypoint(const team::proto::TeamWaypointMessage& msg,
                      chat::ChannelId channel, chat::NodeId dest = 0,
                      bool want_ack = false, bool want_response = false);
    bool sendTrack(const std::vector<uint8_t>& payload,
                   chat::ChannelId channel, chat::NodeId dest = 0,
                   bool want_ack = false, bool want_response = false);
    bool sendChat(const team::proto::TeamChatMessage& msg,
                  chat::ChannelId channel, chat::NodeId dest = 0,
                  bool want_ack = false, bool want_response = false);
    bool requestNodeInfo(chat::NodeId dest, bool want_response);
    bool startPkiVerification(chat::NodeId dest);
    bool submitPkiNumber(chat::NodeId dest, uint64_t nonce, uint32_t number);
    SendError getLastSendError() const { return last_send_error_; }
    bool hasKeys() const { return keys_.valid; }

  private:
    team::ITeamCrypto& crypto_;
    chat::IMeshAdapter& mesh_;
    team::ITeamEventSink& sink_;
    team::ITeamRuntime& runtime_;
    TeamKeys keys_{};
    SendError last_send_error_ = SendError::None;
    std::vector<IncomingDataObserver*> incoming_data_observers_;
    UnhandledAppDataObserver* unhandled_app_data_observer_ = nullptr;
    std::vector<chat::NodeId> team_member_ids_;

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
                       chat::ChannelId channel, chat::NodeId dest,
                       bool want_ack, bool want_response);
    bool sendMgmtEncrypted(const team::proto::TeamMgmtType type,
                           const std::vector<uint8_t>& payload,
                           chat::ChannelId channel, chat::NodeId dest,
                           bool want_ack, bool want_response);
    bool validateSendCapabilities(chat::NodeId dest, bool want_ack);
    void rememberTeamMember(chat::NodeId node_id);
    void updateTeamMemberRoster(const team::proto::TeamStatus& status,
                                chat::NodeId sender_id);
    bool sendMeshAppData(uint32_t portnum,
                         const uint8_t* payload,
                         size_t len,
                         chat::ChannelId channel,
                         chat::NodeId dest,
                         bool want_ack,
                         bool want_response);

    void emitError(const chat::MeshIncomingData& data,
                   team::TeamProtocolError error,
                   const team::proto::TeamEncrypted* envelope);
};

} // namespace team
