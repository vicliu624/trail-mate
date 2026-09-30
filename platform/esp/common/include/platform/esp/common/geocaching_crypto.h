#pragma once

#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "geocaching/protocol/verify_record.h"
#include "platform/esp/common/mbedtls_sha256_compat.h"

namespace platform::esp::common
{

// Uses existing platform crypto, independent of the selected chat backend.
// No identity persistence or radio configuration is touched by verification.
class EspGeocachingCrypto final : public geocaching::protocol::RecordCrypto
{
  public:
    bool sha256(geocaching::ByteView input, std::uint8_t output[32]) override
    {
        if (!output || (!input.data && input.size != 0)) return false;
        mbedtls_sha256_context context;
        mbedtls_sha256_init(&context);
        const bool ok = crypto::sha256_starts(&context, 0) == 0 &&
                        crypto::sha256_update(&context, input.data, input.size) == 0 &&
                        crypto::sha256_finish(&context, output) == 0;
        mbedtls_sha256_free(&context);
        return ok;
    }

    geocaching::protocol::VerificationResult verifyEd25519(
        geocaching::ByteView public_key, geocaching::ByteView signature,
        geocaching::ByteView message) override
    {
        using Result = geocaching::protocol::VerificationResult;
        if (!public_key.data || public_key.size != 32 || !signature.data ||
            signature.size != 64 || (!message.data && message.size != 0))
            return Result::InvalidSignature;
        return ed25519_verify(signature.data, message.data, message.size, public_key.data) != 0
                   ? Result::Valid
                   : Result::InvalidSignature;
    }
};

} // namespace platform::esp::common
