#include "mesh/protocol/meshtastic/mt_pki_flow.h"

#include "chat/infra/meshtastic/mt_pki_crypto.h"

#include <cstring>

namespace mesh
{
namespace meshtastic
{

ProtocolResult MtPkiFlow::encryptWithSharedSecret(const MtPkiPayloadRequest& request,
                                                  EncodedPacket& out) const
{
    if (request.shared_secret.size != 32 || request.shared_secret.data == nullptr ||
        request.plaintext.empty() || !request.from.isValidUnicast() ||
        request.packet_id == 0)
    {
        return ProtocolResult::fail(ProtocolFailure::InvalidInput);
    }

    constexpr size_t auth_len = 8;
    const size_t needed = request.plaintext.size + auth_len + sizeof(uint32_t);
    if (needed > sizeof(out.bytes))
    {
        return ProtocolResult::fail(ProtocolFailure::EncodeFailed);
    }

    uint8_t shared[32]{};
    std::memcpy(shared, request.shared_secret.data, sizeof(shared));
    ::chat::meshtastic::hashSharedKey(shared, sizeof(shared));

    uint8_t nonce[::chat::meshtastic::kPkiNonceSize]{};
    ::chat::meshtastic::initPkiNonce(request.from.value,
                                     request.packet_id,
                                     request.extra_nonce,
                                     nonce);

    uint8_t auth[16]{};
    if (!::chat::meshtastic::encryptPkiAesCcm(shared,
                                              sizeof(shared),
                                              nonce,
                                              auth_len,
                                              nullptr,
                                              0,
                                              request.plaintext.data,
                                              request.plaintext.size,
                                              out.bytes,
                                              auth))
    {
        return ProtocolResult::fail(ProtocolFailure::CryptoFailed);
    }

    std::memcpy(out.bytes + request.plaintext.size, auth, auth_len);
    std::memcpy(out.bytes + request.plaintext.size + auth_len,
                &request.extra_nonce,
                sizeof(request.extra_nonce));
    out.size = needed;
    return ProtocolResult::success();
}

} // namespace meshtastic
} // namespace mesh
