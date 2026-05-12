#include "mesh/protocol/meshcore/mc_identity_flow.h"

#include "chat/infra/meshcore/meshcore_identity_crypto.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"

namespace mesh
{
namespace meshcore
{

namespace
{

bool hasSize(ByteView view, size_t expected)
{
    return view.data != nullptr && view.size == expected;
}

} // namespace

ProtocolResult McIdentityFlow::createKeypair(
    ByteView seed,
    uint8_t out_public_key[kMeshCorePublicKeySize],
    uint8_t out_private_key[kMeshCorePrivateKeySize]) const
{
    if (!hasSize(seed, kMeshCoreSeedSize) || !out_public_key || !out_private_key)
    {
        return ProtocolResult::fail(ProtocolFailure::InvalidInput);
    }

    if (!chat::meshcore::meshcoreCreateKeypair(seed.data, out_public_key, out_private_key))
    {
        return ProtocolResult::fail(ProtocolFailure::CryptoFailed);
    }

    return ProtocolResult::success();
}

ProtocolResult McIdentityFlow::derivePublicKey(
    ByteView private_key,
    uint8_t out_public_key[kMeshCorePublicKeySize]) const
{
    if (!hasSize(private_key, kMeshCorePrivateKeySize) || !out_public_key)
    {
        return ProtocolResult::fail(ProtocolFailure::InvalidInput);
    }

    if (!chat::meshcore::meshcoreDerivePublicKey(private_key.data, out_public_key))
    {
        return ProtocolResult::fail(ProtocolFailure::CryptoFailed);
    }

    return ProtocolResult::success();
}

ProtocolResult McIdentityFlow::sign(
    const MeshCoreKeyPairView& keypair,
    ByteView message,
    uint8_t out_signature[kMeshCoreSignatureSize]) const
{
    if (!keypair.private_key || keypair.private_key_size != kMeshCorePrivateKeySize ||
        !keypair.public_key || keypair.public_key_size != kMeshCorePublicKeySize ||
        message.empty() || !out_signature)
    {
        return ProtocolResult::fail(ProtocolFailure::InvalidInput);
    }

    if (!chat::meshcore::meshcoreSign(keypair.private_key,
                                      keypair.public_key,
                                      message.data,
                                      message.size,
                                      out_signature))
    {
        return ProtocolResult::fail(ProtocolFailure::CryptoFailed);
    }

    return ProtocolResult::success();
}

ProtocolResult McIdentityFlow::verify(ByteView public_key,
                                      ByteView signature,
                                      ByteView message) const
{
    if (!hasSize(public_key, kMeshCorePublicKeySize) ||
        !hasSize(signature, kMeshCoreSignatureSize) ||
        message.empty())
    {
        return ProtocolResult::fail(ProtocolFailure::InvalidInput);
    }

    if (!chat::meshcore::meshcoreVerify(public_key.data,
                                        signature.data,
                                        message.data,
                                        message.size))
    {
        return ProtocolResult::fail(ProtocolFailure::CryptoFailed);
    }

    return ProtocolResult::success();
}

ProtocolResult McIdentityFlow::deriveSharedSecret(
    ByteView private_key,
    ByteView peer_public_key,
    uint8_t out_secret[kMeshCorePublicKeySize]) const
{
    if (!hasSize(private_key, kMeshCorePrivateKeySize) ||
        !hasSize(peer_public_key, kMeshCorePublicKeySize) ||
        !out_secret)
    {
        return ProtocolResult::fail(ProtocolFailure::InvalidInput);
    }

    if (!chat::meshcore::meshcoreDeriveSharedSecret(private_key.data,
                                                    peer_public_key.data,
                                                    out_secret))
    {
        return ProtocolResult::fail(ProtocolFailure::CryptoFailed);
    }

    return ProtocolResult::success();
}

NodeId McIdentityFlow::deriveNodeId(ByteView public_key) const
{
    if (!hasSize(public_key, kMeshCorePublicKeySize))
    {
        return NodeId{};
    }

    return NodeId{chat::meshcore::deriveNodeIdFromPubkey(public_key.data, public_key.size)};
}

} // namespace meshcore
} // namespace mesh
