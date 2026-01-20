#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace team
{

class ITeamCrypto
{
  public:
    virtual ~ITeamCrypto() = default;

    virtual bool deriveKey(const uint8_t* key, size_t key_len,
                           const char* info,
                           uint8_t* out, size_t out_len) = 0;

    virtual bool aeadEncrypt(const uint8_t* key, size_t key_len,
                             const uint8_t* nonce, size_t nonce_len,
                             const uint8_t* aad, size_t aad_len,
                             const uint8_t* plain, size_t plain_len,
                             std::vector<uint8_t>& out_cipher) = 0;

    virtual bool aeadDecrypt(const uint8_t* key, size_t key_len,
                             const uint8_t* nonce, size_t nonce_len,
                             const uint8_t* aad, size_t aad_len,
                             const uint8_t* cipher, size_t cipher_len,
                             std::vector<uint8_t>& out_plain) = 0;
};

} // namespace team
