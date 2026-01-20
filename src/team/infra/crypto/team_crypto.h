#pragma once

#include "../../ports/i_team_crypto.h"

namespace team::infra
{

class TeamCrypto : public team::ITeamCrypto
{
  public:
    bool deriveKey(const uint8_t* key, size_t key_len,
                   const char* info,
                   uint8_t* out, size_t out_len) override;

    bool aeadEncrypt(const uint8_t* key, size_t key_len,
                     const uint8_t* nonce, size_t nonce_len,
                     const uint8_t* aad, size_t aad_len,
                     const uint8_t* plain, size_t plain_len,
                     std::vector<uint8_t>& out_cipher) override;

    bool aeadDecrypt(const uint8_t* key, size_t key_len,
                     const uint8_t* nonce, size_t nonce_len,
                     const uint8_t* aad, size_t aad_len,
                     const uint8_t* cipher, size_t cipher_len,
                     std::vector<uint8_t>& out_plain) override;
};

} // namespace team::infra
