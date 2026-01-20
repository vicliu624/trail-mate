#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace team::proto
{

constexpr size_t kTeamIdSize = 8;
constexpr size_t kTeamNonceSize = 12;
constexpr uint8_t kTeamEnvelopeVersion = 1;

struct TeamEncrypted
{
    uint8_t version = kTeamEnvelopeVersion;
    uint8_t aad_flags = 0;
    uint32_t key_id = 0;
    std::array<uint8_t, kTeamIdSize> team_id{};
    std::array<uint8_t, kTeamNonceSize> nonce{};
    std::vector<uint8_t> ciphertext;
};

bool encodeTeamEncrypted(const TeamEncrypted& input, std::vector<uint8_t>& out);
bool decodeTeamEncrypted(const uint8_t* data, size_t len, TeamEncrypted* out);

} // namespace team::proto
