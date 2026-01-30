/**
 * @file team_nfc.h
 * @brief NFC payload + key exchange helpers (Invite Code protected)
 */

#pragma once

#include "../../domain/team_types.h"
#include "../../protocol/team_mgmt.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace team::nfc
{

constexpr uint8_t kNfcPayloadVersion = 1;
constexpr size_t kNfcSaltSize = 8;
constexpr size_t kNfcNonceSize = 12;
constexpr size_t kNfcTagSize = 16;

struct Payload
{
    TeamId team_id{};
    uint32_t key_id = 0;
    uint32_t expires_at = 0;
    std::array<uint8_t, kNfcSaltSize> salt{};
    std::array<uint8_t, kNfcNonceSize> nonce{};
    std::array<uint8_t, team::proto::kTeamChannelPskSize> cipher{};
    std::array<uint8_t, kNfcTagSize> tag{};
};

bool encode_payload(const Payload& payload, std::vector<uint8_t>& out);
bool decode_payload(const uint8_t* data, size_t len, Payload* out);

bool build_payload(const TeamId& team_id,
                   uint32_t key_id,
                   uint32_t expires_at,
                   const uint8_t* psk,
                   size_t psk_len,
                   const std::string& invite_code,
                   std::vector<uint8_t>& out);

bool decrypt_payload(const Payload& payload,
                     const std::string& invite_code,
                     std::array<uint8_t, team::proto::kTeamChannelPskSize>& out_psk);

bool start_share(const std::vector<uint8_t>& payload);
void stop_share();
void poll_share();
bool start_scan(uint16_t duration_ms);
void stop_scan();
bool poll_scan(std::vector<uint8_t>& out_payload);
bool is_scan_active();
bool is_share_active();

} // namespace team::nfc
