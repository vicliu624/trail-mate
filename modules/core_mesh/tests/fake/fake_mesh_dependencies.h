#pragma once

#include "mesh/ports/i_clock.h"
#include "mesh/ports/i_crypto_provider.h"
#include "mesh/ports/i_local_identity_store.h"
#include "mesh/ports/i_mesh_event_sink.h"
#include "mesh/ports/i_packet_radio.h"
#include "mesh/ports/i_peer_key_store.h"
#include "mesh/protocol/mesh_protocol_strategy.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace mesh::tests
{

class FakeClock final : public IClock
{
  public:
    uint32_t nowMs() const override
    {
        return now_ms;
    }

    uint32_t now_ms = 0;
};

class FakeCryptoProvider final : public ICryptoProvider
{
  public:
    CryptoResult random(uint8_t* out, size_t len) override
    {
        if (!out)
        {
            return CryptoResult::fail(CryptoFailure::InvalidInput);
        }
        if (!random_result.ok)
        {
            return random_result;
        }
        for (size_t index = 0; index < len; ++index)
        {
            out[index] = static_cast<uint8_t>(index + 1);
        }
        random_count++;
        return CryptoResult::success();
    }

    CryptoResult sha256(ByteView input, uint8_t out[32]) override
    {
        if (input.empty() || !out)
        {
            return CryptoResult::fail(CryptoFailure::InvalidInput);
        }
        std::memset(out, 0xA5, 32);
        return CryptoResult::success();
    }

    CryptoResult aesCcmEncrypt(ByteView key,
                               ByteView nonce,
                               ByteView aad,
                               ByteView plaintext,
                               uint8_t* ciphertext,
                               size_t ciphertext_capacity,
                               size_t& ciphertext_size) override
    {
        (void)key;
        (void)nonce;
        (void)aad;
        if (plaintext.empty() || !ciphertext || ciphertext_capacity < plaintext.size)
        {
            return CryptoResult::fail(CryptoFailure::InvalidInput);
        }
        std::memcpy(ciphertext, plaintext.data, plaintext.size);
        ciphertext_size = plaintext.size;
        return CryptoResult::success();
    }

    CryptoResult aesCcmDecrypt(ByteView key,
                               ByteView nonce,
                               ByteView aad,
                               ByteView ciphertext,
                               uint8_t* plaintext,
                               size_t plaintext_capacity,
                               size_t& plaintext_size) override
    {
        (void)key;
        (void)nonce;
        (void)aad;
        if (ciphertext.empty() || !plaintext || plaintext_capacity < ciphertext.size)
        {
            return CryptoResult::fail(CryptoFailure::InvalidInput);
        }
        std::memcpy(plaintext, ciphertext.data, ciphertext.size);
        plaintext_size = ciphertext.size;
        return CryptoResult::success();
    }

    int random_count = 0;
    CryptoResult random_result = CryptoResult::success();
};

class FakeLocalIdentityStore final : public ILocalIdentityStore
{
  public:
    StoreResult load(LocalIdentity& out) override
    {
        if (!has_identity)
        {
            return StoreResult::fail(StoreFailure::NotFound);
        }
        out = identity;
        return StoreResult::success();
    }

    StoreResult save(const LocalIdentity& value) override
    {
        identity = value;
        has_identity = true;
        save_count++;
        return save_result;
    }

    StoreResult clear() override
    {
        identity = LocalIdentity{};
        has_identity = false;
        return StoreResult::success();
    }

    LocalIdentity identity{};
    bool has_identity = false;
    int save_count = 0;
    StoreResult save_result = StoreResult::success();
};

class FakePeerKeyStore final : public IPeerKeyStore
{
  public:
    StoreResult get(NodeId node_id, PeerPublicKey& out) override
    {
        for (const auto& key : keys)
        {
            if (key.node_id == node_id)
            {
                out = key;
                return StoreResult::success();
            }
        }
        return StoreResult::fail(StoreFailure::NotFound);
    }

    StoreResult put(const PeerPublicKey& key) override
    {
        for (auto& existing : keys)
        {
            if (existing.node_id == key.node_id)
            {
                existing = key;
                put_count++;
                return StoreResult::success();
            }
        }
        keys.push_back(key);
        put_count++;
        return StoreResult::success();
    }

    StoreResult remove(NodeId node_id) override
    {
        keys.erase(std::remove_if(keys.begin(),
                                  keys.end(),
                                  [node_id](const PeerPublicKey& key)
                                  {
                                      return key.node_id == node_id;
                                  }),
                   keys.end());
        return StoreResult::success();
    }

    StoreResult clear() override
    {
        keys.clear();
        return StoreResult::success();
    }

    std::vector<PeerPublicKey> keys;
    int put_count = 0;
};

class FakeMeshEventSink final : public IMeshEventSink
{
  public:
    void emit(const MeshEvent& event) override
    {
        events.push_back(event);
    }

    int count(MeshEventKind kind) const
    {
        int result = 0;
        for (const auto& event : events)
        {
            if (event.kind == kind)
            {
                result++;
            }
        }
        return result;
    }

    std::vector<MeshEvent> events;
};

class FakePacketRadio final : public IPacketRadio
{
  public:
    RadioResult configure(const RadioConfig& config) override
    {
        last_config = config;
        configure_count++;
        return configure_result;
    }

    RadioResult send(ByteView packet) override
    {
        send_count++;
        last_sent.assign(packet.data, packet.data + packet.size);
        return send_result;
    }

    bool poll(RadioRxPacket& out) override
    {
        if (rx_packets.empty())
        {
            return false;
        }
        out = rx_packets.front();
        rx_packets.erase(rx_packets.begin());
        return true;
    }

    RadioConfig last_config{};
    RadioResult configure_result = RadioResult::success();
    RadioResult send_result = RadioResult::success();
    int configure_count = 0;
    int send_count = 0;
    std::vector<uint8_t> last_sent;
    std::vector<RadioRxPacket> rx_packets;
};

class FakeProtocolStrategy final : public MeshProtocolStrategy
{
  public:
    MeshProtocolKind kind() const override
    {
        return protocol_kind;
    }

    RadioConfig deriveRadioConfig(const MeshRuntimeConfig& config) override
    {
        derive_count++;
        return config.radio;
    }

    ProtocolResult buildDirectMessage(const ProtocolBuildContext& context,
                                      const DirectMessageCommand& command,
                                      EncodedPacket& out) override
    {
        last_context = context;
        last_command = command;
        build_count++;
        if (!build_result.ok)
        {
            return build_result;
        }
        out.size = command.payload.size;
        std::memcpy(out.bytes, command.payload.data, out.size);
        return ProtocolResult::success();
    }

    ProtocolResult parseRadioPacket(const RadioRxPacket& packet,
                                    MeshProtocolEvent& out) override
    {
        (void)packet;
        parse_count++;
        if (!parse_result.ok)
        {
            return parse_result;
        }
        out = next_event;
        return ProtocolResult::success();
    }

    MeshProtocolKind protocol_kind = MeshProtocolKind::Meshtastic;
    ProtocolResult build_result = ProtocolResult::success();
    ProtocolResult parse_result = ProtocolResult::success();
    MeshProtocolEvent next_event{};
    ProtocolBuildContext last_context{};
    DirectMessageCommand last_command{};
    int derive_count = 0;
    int build_count = 0;
    int parse_count = 0;
};

inline LocalIdentity makeIdentity()
{
    LocalIdentity identity{};
    for (size_t index = 0; index < 32; ++index)
    {
        identity.private_key[index] = static_cast<uint8_t>(index + 1);
        identity.public_key[index] = static_cast<uint8_t>(0x80 + index);
    }
    identity.valid = true;
    return identity;
}

inline PeerPublicKey makePeerKey(uint32_t node_id, bool verified = true)
{
    PeerPublicKey key{};
    key.node_id = NodeId{node_id};
    for (size_t index = 0; index < 32; ++index)
    {
        key.public_key[index] = static_cast<uint8_t>(node_id + index);
    }
    key.updated_at_ms = 100;
    key.verified = verified;
    return key;
}

} // namespace mesh::tests
