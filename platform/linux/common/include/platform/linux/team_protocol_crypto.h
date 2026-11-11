#pragma once
#include "team/ports/i_team_crypto.h"

namespace platform::linux_runtime
{
// Matches the unchanged ESP TeamCrypto wire contract: SHA256(PSK || label),
// followed by ChaCha20-Poly1305 with a 12-byte nonce and a 16-byte appended tag.
class TeamProtocolCrypto : public team::ITeamCrypto
{
  public:
    bool deriveKey(const uint8_t* key, size_t key_len, const char* info,
                   uint8_t* out, size_t out_len) override;
    bool aeadEncrypt(const uint8_t* key, size_t key_len, const uint8_t* nonce, size_t nonce_len,
                     const uint8_t* aad, size_t aad_len, const uint8_t* plain, size_t plain_len,
                     std::vector<uint8_t>& out) override;
    bool aeadDecrypt(const uint8_t* key, size_t key_len, const uint8_t* nonce, size_t nonce_len,
                     const uint8_t* aad, size_t aad_len, const uint8_t* cipher, size_t cipher_len,
                     std::vector<uint8_t>& out) override;
};
} // namespace platform::linux_runtime
