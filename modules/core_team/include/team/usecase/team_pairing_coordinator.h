#pragma once

#include "../ports/i_team_pairing_event_sink.h"
#include "../ports/i_team_pairing_transport.h"
#include "../ports/i_team_runtime.h"
#include "../protocol/team_mgmt.h"
#include "team_pairing_service.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace team
{

class TeamPairingCoordinator final : public TeamPairingService
{
  public:
    TeamPairingCoordinator(team::ITeamRuntime& runtime,
                           team::ITeamPairingEventSink& event_sink,
                           team::ITeamPairingTransport& transport);

    bool startLeader(const TeamId& team_id,
                     uint32_t key_id,
                     const uint8_t* psk,
                     size_t psk_len,
                     uint32_t leader_id,
                     const char* team_name) override;
    bool startMember(uint32_t self_id) override;
    void stop() override;
    void update() override;

    TeamPairingStatus getStatus() const override;
    void handleIncomingPacket(const uint8_t* mac, const uint8_t* data, size_t len);

  private:
    void setState(TeamPairingState state, uint32_t peer_id = 0);
    void publishState(uint32_t peer_id = 0) const;
    void handleBeacon(const uint8_t* mac, const uint8_t* data, size_t len);
    void handleJoin(const uint8_t* mac, const uint8_t* data, size_t len);
    void handleKey(const uint8_t* mac, const uint8_t* data, size_t len);
    void sendBeacon();
    void sendJoin();
    bool sendKey(const uint8_t* mac, uint32_t member_id, uint32_t nonce);
    void copyTeamName(const char* name);
    uint32_t nextRandomU32();

    team::ITeamRuntime& runtime_;
    team::ITeamPairingEventSink& event_sink_;
    team::ITeamPairingTransport& transport_;

    TeamPairingRole role_ = TeamPairingRole::None;
    TeamPairingState state_ = TeamPairingState::Idle;
    uint32_t state_since_ms_ = 0;
    uint32_t active_until_ms_ = 0;
    uint32_t last_beacon_ms_ = 0;
    uint32_t last_join_ms_ = 0;
    uint32_t join_sent_ms_ = 0;
    uint8_t join_attempts_ = 0;

    TeamId team_id_{};
    bool has_team_id_ = false;
    uint32_t key_id_ = 0;
    uint32_t leader_id_ = 0;
    uint32_t member_id_ = 0;
    uint32_t join_nonce_ = 0;
    std::array<uint8_t, team::proto::kTeamChannelPskSize> team_psk_{};
    uint8_t team_psk_len_ = 0;
    char team_name_[16] = {0};
    bool has_team_name_ = false;

    uint8_t leader_mac_[6] = {0};
    bool leader_mac_valid_ = false;
};

} // namespace team
