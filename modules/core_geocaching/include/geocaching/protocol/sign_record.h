#pragma once
#include "geocaching/protocol/cmp_writer.h"
#include "geocaching/protocol/record_decoder.h"
#include <array>

namespace geocaching::protocol
{
// Signer matches the existing LxmfIdentity interface. Identity owns private
// keys. Encoded input must be disjoint from output and workspace. Output may
// reuse workspace: signing consumes the domain-prefixed message before the
// writer overwrites it with SignedCache. All spans are caller-owned.
// The caller reserves/persists the author revision before publishing this
// result; this function alone does not enforce issued-revision uniqueness.
template <class Signer>
bool signGeocacheRecord(ByteView encoded, Signer& identity,
                        uint8_t* workspace, size_t workspace_capacity,
                        uint8_t* output, size_t output_capacity, size_t& written)
{
    written = 0;
    RecordView record;
    if (!identity.isReady() || !decodeGeocacheRecord(encoded, record)) return false;
    constexpr char domain[] = "trailmate.geocache/sign/v1";
    if (!workspace || workspace_capacity < sizeof(domain) + encoded.size || !output) return false;
    std::array<uint8_t, 64> public_key{}, signature{};
    identity.combinedPublicKey(public_key.data());
    if (std::memcmp(public_key.data(), record.author_public_key.data, public_key.size())) return false;
    std::memcpy(workspace, domain, sizeof(domain));
    std::memcpy(workspace + sizeof(domain), encoded.data, encoded.size);
    if (!identity.sign(workspace, sizeof(domain) + encoded.size, signature.data())) return false;
    CmpWriter writer(output, output_capacity < 4166 ? output_capacity : 4166);
    if (!writer.array(2) || !writer.binary(encoded) || !writer.binary({signature.data(), signature.size()})) return false;
    written = writer.size();
    return true;
}
} // namespace geocaching::protocol
