#include "platform/nrf52/arduino_common/chat/infra/meshcore/meshcore_radio_adapter.h"

#include "chat/infra/meshcore/meshcore_identity_crypto.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"
#include "chat/runtime/meshcore_self_announcement_core.h"
#include "chat/runtime/self_identity_policy.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"
#include "platform/nrf52/arduino_common/device_identity.h"

#include <Arduino.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace platform::nrf52::arduino_common::chat::meshcore
{
namespace
{
constexpr uint8_t kRouteTypeFlood = 0x01;
constexpr uint8_t kRouteTypeDirect = 0x02;
constexpr uint8_t kPayloadTypeReq = 0x00;
constexpr uint8_t kPayloadTypeDirectData = 0x07;
constexpr uint8_t kPayloadTypeGrpData = 0x06;
constexpr uint8_t kPayloadTypeTrace = 0x09;
constexpr uint8_t kPayloadTypeControl = 0x0B;
constexpr uint8_t kPayloadTypeRawCustom = 0x0F;
constexpr uint8_t kDirectAppMagic0 = 0xDA;
constexpr uint8_t kDirectAppMagic1 = 0x7A;
constexpr uint8_t kDirectAppFlagWantAck = 0x01;
constexpr uint8_t kGroupDataMagic0 = 0x47;
constexpr uint8_t kGroupDataMagic1 = 0x44;
constexpr size_t kMeshcoreMaxFrameSize = 255;
constexpr size_t kMeshcoreMaxPayloadSize = 220;

uint32_t estimateTimeoutMs(const ::chat::MeshConfig& cfg, size_t frame_len, size_t path_len, bool flood)
{
    return ::chat::meshcore::estimateSendTimeoutMs(frame_len,
                                                   path_len,
                                                   flood,
                                                   cfg.meshcore_bw_khz,
                                                   cfg.meshcore_sf,
                                                   cfg.meshcore_cr);
}

bool isPrintableTextPayload(const uint8_t* data, size_t len)
{
    if (!data || len == 0)
    {
        return false;
    }
    for (size_t index = 0; index < len; ++index)
    {
        const uint8_t ch = data[index];
        if (ch == '\n' || ch == '\r' || ch == '\t')
        {
            continue;
        }
        if (ch < 0x20 || ch > 0x7EU)
        {
            return false;
        }
    }
    return true;
}

} // namespace

MeshCoreRadioAdapter::MeshCoreRadioAdapter(const ::chat::runtime::SelfIdentityProvider* identity_provider)
    : node_id_(device_identity::getSelfNodeId()),
      identity_provider_(identity_provider)
{
}

::chat::MeshCapabilities MeshCoreRadioAdapter::getCapabilities() const
{
    ::chat::MeshCapabilities caps{};
    caps.supports_unicast_appdata = true;
    caps.supports_broadcast_appdata = true;
    caps.supports_discovery_actions = true;
    return caps;
}

bool MeshCoreRadioAdapter::sendText(::chat::ChannelId channel, const std::string& text,
                                    ::chat::MessageId* out_msg_id, ::chat::NodeId peer)
{
    if (!out_msg_id)
    {
        static ::chat::MessageId sink = 0;
        out_msg_id = &sink;
    }
    *out_msg_id = millis();
    return sendAppData(channel,
                       0x1001,
                       reinterpret_cast<const uint8_t*>(text.data()),
                       text.size(),
                       peer,
                       false,
                       *out_msg_id,
                       false);
}

bool MeshCoreRadioAdapter::pollIncomingText(::chat::MeshIncomingText* out)
{
    if (!out || text_queue_.empty())
    {
        return false;
    }
    *out = text_queue_.front();
    text_queue_.pop();
    return true;
}

bool MeshCoreRadioAdapter::sendAppData(::chat::ChannelId channel, uint32_t portnum,
                                       const uint8_t* payload, size_t len,
                                       ::chat::NodeId dest, bool want_ack,
                                       ::chat::MessageId packet_id,
                                       bool want_response)
{
    (void)channel;
    (void)packet_id;
    (void)want_response;
    if (!payload || len == 0 || !config_.tx_enabled)
    {
        return false;
    }

    uint8_t frame[255] = {};
    size_t frame_len = 0;

    if (dest != 0)
    {
        uint8_t plain[220] = {};
        size_t plain_len = 0;
        plain[plain_len++] = kDirectAppMagic0;
        plain[plain_len++] = kDirectAppMagic1;
        plain[plain_len++] = want_ack ? kDirectAppFlagWantAck : 0x00;
        std::memcpy(&plain[plain_len], &portnum, sizeof(portnum));
        plain_len += sizeof(portnum);
        const size_t body_len = std::min(len, sizeof(plain) - plain_len);
        std::memcpy(&plain[plain_len], payload, body_len);
        plain_len += body_len;

        if (!::chat::meshcore::buildFrameNoTransport(kRouteTypeFlood,
                                                     kPayloadTypeDirectData,
                                                     nullptr,
                                                     0,
                                                     plain,
                                                     plain_len,
                                                     frame,
                                                     sizeof(frame),
                                                     &frame_len))
        {
            return false;
        }
        return transmitFrame(frame, frame_len);
    }

    uint8_t plain[220] = {};
    size_t plain_len = 0;
    plain[plain_len++] = kGroupDataMagic0;
    plain[plain_len++] = kGroupDataMagic1;
    std::memcpy(&plain[plain_len], &node_id_, sizeof(node_id_));
    plain_len += sizeof(node_id_);
    std::memcpy(&plain[plain_len], &portnum, sizeof(portnum));
    plain_len += sizeof(portnum);
    const size_t body_len = std::min(len, sizeof(plain) - plain_len);
    std::memcpy(&plain[plain_len], payload, body_len);
    plain_len += body_len;

    if (!::chat::meshcore::buildFrameNoTransport(kRouteTypeFlood,
                                                 kPayloadTypeGrpData,
                                                 nullptr,
                                                 0,
                                                 plain,
                                                 plain_len,
                                                 frame,
                                                 sizeof(frame),
                                                 &frame_len))
    {
        return false;
    }
    return transmitFrame(frame, frame_len);
}

bool MeshCoreRadioAdapter::pollIncomingData(::chat::MeshIncomingData* out)
{
    if (!out || data_queue_.empty())
    {
        return false;
    }
    *out = data_queue_.front();
    data_queue_.pop();
    return true;
}

bool MeshCoreRadioAdapter::requestNodeInfo(::chat::NodeId dest, bool want_response)
{
    (void)dest;
    (void)want_response;
    return sendAdvert(true);
}

bool MeshCoreRadioAdapter::triggerDiscoveryAction(::chat::MeshDiscoveryAction action)
{
    switch (action)
    {
    case ::chat::MeshDiscoveryAction::SendIdLocal:
        return sendAdvert(false);
    case ::chat::MeshDiscoveryAction::SendIdBroadcast:
    case ::chat::MeshDiscoveryAction::ScanLocal:
    default:
        return sendAdvert(true);
    }
}

void MeshCoreRadioAdapter::applyConfig(const ::chat::MeshConfig& config)
{
    config_ = config;
}

void MeshCoreRadioAdapter::setUserInfo(const char* long_name, const char* short_name)
{
    long_name_ = long_name ? long_name : "";
    short_name_ = short_name ? short_name : "";
}

void MeshCoreRadioAdapter::setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent)
{
    (void)duty_cycle_enabled;
    (void)util_percent;
}

