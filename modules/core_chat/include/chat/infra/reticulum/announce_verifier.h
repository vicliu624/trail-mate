#pragma once
#include "chat/infra/reticulum/reticulum_wire.h"

namespace chat::reticulum
{
// Signature work belongs to a platform crypto port; packet shape, destination
// binding and signed-byte ordering have one shared implementation.
using AnnounceSignatureVerifier = bool (*)(const uint8_t public_key[32], const uint8_t signature[64], const uint8_t*, size_t);
class AnnounceVerifier
{
  public:
    // nullptr means verified. Views borrow the parsed packet for this call.
    const char* verify(const ParsedPacket&, ParsedAnnounce&, uint8_t identity[16], uint8_t destination[16], AnnounceSignatureVerifier);

  private:
    uint8_t signed_scratch_[kReticulumMtu]{};
};
static_assert(sizeof(AnnounceVerifier) == kReticulumMtu, "Preserve embedded verifier scratch budget");
} // namespace chat::reticulum
