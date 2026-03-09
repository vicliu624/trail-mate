#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hostlink
{

struct RxMessageEventPayload
{
    uint32_t msg_id = 0;
    uint32_t from = 0;
    uint32_t to = 0;
    uint8_t channel = 0;
    uint32_t timestamp_s = 0;
    std::string text;
    const uint8_t* meta_tlv = nullptr;
    size_t meta_tlv_len = 0;
};

struct TeamStateMemberSnapshot
{
    uint32_t node_id = 0;
    bool leader = false;
    bool online = false;
    uint32_t last_seen_s = 0;
    std::string name;
};

struct TeamStateSnapshot
{
    bool in_team = false;
    bool pending_join = false;
    bool kicked_out = false;
    bool self_is_leader = false;
    bool has_team_id = false;
    uint32_t self_id = 0;
    const uint8_t* team_id = nullptr;
    size_t team_id_len = 0;
    uint32_t security_round = 0;
    uint32_t last_event_seq = 0;
    uint32_t last_update_s = 0;
    std::string team_name;
    std::vector<TeamStateMemberSnapshot> members;
};

bool build_rx_message_payload(const RxMessageEventPayload& snapshot,
                              std::vector<uint8_t>& out);

bool build_tx_result_payload(uint32_t msg_id, bool success, std::vector<uint8_t>& out);

bool build_team_state_payload(const TeamStateSnapshot& snapshot,
                              std::vector<uint8_t>& out);

uint32_t hash_bytes(const uint8_t* data, size_t len);

} // namespace hostlink