void MeshCoreRadioAdapter::setPrivacyConfig(uint8_t encrypt_mode)
{
    (void)encrypt_mode;
}

bool MeshCoreRadioAdapter::isReady() const
{
    return ::platform::nrf52::arduino_common::chat::infra::radioPacketIo() != nullptr;
}

::chat::NodeId MeshCoreRadioAdapter::getNodeId() const
{
    return node_id_;
}

bool MeshCoreRadioAdapter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    (void)out_data;
    (void)max_len;
    out_len = 0;
    return false;
}

void MeshCoreRadioAdapter::handleRawPacket(const uint8_t* data, size_t size)
{
    if (!data || size == 0)
    {
        return;
    }

    ::chat::meshcore::ParsedPacket parsed{};
    if (!::chat::meshcore::parsePacket(data, size, &parsed))
    {
        return;
    }

    if (parsed.payload_type == 0x04)
    {
        ::chat::meshcore::DecodedAdvertAppData advert{};
        if (::chat::meshcore::decodeAdvertAppData(parsed.payload, parsed.payload_len, &advert) &&
            advert.valid && advert.has_name)
        {
            ::chat::MeshIncomingText incoming{};
            incoming.text = advert.name;
            text_queue_.push(std::move(incoming));
        }
        return;
    }

    ::chat::meshcore::DecodedDirectAppPayload direct_payload{};
    if (::chat::meshcore::decodeDirectAppPayload(parsed.payload, parsed.payload_len, &direct_payload) &&
        direct_payload.payload && direct_payload.payload_len > 0)
    {
        ::chat::MeshIncomingData incoming{};
        incoming.from = node_id_;
        incoming.to = 0;
        incoming.portnum = direct_payload.portnum;
        incoming.payload.assign(direct_payload.payload,
                                direct_payload.payload + direct_payload.payload_len);
        data_queue_.push(incoming);

        if (direct_payload.portnum == 0x1001 &&
            isPrintableTextPayload(direct_payload.payload, direct_payload.payload_len))
        {
            ::chat::MeshIncomingText text{};
            text.from = incoming.from;
            text.to = incoming.to;
            text.channel = ::chat::ChannelId::PRIMARY;
            text.text.assign(reinterpret_cast<const char*>(direct_payload.payload),
                             direct_payload.payload_len);
            text_queue_.push(std::move(text));
        }
        return;
    }

    ::chat::meshcore::DecodedGroupAppPayload group_payload{};
    if (::chat::meshcore::decodeGroupAppPayload(parsed.payload, parsed.payload_len, &group_payload) &&
        group_payload.payload && group_payload.payload_len > 0)
    {
        ::chat::MeshIncomingData incoming{};
        incoming.from = group_payload.sender;
        incoming.to = 0xFFFFFFFFUL;
        incoming.portnum = group_payload.portnum;
        incoming.payload.assign(group_payload.payload,
                                group_payload.payload + group_payload.payload_len);
        data_queue_.push(incoming);

        if (group_payload.portnum == 0x1001 &&
            isPrintableTextPayload(group_payload.payload, group_payload.payload_len))
        {
            ::chat::MeshIncomingText text{};
            text.from = incoming.from;
            text.to = incoming.to;
            text.channel = ::chat::ChannelId::PRIMARY;
            text.text.assign(reinterpret_cast<const char*>(group_payload.payload),
                             group_payload.payload_len);
            text_queue_.push(std::move(text));
        }
    }
}

