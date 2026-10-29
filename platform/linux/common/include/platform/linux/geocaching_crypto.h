#pragma once
#include "geocaching/protocol/verify_record.h"

namespace platform::linux_runtime
{
// Verification only. Does not create/import identities, sign publications,
// change the selected mesh protocol, or interpret a signature as directory trust.
class LinuxGeocachingCrypto final : public ::geocaching::protocol::RecordCrypto
{
  public:
    bool sha256(::geocaching::ByteView input, uint8_t output[32]) override;
    ::geocaching::protocol::VerificationResult verifyEd25519(
        ::geocaching::ByteView key, ::geocaching::ByteView signature, ::geocaching::ByteView message) override;
};
} // namespace platform::linux_runtime
