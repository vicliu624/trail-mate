#pragma once

#include "chat/infra/meshcore/meshcore_identity_crypto.h"

#include <cstddef>
#include <cstdint>

namespace chat::meshcore
{

class IMeshCoreBleBackend
{
  public:
    virtual ~IMeshCoreBleBackend() = default;

    virtual bool exportIdentityPublicKey(uint8_t out_pubkey[kMeshCorePubKeySize]) = 0;
    virtual bool exportIdentityPrivateKey(uint8_t out_priv[kMeshCorePrivKeySize]) = 0;
    virtual bool importIdentityPrivateKey(const uint8_t* in_priv, size_t len) = 0;
    virtual bool signPayload(const uint8_t* data, size_t len,
                             uint8_t out_sig[kMeshCoreSignatureSize]) = 0;
    virtual bool sendSelfAdvert(bool broadcast) = 0;
    virtual bool sendPeerRequestType(const uint8_t* pubkey, size_t len, uint8_t req_type,
                                     uint32_t* out_tag, uint32_t* out_est_timeout,
                                     bool* out_sent_flood) = 0;
    virtual bool sendPeerRequestPayload(const uint8_t* pubkey, size_t len,
                                        const uint8_t* payload, size_t payload_len,
                                        bool force_flood,
                                        uint32_t* out_tag, uint32_t* out_est_timeout,
                                        bool* out_sent_flood) = 0;
    virtual bool sendAnonRequestPayload(const uint8_t* pubkey, size_t len,
                                        const uint8_t* payload, size_t payload_len,
                                        uint32_t* out_est_timeout,
                                        bool* out_sent_flood) = 0;
    virtual bool sendTracePath(const uint8_t* path, size_t path_len,
                               uint32_t tag, uint32_t auth, uint8_t flags,
                               uint32_t* out_est_timeout) = 0;
    virtual bool sendControlData(const uint8_t* payload, size_t payload_len) = 0;
    virtual bool sendRawData(const uint8_t* path, size_t path_len,
                             const uint8_t* payload, size_t payload_len,
                             uint32_t* out_est_timeout) = 0;
    virtual void setFloodScopeKey(const uint8_t* key, size_t len) = 0;
};

} // namespace chat::meshcore
