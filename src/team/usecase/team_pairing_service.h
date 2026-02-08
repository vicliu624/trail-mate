#pragma once

#include "../domain/team_types.h"
#include "../protocol/team_mgmt.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace team
{

struct TeamPairingStatus
{
    TeamPairingRole role = TeamPairingRole::None;
    TeamPairingState state = TeamPairingState::Idle;
    TeamId team_id{};
    bool has_team_id = false;
    uint32_t key_id = 0;
    uint32_t peer_id = 0;
    char team_name[16] = {0};
    bool has_team_name = false;
};

class TeamPairingService
{
  public:
    TeamPairingService();

    bool startLeader(const TeamId& team_id,
                     uint32_t key_id,
                     const uint8_t* psk,
                     size_t psk_len,
                     uint32_t leader_id,
                     const char* team_name);
    bool startMember(uint32_t self_id);
    void stop();
    void update();

    TeamPairingStatus getStatus() const;

  private:
    bool ensureInit();
    void shutdown();
    void setState(TeamPairingState state, uint32_t peer_id = 0);
    void publishState(uint32_t peer_id = 0) const;

    void handleRx(const uint8_t* mac, const uint8_t* data, size_t len);
    void handleBeacon(const uint8_t* mac, const uint8_t* data, size_t len);
    void handleJoin(const uint8_t* mac, const uint8_t* data, size_t len);
    void handleKey(const uint8_t* mac, const uint8_t* data, size_t len);

    void sendBeacon();
    void sendJoin();
    bool sendKey(const uint8_t* mac, uint32_t member_id, uint32_t nonce);

    bool ensurePeer(const uint8_t* mac);

    struct RxPacket
    {
        uint8_t mac[6];
        uint8_t data[128];
        size_t len = 0;
    };

    void copyTeamName(const char* name);

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

    bool initialized_ = false;
    static TeamPairingService* instance_;

    mutable portMUX_TYPE rx_mux_ = portMUX_INITIALIZER_UNLOCKED;
    volatile bool rx_pending_ = false;
    RxPacket rx_packet_{};
};

} // namespace team