void MeshCoreRadioAdapter::setLastRxStats(float rssi, float snr)
{
    (void)rssi;
    (void)snr;
}

void MeshCoreRadioAdapter::processSendQueue()
{
}

bool MeshCoreRadioAdapter::exportIdentityPublicKey(uint8_t out_pubkey[::chat::meshcore::kMeshCorePubKeySize])
{
    return exportIdentityPublicKey(out_pubkey, ::chat::meshcore::kMeshCorePubKeySize);
}

bool MeshCoreRadioAdapter::exportIdentityPublicKey(uint8_t* out_key, size_t out_len)
{
    if (!out_key || out_len < sizeof(public_key_))
    {
        return false;
    }
    ensureIdentityKeys();
    if (!keys_ready_)
    {
        return false;
    }
    std::memcpy(out_key, public_key_, sizeof(public_key_));
    return true;
}

bool MeshCoreRadioAdapter::exportIdentityPrivateKey(uint8_t out_priv[::chat::meshcore::kMeshCorePrivKeySize])
{
    return exportIdentityPrivateKey(out_priv, ::chat::meshcore::kMeshCorePrivKeySize);
}

bool MeshCoreRadioAdapter::exportIdentityPrivateKey(uint8_t* out_key, size_t out_len)
{
    if (!out_key || out_len < sizeof(private_key_))
    {
        return false;
    }
    ensureIdentityKeys();
    if (!keys_ready_)
    {
        return false;
    }
    std::memcpy(out_key, private_key_, sizeof(private_key_));
    return true;
}

