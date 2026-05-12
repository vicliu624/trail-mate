#pragma once

#include "mesh/domain/mesh_packet.h"

#include <stddef.h>
#include <stdint.h>

namespace mesh
{

enum class CryptoFailure : uint8_t
{
    None = 0,
    InvalidInput,
    VerifyFailed,
    EncryptFailed,
    DecryptFailed,
    Unsupported,
};

struct CryptoResult
{
    bool ok = false;
    CryptoFailure failure = CryptoFailure::None;

    CryptoResult() = default;

    CryptoResult(bool result_ok, CryptoFailure result_failure)
        : ok(result_ok),
          failure(result_failure)
    {
    }

    static CryptoResult success()
    {
        return CryptoResult(true, CryptoFailure::None);
    }

    static CryptoResult fail(CryptoFailure failure)
    {
        return CryptoResult(false, failure);
    }
};

class ICryptoProvider
{
  public:
    virtual ~ICryptoProvider() = default;

    virtual CryptoResult random(uint8_t* out, size_t len) = 0;
    virtual CryptoResult sha256(ByteView input, uint8_t out[32]) = 0;

    virtual CryptoResult aesCcmEncrypt(ByteView key,
                                       ByteView nonce,
                                       ByteView aad,
                                       ByteView plaintext,
                                       uint8_t* ciphertext,
                                       size_t ciphertext_capacity,
                                       size_t& ciphertext_size) = 0;

    virtual CryptoResult aesCcmDecrypt(ByteView key,
                                       ByteView nonce,
                                       ByteView aad,
                                       ByteView ciphertext,
                                       uint8_t* plaintext,
                                       size_t plaintext_capacity,
                                       size_t& plaintext_size) = 0;
};

} // namespace mesh
