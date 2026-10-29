#include "platform/linux/geocaching_crypto.h"
#include <algorithm>
#include <array>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <memory>
#include <openssl/evp.h>
#endif

namespace platform::linux_runtime
{
bool LinuxGeocachingCrypto::sha256(::geocaching::ByteView input, uint8_t output[32])
{
    if (!output) return false;
    if (!input.data && input.size)
    {
        std::fill_n(output, 32, 0);
        return false;
    }
#if defined(TRAIL_MATE_HAS_OPENSSL)
    const uint8_t empty = 0;
    std::array<uint8_t, 32> digest{};
    unsigned size = 0;
    if (EVP_Digest(input.data ? input.data : &empty, input.size, digest.data(), &size, EVP_sha256(), nullptr) == 1 && size == 32)
    {
        std::copy(digest.begin(), digest.end(), output);
        return true;
    }
#endif
    std::fill_n(output, 32, 0);
    return false;
}
::geocaching::protocol::VerificationResult LinuxGeocachingCrypto::verifyEd25519(
    ::geocaching::ByteView key, ::geocaching::ByteView signature, ::geocaching::ByteView message)
{
    using Result = ::geocaching::protocol::VerificationResult;
    if (!key.data || key.size != 32 || !signature.data || signature.size != 64 || (!message.data && message.size))
        return Result::InvalidSignature;
#if defined(TRAIL_MATE_HAS_OPENSSL)
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> public_key(
        EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, key.data, key.size), EVP_PKEY_free);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!public_key || !context || EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, public_key.get()) != 1)
        return Result::CryptoUnavailable;
    const uint8_t empty = 0;
    const int verified = EVP_DigestVerify(context.get(), signature.data, signature.size,
                                          message.data ? message.data : &empty, message.size);
    return verified == 1 ? Result::Valid : verified == 0 ? Result::InvalidSignature
                                                         : Result::CryptoUnavailable;
#else
    return Result::CryptoUnavailable;
#endif
}
} // namespace platform::linux_runtime
