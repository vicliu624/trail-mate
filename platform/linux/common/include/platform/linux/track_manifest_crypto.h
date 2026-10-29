#pragma once
#include "platform/linux/geocaching_crypto.h"
#include "track_sharing/verify_manifest.h"

namespace platform::linux_runtime
{
struct LinuxTrackManifestCrypto
{
    track_sharing::ManifestVerification verify(geocaching::ByteView key, geocaching::ByteView signature, geocaching::ByteView message)
    {
        LinuxGeocachingCrypto crypto;
        const auto result = crypto.verifyEd25519(key, signature, message);
        using Result = geocaching::protocol::VerificationResult;
        using Verification = track_sharing::ManifestVerification;
        return result == Result::Valid ? Verification::Valid : result == Result::InvalidSignature ? Verification::InvalidSignature
                                                                                                  : Verification::CryptoUnavailable;
    }
};
} // namespace platform::linux_runtime
