#pragma once
#include "platform/linux/geocaching_crypto.h"
#include "track_sharing/verify_receipt.h"

namespace platform::linux_runtime
{
struct LinuxTrackReceiptCrypto
{
    track_sharing::ReceiptVerification verify(geocaching::ByteView key, geocaching::ByteView signature, geocaching::ByteView message) const
    {
        constexpr char domain[] = "TrailMate.Track.Receipt.v1";
        using Result = track_sharing::ReceiptVerification;
        if (!key.data || key.size != 32 || !signature.data || signature.size != 64 || !message.data ||
            message.size <= sizeof(domain) || message.size > sizeof(domain) + 1024 || std::memcmp(message.data, domain, sizeof(domain))) return Result::InvalidSignature;
        LinuxGeocachingCrypto crypto;
        return crypto.verifyEd25519(key, signature, message) == geocaching::protocol::VerificationResult::Valid ? Result::Valid : Result::InvalidSignature;
    }
};
} // namespace platform::linux_runtime
