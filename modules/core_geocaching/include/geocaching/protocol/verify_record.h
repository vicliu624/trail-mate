#pragma once

#include "geocaching/protocol/record_decoder.h"
#include <cstring>

namespace geocaching::protocol
{

enum class VerificationResult : std::uint8_t
{
    Valid,
    InvalidRecord,
    InvalidSignature,
    CryptoUnavailable,
    WorkspaceTooSmall,
    IdentityMismatch,
};

// Implemented with the platform's existing SHA-256 and Ed25519 facilities.
// False from sha256 means unavailable; Ed25519 distinguishes this from invalid.
class RecordCrypto
{
  public:
    virtual ~RecordCrypto() = default;
    virtual bool sha256(ByteView input, std::uint8_t output[32]) = 0;
    virtual VerificationResult verifyEd25519(ByteView public_key, ByteView signature,
                                             ByteView message) = 0;
};

struct VerifiedRecordView
{
    RecordView record;
    GeocacheId id;
    RevisionHash hash;
    ByteView signature;
};

// Computes identifiers for a structurally valid record before author-version
// reservation. Valid here does not mean the record has an authenticated signature.
inline VerificationResult deriveGeocacheHashes(ByteView encoded, RecordCrypto& crypto,
                                               uint8_t* workspace, size_t capacity,
                                               GeocacheId& id, RevisionHash& hash)
{
    id = {};
    hash = {};
    RecordView record;
    if (!decodeGeocacheRecord(encoded, record)) return VerificationResult::InvalidRecord;
    constexpr char id_domain[] = "trailmate.geocache/id/v1";
    constexpr char revision_domain[] = "trailmate.geocache/revision/v1";
    const auto required = sizeof(revision_domain) + encoded.size;
    if (!workspace || capacity < required || capacity < sizeof(id_domain) + 80) return VerificationResult::WorkspaceTooSmall;
    GeocacheId candidate_id;
    RevisionHash candidate_hash;
    std::memcpy(workspace, id_domain, sizeof(id_domain));
    std::memcpy(workspace + sizeof(id_domain), record.author_public_key.data, 64);
    std::memcpy(workspace + sizeof(id_domain) + 64, record.creation_nonce.data, 16);
    if (!crypto.sha256({workspace, sizeof(id_domain) + 80}, candidate_id.bytes.data())) return VerificationResult::CryptoUnavailable;
    std::memcpy(workspace, revision_domain, sizeof(revision_domain));
    std::memcpy(workspace + sizeof(revision_domain), encoded.data, encoded.size);
    if (!crypto.sha256({workspace, required}, candidate_hash.bytes.data())) return VerificationResult::CryptoUnavailable;
    id = candidate_id;
    hash = candidate_hash;
    return VerificationResult::Valid;
}

// Workspace and input must not overlap. Workspace contains public data only.
// The caller owns both; result views keep borrowing input after return.
inline VerificationResult verifyGeocache(ByteView signed_cache, RecordCrypto& crypto,
                                         std::uint8_t* workspace, std::size_t capacity,
                                         VerifiedRecordView& out,
                                         const GeocacheId* expected_id = nullptr,
                                         const RevisionHash* expected_hash = nullptr)
{
    out = {};
    if (!signed_cache.data || signed_cache.size > 4166) return VerificationResult::InvalidRecord;
    CmpReader reader(signed_cache);
    std::size_t count = 0;
    ByteView encoded, signature;
    VerifiedRecordView candidate;
    if (!reader.array(count, 2) || count != 2 ||
        !reader.binary(encoded, kMaxRecordBytes) ||
        !reader.binary(signature, 64) || signature.size != 64 || !reader.finished() ||
        !decodeGeocacheRecord(encoded, candidate.record)) return VerificationResult::InvalidRecord;

    // sizeof includes exactly the required domain-separating NUL byte.
    constexpr char sign_domain[] = "trailmate.geocache/sign/v1";
    constexpr char id_domain[] = "trailmate.geocache/id/v1";
    constexpr char revision_domain[] = "trailmate.geocache/revision/v1";
    const std::size_t required = sizeof(revision_domain) + encoded.size;
    if (!workspace || capacity < required || capacity < sizeof(id_domain) + 80)
        return VerificationResult::WorkspaceTooSmall;

    std::memcpy(workspace, sign_domain, sizeof(sign_domain));
    std::memcpy(workspace + sizeof(sign_domain), encoded.data, encoded.size);
    const auto verified = crypto.verifyEd25519(
        {candidate.record.author_public_key.data + 32, 32}, signature,
        {workspace, sizeof(sign_domain) + encoded.size});
    if (verified != VerificationResult::Valid) return verified;

    const auto derived = deriveGeocacheHashes(encoded, crypto, workspace, capacity, candidate.id, candidate.hash);
    if (derived != VerificationResult::Valid) return derived;
    if ((expected_id && expected_id->bytes != candidate.id.bytes) ||
        (expected_hash && expected_hash->bytes != candidate.hash.bytes))
        return VerificationResult::IdentityMismatch;
    candidate.signature = signature;
    out = candidate;
    return VerificationResult::Valid;
}

} // namespace geocaching::protocol