bool MeshCoreRadioAdapter::importIdentityPrivateKey(const uint8_t* key, size_t len)
{
    if (!key || len < sizeof(private_key_))
    {
        return false;
    }
    std::memcpy(private_key_, key, sizeof(private_key_));
    keys_ready_ = ::chat::meshcore::meshcoreDerivePublicKey(private_key_, public_key_);
    return keys_ready_;
}

bool MeshCoreRadioAdapter::signPayload(const uint8_t* payload, size_t len,
                                       uint8_t out_signature[::chat::meshcore::kMeshCoreSignatureSize])
{
    return signPayload(payload, len, out_signature, ::chat::meshcore::kMeshCoreSignatureSize);
}

bool MeshCoreRadioAdapter::signPayload(const uint8_t* payload, size_t len, uint8_t* out_signature, size_t out_len)
{
    if (!payload || len == 0 || !out_signature || out_len < ::chat::meshcore::kMeshCoreSignatureSize)
    {
        return false;
    }
    ensureIdentityKeys();
    if (!keys_ready_)
    {
        return false;
    }
    return ::chat::meshcore::meshcoreSign(private_key_, public_key_, payload, len, out_signature);
}

bool MeshCoreRadioAdapter::sendSelfAdvert(bool broadcast)
{
    return sendAdvert(broadcast);
}

bool MeshCoreRadioAdapter::sendPeerRequestType(const uint8_t* pubkey, size_t len, uint8_t req_type,
                                               uint32_t* out_tag, uint32_t* out_est_timeout,
                                               bool* out_sent_flood)
{
    uint8_t payload[9] = {};
    payload[0] = req_type;
    const uint32_t nonce = static_cast<uint32_t>(millis());
    std::memcpy(payload + 5, &nonce, sizeof(nonce));
    return sendPeerRequestPayload(pubkey,
                                  len,
                                  payload,
                                  sizeof(payload),
                                  false,
                                  out_tag,
                                  out_est_timeout,
                                  out_sent_flood);
}

bool MeshCoreRadioAdapter::sendPeerRequestPayload(const uint8_t* pubkey, size_t len,
                                                  const uint8_t* payload, size_t payload_len,
                                                  bool force_flood,
                                                  uint32_t* out_tag, uint32_t* out_est_timeout,
                                                  bool* out_sent_flood)
{
    if (!pubkey || len != sizeof(public_key_) || !payload || payload_len == 0)
    {
        return false;
    }

    ensureIdentityKeys();
    if (!keys_ready_)
    {
        return false;
    }

    uint8_t shared_secret[::chat::meshcore::kMeshCorePubKeySize] = {};
    if (!::chat::meshcore::meshcoreDeriveSharedSecret(private_key_, pubkey, shared_secret))
    {
        return false;
    }

    uint8_t key16[16] = {};
    uint8_t key32[32] = {};
    ::chat::meshcore::sharedSecretToKeys(shared_secret, key16, key32);

    uint8_t plain[kMeshcoreMaxPayloadSize] = {};
    size_t plain_len = 0;
    const uint32_t tag = static_cast<uint32_t>(millis());
    std::memcpy(plain + plain_len, &tag, sizeof(tag));
    plain_len += sizeof(tag);
    if (plain_len + payload_len > sizeof(plain))
    {
        return false;
    }
    std::memcpy(plain + plain_len, payload, payload_len);
    plain_len += payload_len;

    uint8_t datagram[kMeshcoreMaxPayloadSize] = {};
    size_t datagram_len = 0;
    if (!::chat::meshcore::buildPeerDatagramPayload(pubkey[0],
                                                    public_key_[0],
                                                    key16,
                                                    key32,
                                                    plain,
                                                    plain_len,
                                                    datagram,
                                                    sizeof(datagram),
                                                    &datagram_len))
    {
        return false;
    }

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    const uint8_t route_type = force_flood ? kRouteTypeFlood : kRouteTypeFlood;
    if (!::chat::meshcore::buildFrameNoTransport(route_type,
                                                 kPayloadTypeReq,
                                                 nullptr,
                                                 0,
                                                 datagram,
                                                 datagram_len,
                                                 frame,
                                                 sizeof(frame),
                                                 &frame_len))
    {
        return false;
    }

    if (!transmitFrame(frame, frame_len))
    {
        return false;
    }

    if (out_tag)
    {
        *out_tag = tag;
    }
    if (out_est_timeout)
    {
        *out_est_timeout = estimateTimeoutMs(config_, frame_len, 0, true);
    }
    if (out_sent_flood)
    {
        *out_sent_flood = true;
    }
    return true;
}

