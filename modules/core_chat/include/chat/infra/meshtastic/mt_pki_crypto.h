/**
 * @file mt_pki_crypto.h
 * @brief Shared Meshtastic PKI/AES-CCM crypto helpers
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace chat
{
namespace meshtastic
{

constexpr size_t kPkiNonceSize = 16;

void hashSharedKey(uint8_t* bytes, size_t num_bytes);
void initPkiNonce(uint32_t from,
                  uint64_t packet_id,
                  uint32_t extra_nonce,
                  uint8_t out_nonce[kPkiNonceSize]);
bool decryptPkiAesCcm(const uint8_t* key,
                      size_t key_len,
                      const uint8_t nonce[kPkiNonceSize],
                      size_t auth_len,
                      const uint8_t* crypt,
                      size_t crypt_len,
                      const uint8_t* aad,
                      size_t aad_len,
                      const uint8_t* auth,
                      uint8_t* out_plain);
bool encryptPkiAesCcm(const uint8_t* key,
                      size_t key_len,
                      const uint8_t nonce[kPkiNonceSize],
                      size_t auth_len,
                      const uint8_t* aad,
                      size_t aad_len,
                      const uint8_t* plain,
                      size_t plain_len,
                      uint8_t* out_cipher,
                      uint8_t* out_auth);

bool computeKeyVerificationHashes(uint32_t security_number,
                                  uint64_t nonce,
                                  uint32_t first_node_id,
                                  uint32_t second_node_id,
                                  const uint8_t* first_pubkey,
                                  size_t first_pubkey_len,
                                  const uint8_t* second_pubkey,
                                  size_t second_pubkey_len,
                                  uint8_t out_hash1[32],
                                  uint8_t out_hash2[32]);

} // namespace meshtastic
} // namespace chat