bool MeshCoreRadioAdapter::sendAnonRequestPayload(const uint8_t* pubkey, size_t len,
                                                  const uint8_t* payload, size_t payload_len,
                                                  uint32_t* out_est_timeout,
                                                  bool* out_sent_flood)
{
    if (!pubkey || len != sizeof(public_key_) || !payload || payload_len == 0)
    {
        return false;
    }

    ensureIdentityKeys();
    if (!keys_ready_)
    {
        return false;
    }

    uint8_t shared_secret[::chat::meshcore::kMeshCorePubKeySize] = {};
    if (!::chat::meshcore::meshcoreDeriveSharedSecret(private_key_, pubkey, shared_secret))
    {
        return false;
    }

    uint8_t key16[16] = {};
    uint8_t key32[32] = {};
    ::chat::meshcore::sharedSecretToKeys(shared_secret, key16, key32);

    uint8_t cipher[kMeshcoreMaxPayloadSize] = {};
    const size_t cipher_len = ::chat::meshcore::encryptThenMac(key16,
                                                               key32,
                                                               cipher,
                                                               sizeof(cipher),
                                                               payload,
                                                               payload_len);
    if (cipher_len == 0)
    {
        return false;
    }

    uint8_t datagram[kMeshcoreMaxPayloadSize] = {};
    size_t datagram_len = 0;
    datagram[datagram_len++] = pubkey[0];
    std::memcpy(datagram + datagram_len, public_key_, sizeof(public_key_));
    datagram_len += sizeof(public_key_);
    if (datagram_len + cipher_len > sizeof(datagram))
    {
        return false;
    }
    std::memcpy(datagram + datagram_len, cipher, cipher_len);
    datagram_len += cipher_len;

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    if (!::chat::meshcore::buildFrameNoTransport(kRouteTypeFlood,
                                                 kPayloadTypeDirectData,
                                                 nullptr,
                                                 0,
                                                 datagram,
                                                 datagram_len,
                                                 frame,
                                                 sizeof(frame),
                                                 &frame_len))
    {
        return false;
    }

    if (!transmitFrame(frame, frame_len))
    {
        return false;
    }

    if (out_est_timeout)
    {
        *out_est_timeout = estimateTimeoutMs(config_, frame_len, 0, true);
    }
    if (out_sent_flood)
    {
        *out_sent_flood = true;
    }
    return true;
}

bool MeshCoreRadioAdapter::sendTracePath(const uint8_t* path, size_t path_len,
                                         uint32_t tag, uint32_t auth, uint8_t flags,
                                         uint32_t* out_est_timeout)
{
    if (!path || path_len == 0 || path_len > 64)
    {
        return false;
    }

    uint8_t payload[9] = {};
    std::memcpy(payload, &tag, sizeof(tag));
    std::memcpy(payload + 4, &auth, sizeof(auth));
    payload[8] = flags;

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    if (!::chat::meshcore::buildFrameNoTransport(kRouteTypeDirect,
                                                 kPayloadTypeTrace,
                                                 path,
                                                 path_len,
                                                 payload,
                                                 sizeof(payload),
                                                 frame,
                                                 sizeof(frame),
                                                 &frame_len))
    {
        return false;
    }

    if (!transmitFrame(frame, frame_len))
    {
        return false;
    }
    if (out_est_timeout)
    {
        *out_est_timeout = estimateTimeoutMs(config_, frame_len, path_len, false);
    }
    return true;
}

bool MeshCoreRadioAdapter::sendControlData(const uint8_t* payload, size_t payload_len)
{
    if (!payload || payload_len == 0 || payload_len > kMeshcoreMaxPayloadSize)
    {
        return false;
    }

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    if (!::chat::meshcore::buildFrameNoTransport(kRouteTypeDirect,
                                                 kPayloadTypeControl,
                                                 nullptr,
                                                 0,
                                                 payload,
                                                 payload_len,
                                                 frame,
                                                 sizeof(frame),
                                                 &frame_len))
    {
        return false;
    }
    return transmitFrame(frame, frame_len);
}

bool MeshCoreRadioAdapter::sendRawData(const uint8_t* path, size_t path_len,
                                       const uint8_t* payload, size_t payload_len,
                                       uint32_t* out_est_timeout)
{
    if (!payload || payload_len == 0 || path_len > 64 || (path_len > 0 && !path))
    {
        return false;
    }

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    if (!::chat::meshcore::buildFrameNoTransport(kRouteTypeDirect,
                                                 kPayloadTypeRawCustom,
                                                 path,
                                                 path_len,
                                                 payload,
                                                 payload_len,
                                                 frame,
                                                 sizeof(frame),
                                                 &frame_len))
    {
        return false;
    }
    if (!transmitFrame(frame, frame_len))
    {
        return false;
    }
    if (out_est_timeout)
    {
        *out_est_timeout = estimateTimeoutMs(config_, frame_len, path_len, false);
    }
    return true;
}

void MeshCoreRadioAdapter::setFloodScopeKey(const uint8_t* key, size_t len)
{
    flood_scope_key_.fill(0);
    if (!key || len == 0)
    {
        return;
    }
    std::memcpy(flood_scope_key_.data(), key, std::min(len, flood_scope_key_.size()));
}

::chat::runtime::EffectiveSelfIdentity MeshCoreRadioAdapter::buildEffectiveIdentity() const
{
    ::chat::runtime::EffectiveSelfIdentity identity{};

    if (identity_provider_)
    {
        ::chat::runtime::SelfIdentityInput input{};
        if (identity_provider_->readSelfIdentityInput(&input))
        {
            if (!long_name_.empty())
            {
                input.configured_long_name = long_name_.c_str();
            }
            if (!short_name_.empty())
            {
                input.configured_short_name = short_name_.c_str();
            }
            (void)::chat::runtime::resolveEffectiveSelfIdentity(input, &identity);
            return identity;
        }
    }

    ::chat::runtime::SelfIdentityInput input{};
    input.node_id = node_id_;
    input.configured_long_name = long_name_.c_str();
    input.configured_short_name = short_name_.c_str();
    input.fallback_long_prefix = "node";
    input.fallback_ble_prefix = "node";
    input.allow_short_hex_fallback = true;
    (void)::chat::runtime::resolveEffectiveSelfIdentity(input, &identity);
    return identity;
}

void MeshCoreRadioAdapter::ensureIdentityKeys()
{
    if (keys_ready_)
    {
        return;
    }

    uint8_t seed[::chat::meshcore::kMeshCoreSeedSize] = {};
    const auto mac = device_identity::getSelfMacAddress();
    for (size_t i = 0; i < sizeof(seed); ++i)
    {
        seed[i] = static_cast<uint8_t>(mac[i % mac.size()] ^
                                       ((node_id_ >> ((i & 0x3U) * 8U)) & 0xFFU) ^
                                       i);
    }

    keys_ready_ = ::chat::meshcore::meshcoreCreateKeypair(seed, public_key_, private_key_);
}

bool MeshCoreRadioAdapter::transmitFrame(const uint8_t* data, size_t size)
{
    auto* io = ::platform::nrf52::arduino_common::chat::infra::radioPacketIo();
    return io && io->transmit(data, size);
}

bool MeshCoreRadioAdapter::sendAdvert(bool broadcast)
{
    ensureIdentityKeys();
    if (!keys_ready_)
    {
        return false;
    }

    ::chat::runtime::MeshCoreAnnouncementRequest request{};
    request.identity = buildEffectiveIdentity();
    request.mesh_config = config_;
    request.broadcast = broadcast;
    request.include_location = false;
    request.timestamp_s = millis() / 1000U;
    request.client_repeat = config_.meshcore_client_repeat;
    request.public_key = public_key_;
    request.public_key_len = sizeof(public_key_);
    request.private_key = private_key_;
    request.private_key_len = sizeof(private_key_);

    ::chat::runtime::MeshCoreAnnouncementPacket packet{};
    return ::chat::runtime::MeshCoreSelfAnnouncementCore::buildAdvertPacket(request, &packet) &&
           transmitFrame(packet.frame, packet.frame_size);
}

} // namespace platform::nrf52::arduino_common::chat::meshcore
