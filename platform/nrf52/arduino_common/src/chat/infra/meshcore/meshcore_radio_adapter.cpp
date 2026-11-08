#include "platform/nrf52/arduino_common/chat/infra/meshcore/meshcore_radio_adapter.h"

#include "chat/domain/contact_types.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"
#include "chat/runtime/meshcore_direct_secret_core.h"
#include "chat/runtime/meshcore_self_announcement_core.h"
#include "chat/runtime/self_identity_policy.h"
#include "chat/time_utils.h"
#include "chat/usecase/contact_service.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"
#include "platform/nrf52/arduino_common/device_identity.h"
#include "platform/nrf52/arduino_common/sys/event_bus.h"

#include <Arduino.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

namespace platform::nrf52::arduino_common::chat::meshcore
{
namespace
{
constexpr uint8_t kRouteTypeFlood = 0x01;
constexpr uint8_t kRouteTypeDirect = 0x02;
constexpr uint8_t kPayloadTypeReq = 0x00;
constexpr uint8_t kPayloadTypeAck = 0x03;
constexpr uint8_t kPayloadTypeDirectData = 0x07;
constexpr uint8_t kPayloadTypeGrpData = 0x06;
constexpr uint8_t kPayloadTypeTrace = ::chat::meshcore::kMeshCorePayloadTypeTrace;
constexpr uint8_t kPayloadTypeControl = ::chat::meshcore::kMeshCorePayloadTypeControl;
constexpr uint8_t kPayloadTypeRawCustom = 0x0F;
constexpr uint8_t kPayloadTypeAdvert = 0x04;
constexpr uint8_t kDirectAppMagic0 = 0xDA;
constexpr uint8_t kDirectAppMagic1 = 0x7A;
constexpr uint8_t kDirectAppFlagWantAck = 0x01;
constexpr uint8_t kGroupDataMagic0 = 0x47;
constexpr uint8_t kGroupDataMagic1 = 0x44;
constexpr size_t kMeshcoreMaxFrameSize = 255;
constexpr size_t kMeshcoreMaxPayloadSize = 220;
constexpr size_t kAdvertMinPayloadSize =
    ::chat::meshcore::kMeshCorePubKeySize + sizeof(uint32_t) +
    ::chat::meshcore::kMeshCoreSignatureSize;

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

MeshCoreRadioAdapter::MeshCoreRadioAdapter(const ::chat::runtime::SelfIdentityProvider* identity_provider,
                                           ::chat::contacts::ContactService* contact_service)
    : node_id_(device_identity::getSelfNodeId()),
      identity_provider_(identity_provider),
      contact_service_(contact_service)
{
}

::chat::MeshCapabilities MeshCoreRadioAdapter::getCapabilities() const
{
    ::chat::MeshCapabilities caps{};
    caps.supports_unicast_appdata = true;
    caps.supports_broadcast_appdata = true;
    caps.supports_appdata_ack = true;
    caps.supports_node_info = true;
    caps.supports_discovery_actions = true;
    caps.supports_node_info_query = true;
    caps.supports_node_info_reply = true;
    caps.supports_node_info_reannounce = true;
    caps.supports_trace_route_request = true;
    caps.supports_protocol_ack_tracking = true;
    caps.supports_meshcore_identity_keys = true;
    caps.supports_meshcore_peer_secret_derivation = true;
    return caps;
}

bool MeshCoreRadioAdapter::sendText(::chat::ChannelId channel, const std::string& text,
                                    ::chat::MessageId* out_msg_id, ::chat::NodeId peer)
{
    const ::chat::MeshSendResult result = sendTextDetailed(channel, text, 0, peer);
    if (out_msg_id)
    {
        *out_msg_id = result.msg_id;
    }
    return result.ok;
}

bool MeshCoreRadioAdapter::sendTextWithId(::chat::ChannelId channel, const std::string& text,
                                          ::chat::MessageId forced_msg_id,
                                          ::chat::MessageId* out_msg_id, ::chat::NodeId peer)
{
    const ::chat::MeshSendResult result = sendTextDetailed(channel, text, forced_msg_id, peer);
    if (out_msg_id)
    {
        *out_msg_id = result.msg_id;
    }
    return result.ok;
}

::chat::MeshSendResult MeshCoreRadioAdapter::sendTextDetailed(::chat::ChannelId channel,
                                                              const std::string& text,
                                                              ::chat::MessageId forced_msg_id,
                                                              ::chat::NodeId peer)
{
    const ::chat::MessageId msg_id = allocateMessageId(forced_msg_id);
    if (text.empty())
    {
        Serial.printf("[MESHCORE] TX text dropped reason=empty id=%08lX dest=%08lX\n",
                      static_cast<unsigned long>(msg_id),
                      static_cast<unsigned long>(peer));
        return ::chat::MeshSendResult::fail(::chat::MeshOperationFailure::InvalidInput, msg_id);
    }

    const bool want_ack = peer != 0;
    const bool ok = sendAppData(channel,
                                0x1001,
                                reinterpret_cast<const uint8_t*>(text.data()),
                                text.size(),
                                peer,
                                want_ack,
                                msg_id,
                                false);
    Serial.printf("[MESHCORE] TX text id=%08lX dest=%08lX len=%u ack=%u ok=%u\n",
                  static_cast<unsigned long>(msg_id),
                  static_cast<unsigned long>(peer),
                  static_cast<unsigned>(text.size()),
                  want_ack ? 1U : 0U,
                  ok ? 1U : 0U);
    if (!ok)
    {
        return ::chat::MeshSendResult::fail(::chat::MeshOperationFailure::RadioTxFailed, msg_id);
    }

    if (!want_ack)
    {
        Serial.printf("[MESHCORE] TX text id=%08lX no delivery ACK for broadcast\n",
                      static_cast<unsigned long>(msg_id));
    }
    return ::chat::MeshSendResult::success(msg_id);
}

bool MeshCoreRadioAdapter::pollIncomingText(::chat::MeshIncomingText* out)
{
    return text_queue_.pop(out);
}

bool MeshCoreRadioAdapter::enqueueIncomingText(const ::chat::MeshIncomingText& metadata,
                                               const char* text,
                                               size_t text_len)
{
    ::chat::infra::IncomingQueuePushReport report{};
    if (text_queue_.push(metadata,
                         text,
                         text_len,
                         ::chat::infra::IncomingQueuePriority::P1User,
                         &report))
    {
        if (report.dropped_existing)
        {
            Serial.printf("[MESHCORE] RX text queue pressure evicted_prio=%u depth=%u\n",
                          static_cast<unsigned>(report.dropped_priority),
                          static_cast<unsigned>(text_queue_.size()));
        }
        return true;
    }
    Serial.printf("[MESHCORE] RX text queue drop len=%u depth=%u\n",
                  static_cast<unsigned>(text_len),
                  static_cast<unsigned>(text_queue_.size()));
    return false;
}

bool MeshCoreRadioAdapter::sendAppData(::chat::ChannelId channel, uint32_t portnum,
                                       const uint8_t* payload, size_t len,
                                       ::chat::NodeId dest, bool want_ack,
                                       ::chat::MessageId packet_id,
                                       bool want_response)
{
    (void)channel;
    (void)want_response;
    if (!payload || len == 0 || !config_.tx_enabled)
    {
        Serial.printf("[MESHCORE] TX app-data dropped port=%lu dest=%08lX len=%u tx=%u\n",
                      static_cast<unsigned long>(portnum),
                      static_cast<unsigned long>(dest),
                      static_cast<unsigned>(len),
                      config_.tx_enabled ? 1U : 0U);
        return false;
    }

    uint8_t frame[255] = {};
    size_t frame_len = 0;
    const ::chat::meshcore::PayloadProfile profile = payloadProfile();

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

        if (!::chat::meshcore::buildFrameNoTransport(profile,
                                                     kRouteTypeFlood,
                                                     kPayloadTypeDirectData,
                                                     nullptr,
                                                     0,
                                                     plain,
                                                     plain_len,
                                                     frame,
                                                     sizeof(frame),
                                                     &frame_len))
        {
            Serial.printf("[MESHCORE] TX app-data encode failed port=%lu dest=%08lX ack=%u\n",
                          static_cast<unsigned long>(portnum),
                          static_cast<unsigned long>(dest),
                          want_ack ? 1U : 0U);
            return false;
        }
        const bool ok = transmitFrame(frame, frame_len);
        if (ok && want_ack)
        {
            ::chat::meshcore::ParsedPacket parsed{};
            if (::chat::meshcore::parsePacket(frame, frame_len, &parsed))
            {
                ::chat::runtime::MeshCoreAppAckRegistration ack{};
                ack.signature = ::chat::meshcore::packetSignature(parsed.payload_type,
                                                                  parsed.path_len,
                                                                  parsed.payload,
                                                                  parsed.payload_len);
                ack.peer = dest;
                ack.portnum = portnum;
                ack.message_id = packet_id;
                ::chat::runtime::RuntimeContext context = buildRuntimeContext();
                context.now_ms = millis();
                protocol_effect_workspace_.primary.clear();
                protocol_runtime_.trackAppAck(ack,
                                              context,
                                              protocol_effect_workspace_.primary);
                executeProtocolEffects(protocol_effect_workspace_.primary);
                rememberLocalTextAck(ack.signature);
                Serial.printf("[MESHCORE] ACK watch sig=%08lX msg=%08lX dest=%08lX port=%lu\n",
                              static_cast<unsigned long>(ack.signature),
                              static_cast<unsigned long>(ack.message_id),
                              static_cast<unsigned long>(ack.peer),
                              static_cast<unsigned long>(ack.portnum));
            }
        }
        return ok;
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

    if (!::chat::meshcore::buildFrameNoTransport(profile,
                                                 kRouteTypeFlood,
                                                 kPayloadTypeGrpData,
                                                 nullptr,
                                                 0,
                                                 plain,
                                                 plain_len,
                                                 frame,
                                                 sizeof(frame),
                                                 &frame_len))
    {
        Serial.printf("[MESHCORE] TX app-data encode failed port=%lu dest=%08lX ack=%u\n",
                      static_cast<unsigned long>(portnum),
                      static_cast<unsigned long>(dest),
                      want_ack ? 1U : 0U);
        return false;
    }
    return transmitFrame(frame, frame_len);
}

bool MeshCoreRadioAdapter::pollIncomingData(::chat::MeshIncomingData* out)
{
    return data_queue_.pop(out);
}

bool MeshCoreRadioAdapter::enqueueIncomingData(const ::chat::MeshIncomingData& metadata,
                                               const uint8_t* payload,
                                               size_t payload_len)
{
    ::chat::infra::IncomingQueuePushReport report{};
    if (data_queue_.push(metadata,
                         payload,
                         payload_len,
                         ::chat::infra::IncomingQueuePriority::P1User,
                         &report))
    {
        if (report.dropped_existing)
        {
            Serial.printf("[MESHCORE] RX data queue pressure evicted_prio=%u depth=%u\n",
                          static_cast<unsigned>(report.dropped_priority),
                          static_cast<unsigned>(data_queue_.size()));
        }
        return true;
    }
    Serial.printf("[MESHCORE] RX data queue drop port=%lu len=%u depth=%u\n",
                  static_cast<unsigned long>(metadata.portnum),
                  static_cast<unsigned>(payload_len),
                  static_cast<unsigned>(data_queue_.size()));
    return false;
}

bool MeshCoreRadioAdapter::requestNodeInfo(::chat::NodeId dest, bool want_response)
{
    if (!config_.tx_enabled)
    {
        return false;
    }

    ::chat::runtime::FixedProtocolRuntimeContextProvider context_provider(buildRuntimeContext());
    const auto bundle = protocolRuntimeBundle(context_provider);
    if (!bundle.valid())
    {
        return false;
    }
    auto facade = bundle.createFacade(
        protocol_effect_workspace_,
        ::chat::runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
    return facade.requestNodeInfo(dest, want_response).ok();
}

::chat::runtime::RuntimeContext MeshCoreRadioAdapter::buildRuntimeContext() const
{
    ::chat::runtime::RuntimeContext context{};
    context.protocol = ::chat::MeshProtocol::MeshCore;
    context.self_node = node_id_;
    context.now_ms = millis();
    context.meshcore_discover_node_type = ::chat::meshcore::kMeshCoreAdvertTypeChat;
    context.meshcore_local_modified_epoch = ::chat::now_epoch_seconds();
    return context;
}

::chat::meshcore::PayloadProfile MeshCoreRadioAdapter::payloadProfile() const
{
    return config_.meshcore_send_profile == ::chat::MeshCorePayloadSendProfile::V1Only
               ? ::chat::meshcore::PayloadProfile::V1
               : ::chat::meshcore::PayloadProfile::V2;
}

::chat::runtime::ProtocolRuntimeBundle MeshCoreRadioAdapter::protocolRuntimeBundle(
    const ::chat::runtime::IProtocolRuntimeContextProvider& context_provider)
{
    ::chat::runtime::ProtocolRuntimeSelection selection{};
    selection.meshcore = &protocol_runtime_;
    return ::chat::runtime::protocolRuntimeFor(::chat::MeshProtocol::MeshCore,
                                               selection,
                                               *this,
                                               context_provider);
}

bool MeshCoreRadioAdapter::execute(const ::chat::runtime::ProtocolEffect& effect)
{
    return executeProtocolEffect(effect);
}

bool MeshCoreRadioAdapter::executeProtocolEffects(const ::chat::runtime::ProtocolEffects& effects)
{
    bool ok = true;
    for (const auto& effect : effects.items)
    {
        ok = execute(effect) && ok;
    }
    return ok;
}

bool MeshCoreRadioAdapter::executeProtocolEffect(const ::chat::runtime::ProtocolEffect& effect)
{
    bool ok = false;
    ::chat::runtime::visitProtocolEffect(
        effect,
        [this, &ok](const auto& item)
        {
            using Effect = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Effect, ::chat::runtime::SendNodeInfoEffect>)
            {
                ok = executeNodeInfoEffect(item);
            }
            else if constexpr (std::is_same_v<Effect, ::chat::runtime::SendTraceRouteEffect>)
            {
                if (item.protocol == ::chat::MeshProtocol::MeshCore && config_.tx_enabled)
                {
                    const uint8_t peer_hash = static_cast<uint8_t>(item.peer & 0xFFU);
                    uint8_t path[64] = {};
                    size_t path_len = 0;
                    if (peer_hash != 0x00 && peer_hash != 0xFF &&
                        peer_hash != static_cast<uint8_t>(node_id_ & 0xFFU))
                    {
                        path[path_len++] = peer_hash;
                    }

                    uint32_t timeout_ms = item.timeout_ms;
                    ok = path_len > 0 &&
                         sendTracePath(path,
                                       path_len,
                                       item.request_id,
                                       item.auth,
                                       item.flags,
                                       &timeout_ms);

                    ::chat::runtime::TxResult result{};
                    result.protocol = ::chat::MeshProtocol::MeshCore;
                    result.request_id = item.request_id;
                    result.peer = item.peer;
                    result.ok = ok;
                    result.detail = ok ? static_cast<int32_t>(timeout_ms) : 0;
                    ::chat::runtime::FixedProtocolRuntimeContextProvider context_provider(
                        buildRuntimeContext());
                    const auto bundle = protocolRuntimeBundle(context_provider);
                    if (bundle.valid())
                    {
                        auto facade = bundle.createFacade(
                            protocol_effect_workspace_,
                            ::chat::runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
                        facade.handleTxResult(result);
                    }
                }
            }
            else if constexpr (std::is_same_v<Effect, ::chat::runtime::SendDiscoverRequestEffect>)
            {
                ok = executeDiscoverRequestEffect(item);
            }
            else if constexpr (std::is_same_v<Effect, ::chat::runtime::SendDiscoverResponseEffect>)
            {
                ok = executeDiscoverResponseEffect(item);
            }
            else if constexpr (std::is_same_v<Effect, ::chat::runtime::SendSelfAnnouncementEffect>)
            {
                ok = executeSelfAnnouncementEffect(item);
            }
            else if constexpr (std::is_same_v<Effect, ::chat::runtime::PublishNodeInfoEffect>)
            {
                if (item.protocol == ::chat::MeshProtocol::MeshCore &&
                    item.node_id != 0 &&
                    contact_service_)
                {
                    float snr = 0.0f;
                    if (item.rx_meta.snr_db_x10 != std::numeric_limits<int16_t>::min())
                    {
                        snr = static_cast<float>(item.rx_meta.snr_db_x10) / 10.0f;
                    }

                    float rssi = 0.0f;
                    if (item.rx_meta.rssi_dbm_x10 != std::numeric_limits<int16_t>::min())
                    {
                        rssi = static_cast<float>(item.rx_meta.rssi_dbm_x10) / 10.0f;
                    }

                    // A remote identity timestamp must not make a freshly
                    // received peer expire from the local Nearby view.
                    const uint32_t timestamp = ::chat::now_epoch_seconds();
                    contact_service_->updateNodeInfo(
                        item.node_id,
                        item.short_name.c_str(),
                        item.long_name.c_str(),
                        snr,
                        rssi,
                        timestamp,
                        static_cast<uint8_t>(::chat::contacts::NodeProtocolType::MeshCore),
                        item.role,
                        item.hops,
                        0,
                        static_cast<uint8_t>(item.channel));

                    if (item.has_public_key || item.key_manually_verified)
                    {
                        ::chat::contacts::NodeUpdate update{};
                        update.has_public_key = item.has_public_key;
                        update.public_key_present = item.has_public_key;
                        update.has_key_manually_verified = item.key_manually_verified;
                        update.key_manually_verified = item.key_manually_verified;
                        contact_service_->applyNodeUpdate(item.node_id, update);
                    }

                    ok = true;
                }
            }
            else if constexpr (std::is_same_v<Effect, ::chat::runtime::UpdatePeerRouteEffect>)
            {
                ok = item.protocol == ::chat::MeshProtocol::MeshCore;
                if (ok && contact_service_ &&
                    item.public_key.size() ==
                        ::chat::kMeshPeerMeshCorePublicKeyLen)
                {
                    peer_record_scratch_ = {};
                    peer_record_scratch_.valid = true;
                    if (::chat::makeMeshPeerPublicKeyIdentity(
                            ::chat::MeshProtocol::MeshCore,
                            item.public_key.data(),
                            item.public_key.size(),
                            peer_record_scratch_.identity))
                    {
                        peer_record_scratch_.source =
                            ::chat::MeshPeerSource::RuntimeRx;
                        peer_record_scratch_.last_seen_s =
                            ::chat::now_epoch_seconds();
                        peer_record_scratch_.meshcore.has_public_key = true;
                        std::memcpy(
                            peer_record_scratch_.meshcore.public_key,
                            item.public_key.data(),
                            ::chat::kMeshPeerMeshCorePublicKeyLen);
                        peer_record_scratch_.meshcore.public_key_verified =
                            item.public_key_verified;
                        peer_record_scratch_.meshcore.node_id_hint = item.peer;
                        peer_record_scratch_.meshcore.has_peer_hash = true;
                        peer_record_scratch_.meshcore.peer_hash = item.peer_hash;
                        peer_record_scratch_.meshcore.has_next_hop = true;
                        peer_record_scratch_.meshcore.next_hop = item.next_hop;
                        ok = contact_service_->recordPeer(
                            peer_record_scratch_);
                    }
                }
            }
            else if constexpr (std::is_same_v<Effect, ::chat::runtime::EmitActionResultEffect>)
            {
                if (item.protocol == ::chat::MeshProtocol::MeshCore)
                {
                    const bool is_text_ack =
                        item.action == ::chat::runtime::ProtocolActionKind::SendText &&
                        item.message_id != 0;
                    if (is_text_ack &&
                        item.state == ::chat::runtime::ProtocolActionState::Completed)
                    {
                        Serial.printf("[MESHCORE] ACK complete sig=%08lX msg=%08lX dest=%08lX trip=%ld\n",
                                      static_cast<unsigned long>(item.request_id),
                                      static_cast<unsigned long>(item.message_id),
                                      static_cast<unsigned long>(item.peer),
                                      static_cast<long>(item.detail));
                        forgetLocalTextAck(item.request_id);
                        sys::EventBus::publish(
                            new sys::ChatSendResultEvent(
                                item.message_id,
                                ::chat::MessageStatus::Delivered,
                                ::chat::MeshProtocol::MeshCore),
                            0);
                    }
                    else if (is_text_ack &&
                             (item.state == ::chat::runtime::ProtocolActionState::Failed ||
                              item.state == ::chat::runtime::ProtocolActionState::TimedOut))
                    {
                        Serial.printf("[MESHCORE] ACK failed sig=%08lX msg=%08lX dest=%08lX state=%u age=%ld\n",
                                      static_cast<unsigned long>(item.request_id),
                                      static_cast<unsigned long>(item.message_id),
                                      static_cast<unsigned long>(item.peer),
                                      static_cast<unsigned>(item.state),
                                      static_cast<long>(item.detail));
                        forgetLocalTextAck(item.request_id);
                        sys::EventBus::publish(
                            new sys::ChatSendResultEvent(
                                item.message_id,
                                ::chat::MessageStatus::Failed,
                                ::chat::MeshProtocol::MeshCore,
                                ::chat::delivery::SendFailureKind::AckTimeout),
                            0);
                    }
                    ok = item.state != ::chat::runtime::ProtocolActionState::Failed &&
                         item.state != ::chat::runtime::ProtocolActionState::TimedOut;
                }
            }
        });
    return ok;
}

bool MeshCoreRadioAdapter::executeNodeInfoEffect(const ::chat::runtime::SendNodeInfoEffect& effect)
{
    if (effect.protocol != ::chat::MeshProtocol::MeshCore || !config_.tx_enabled)
    {
        return false;
    }

    const ::chat::NodeId target = effect.peer == 0xFFFFFFFFUL ? 0 : effect.peer;
    uint8_t payload[::chat::meshcore::kMeshCoreNodeInfoInfoPayloadSize] = {};
    size_t payload_len = 0;
    if (effect.want_response)
    {
        if (!::chat::meshcore::buildNodeInfoQueryControlPayload(true,
                                                                payload,
                                                                sizeof(payload),
                                                                &payload_len))
        {
            return false;
        }
    }
    else
    {
        char short_name[::chat::meshcore::kMeshCoreNodeInfoShortNameFieldSize] = {};
        if (!short_name_.empty())
        {
            std::strncpy(short_name, short_name_.c_str(), sizeof(short_name) - 1);
        }
        else
        {
            std::snprintf(short_name, sizeof(short_name), "%04lX",
                          static_cast<unsigned long>(node_id_ & 0xFFFFUL));
        }

        char long_name[::chat::meshcore::kMeshCoreNodeInfoLongNameFieldSize] = {};
        if (!long_name_.empty())
        {
            std::strncpy(long_name, long_name_.c_str(), sizeof(long_name) - 1);
        }
        else
        {
            std::strncpy(long_name, short_name, sizeof(long_name) - 1);
        }

        ::chat::meshcore::MeshCoreNodeInfoBuildInfo info{};
        info.role = static_cast<uint8_t>(::chat::contacts::NodeRoleType::Client);
        info.hops = 0;
        info.node_id = node_id_;
        info.timestamp = ::chat::now_message_timestamp();
        info.short_name = short_name;
        info.long_name = long_name;
        if (!::chat::meshcore::buildNodeInfoInfoControlPayload(info,
                                                               payload,
                                                               sizeof(payload),
                                                               &payload_len))
        {
            return false;
        }
    }

    return sendAppData(::chat::ChannelId::PRIMARY,
                       ::chat::meshcore::kMeshCoreNodeInfoPortnum,
                       payload,
                       payload_len,
                       target,
                       false);
}

bool MeshCoreRadioAdapter::executeDiscoverRequestEffect(const ::chat::runtime::SendDiscoverRequestEffect& effect)
{
    if (effect.protocol != ::chat::MeshProtocol::MeshCore || !config_.tx_enabled)
    {
        Serial.printf("[MESHCORE] TX DISCOVER_REQ blocked mode=local reason=%s\n",
                      effect.protocol != ::chat::MeshProtocol::MeshCore ? "protocol" : "tx_disabled");
        return false;
    }

    ::chat::meshcore::MeshCoreDiscoverRequestBuildInfo request{};
    request.prefix_only = effect.prefix_only;
    request.type_filter = effect.type_filter;
    request.tag = effect.tag == 0 ? static_cast<uint32_t>(millis()) : effect.tag;
    request.since = effect.since;

    uint8_t payload[10] = {};
    size_t payload_len = 0;
    if (!::chat::meshcore::buildDiscoverRequestControlPayload(request,
                                                              payload,
                                                              sizeof(payload),
                                                              &payload_len))
    {
        Serial.printf("[MESHCORE] TX DISCOVER_REQ mode=local tag=%08lX filter=%02X prefix=%u ok=0 reason=encode\n",
                      static_cast<unsigned long>(request.tag),
                      static_cast<unsigned>(request.type_filter),
                      request.prefix_only ? 1U : 0U);
        return false;
    }

    const bool ok = sendControlData(payload, payload_len);
    Serial.printf("[MESHCORE] TX DISCOVER_REQ mode=local tag=%08lX filter=%02X prefix=%u since=%lu len=%u ok=%u\n",
                  static_cast<unsigned long>(request.tag),
                  static_cast<unsigned>(request.type_filter),
                  request.prefix_only ? 1U : 0U,
                  static_cast<unsigned long>(request.since),
                  static_cast<unsigned>(payload_len),
                  ok ? 1U : 0U);
    return ok;
}

bool MeshCoreRadioAdapter::executeDiscoverResponseEffect(const ::chat::runtime::SendDiscoverResponseEffect& effect)
{
    if (effect.protocol != ::chat::MeshProtocol::MeshCore)
    {
        return false;
    }
    if (!config_.tx_enabled)
    {
        return true;
    }

    ensureIdentityKeys();
    if (!keys_ready_)
    {
        return true;
    }

    const size_t key_len = effect.prefix_only
                               ? ::chat::meshcore::kMeshCorePubKeyPrefixSize
                               : ::chat::meshcore::kMeshCorePubKeySize;
    uint8_t payload[::chat::meshcore::kMeshCoreDiscoverResponseBasePayloadSize +
                    ::chat::meshcore::kMeshCorePubKeySize] = {};
    size_t payload_len = 0;
    return ::chat::meshcore::buildDiscoverResponseControlPayload(
               ::chat::meshcore::kMeshCoreAdvertTypeChat,
               0,
               effect.tag,
               public_key_,
               key_len,
               payload,
               sizeof(payload),
               &payload_len) &&
           sendControlData(payload, payload_len);
}

bool MeshCoreRadioAdapter::executeSelfAnnouncementEffect(const ::chat::runtime::SendSelfAnnouncementEffect& effect)
{
    if (effect.protocol != ::chat::MeshProtocol::MeshCore || !config_.tx_enabled)
    {
        Serial.printf("[MESHCORE] TX ADVERT blocked mode=%s reason=%s\n",
                      effect.broadcast ? "broadcast" : "local",
                      effect.protocol != ::chat::MeshProtocol::MeshCore ? "protocol" : "tx_disabled");
        return false;
    }
    (void)effect.include_location;
    (void)effect.lat_i6;
    (void)effect.lon_i6;
    const bool ok = sendAdvert(effect.broadcast);
    Serial.printf("[MESHCORE] TX ADVERT mode=%s ok=%u\n",
                  effect.broadcast ? "broadcast" : "local",
                  ok ? 1U : 0U);
    return ok;
}

bool MeshCoreRadioAdapter::triggerDiscoveryAction(::chat::MeshDiscoveryAction action)
{
    return triggerDiscoveryActionDetailed(action).ok;
}

::chat::MeshActionResult MeshCoreRadioAdapter::triggerDiscoveryActionDetailed(::chat::MeshDiscoveryAction action)
{
    ::chat::runtime::DiscoverIntent intent{};
    intent.action = action;
    intent.type_filter = ::chat::meshcore::kMeshCoreDiscoverTypeFilterAll;

    if (!config_.tx_enabled)
    {
        return ::chat::MeshActionResult::fail(::chat::MeshOperationFailure::TxDisabled);
    }
    if (!isReady())
    {
        return ::chat::MeshActionResult::fail(::chat::MeshOperationFailure::RadioOffline);
    }

    ::chat::runtime::FixedProtocolRuntimeContextProvider context_provider(buildRuntimeContext());
    const auto bundle = protocolRuntimeBundle(context_provider);
    if (!bundle.valid())
    {
        return ::chat::MeshActionResult::fail(::chat::MeshOperationFailure::NotReady);
    }
    auto facade = bundle.createFacade(
        protocol_effect_workspace_,
        ::chat::runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
    const auto result = facade.discover(intent);
    if (result.ok())
    {
        return ::chat::MeshActionResult::success();
    }
    if (result.effect_count == 0)
    {
        return ::chat::MeshActionResult::fail(::chat::MeshOperationFailure::Unsupported);
    }
    return ::chat::MeshActionResult::fail(::chat::MeshOperationFailure::RadioTxFailed,
                                          static_cast<int>(result.failed_effect_count));
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
        Serial.printf("[MESHCORE] RX parse failed len=%u\n",
                      static_cast<unsigned>(size));
        return;
    }

    Serial.printf("[MESHCORE] RX frame route=%u type=%u path=%u payload=%u len=%u\n",
                  static_cast<unsigned>(parsed.route_type),
                  static_cast<unsigned>(parsed.payload_type),
                  static_cast<unsigned>(parsed.path_len),
                  static_cast<unsigned>(parsed.payload_len),
                  static_cast<unsigned>(size));

    if (parsed.payload_type == kPayloadTypeAck &&
        parsed.payload_len >= sizeof(uint32_t))
    {
        uint32_t ack_sig = 0;
        std::memcpy(&ack_sig, parsed.payload, sizeof(ack_sig));
        Serial.printf("[MESHCORE] RX ACK sig=%08lX\n",
                      static_cast<unsigned long>(ack_sig));
        protocol_effect_workspace_.primary.clear();
        protocol_runtime_.handleAppAck(ack_sig,
                                       buildRuntimeContext(),
                                       protocol_effect_workspace_.primary);
        executeProtocolEffects(protocol_effect_workspace_.primary);
        return;
    }

    if (parsed.payload_type == kPayloadTypeTrace &&
        parsed.payload_len >= ::chat::meshcore::kMeshCoreTraceBasePayloadSize)
    {
        ::chat::runtime::IncomingPacket packet{};
        packet.protocol = ::chat::MeshProtocol::MeshCore;
        packet.payload_type = kPayloadTypeTrace;
        if (!packet.payload.assign(parsed.payload, parsed.payload_len))
        {
            Serial.printf("[MESHCORE] RX runtime drop type=%u payload=%u cap=%u\n",
                          static_cast<unsigned>(parsed.payload_type),
                          static_cast<unsigned>(parsed.payload_len),
                          static_cast<unsigned>(packet.payload.capacity()));
            return;
        }
        if (parsed.path_len > 0)
        {
            if (!packet.path.assign(parsed.path, parsed.path_len))
            {
                Serial.printf("[MESHCORE] RX runtime drop type=%u path=%u cap=%u\n",
                              static_cast<unsigned>(parsed.payload_type),
                              static_cast<unsigned>(parsed.path_len),
                              static_cast<unsigned>(packet.path.capacity()));
                return;
            }
        }
        ::chat::runtime::FixedProtocolRuntimeContextProvider context_provider(buildRuntimeContext());
        const auto bundle = protocolRuntimeBundle(context_provider);
        if (bundle.valid())
        {
            auto facade = bundle.createFacade(
                protocol_effect_workspace_,
                ::chat::runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
            facade.handleIncoming(packet);
        }
        return;
    }

    if (parsed.payload_type == kPayloadTypeControl &&
        parsed.payload_len > 0 &&
        (parsed.payload[0] & 0x80U) != 0)
    {
        ::chat::runtime::IncomingPacket packet{};
        packet.protocol = ::chat::MeshProtocol::MeshCore;
        packet.payload_type = kPayloadTypeControl;
        if (!packet.payload.assign(parsed.payload, parsed.payload_len))
        {
            Serial.printf("[MESHCORE] RX runtime drop type=%u payload=%u cap=%u\n",
                          static_cast<unsigned>(parsed.payload_type),
                          static_cast<unsigned>(parsed.payload_len),
                          static_cast<unsigned>(packet.payload.capacity()));
            return;
        }
        if (parsed.path_len > 0)
        {
            if (!packet.path.assign(parsed.path, parsed.path_len))
            {
                Serial.printf("[MESHCORE] RX runtime drop type=%u path=%u cap=%u\n",
                              static_cast<unsigned>(parsed.payload_type),
                              static_cast<unsigned>(parsed.path_len),
                              static_cast<unsigned>(packet.path.capacity()));
                return;
            }
        }
        ::chat::runtime::FixedProtocolRuntimeContextProvider context_provider(buildRuntimeContext());
        const auto bundle = protocolRuntimeBundle(context_provider);
        if (bundle.valid())
        {
            auto facade = bundle.createFacade(
                protocol_effect_workspace_,
                ::chat::runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
            facade.handleIncoming(packet);
        }
        return;
    }

    if (parsed.payload_type == kPayloadTypeAdvert)
    {
        if (parsed.payload_len < kAdvertMinPayloadSize)
        {
            return;
        }

        const uint8_t* pubkey = parsed.payload;
        const uint8_t* timestamp = pubkey + ::chat::meshcore::kMeshCorePubKeySize;
        const uint8_t* signature = timestamp + sizeof(uint32_t);
        const uint8_t* app_data = parsed.payload + kAdvertMinPayloadSize;
        const size_t app_data_len = parsed.payload_len - kAdvertMinPayloadSize;

        std::array<uint8_t, ::chat::meshcore::kMeshCorePubKeySize + sizeof(uint32_t) +
                                kMeshcoreMaxPayloadSize>
            signed_message{};
        size_t signed_len = 0;
        std::memcpy(signed_message.data() + signed_len,
                    pubkey,
                    ::chat::meshcore::kMeshCorePubKeySize);
        signed_len += ::chat::meshcore::kMeshCorePubKeySize;
        std::memcpy(signed_message.data() + signed_len, timestamp, sizeof(uint32_t));
        signed_len += sizeof(uint32_t);
        if (app_data_len > 0)
        {
            std::memcpy(signed_message.data() + signed_len, app_data, app_data_len);
            signed_len += app_data_len;
        }
        if (!::chat::meshcore::meshcoreVerify(pubkey,
                                              signature,
                                              signed_message.data(),
                                              signed_len))
        {
            return;
        }

        ::chat::meshcore::DecodedAdvertAppData advert{};
        if (::chat::meshcore::decodeAdvertAppData(app_data, app_data_len, &advert) &&
            advert.valid)
        {
            const ::chat::NodeId advert_node =
                ::chat::meshcore::deriveNodeIdFromPubkey(pubkey,
                                                         ::chat::meshcore::kMeshCorePubKeySize);
            if (advert_node != 0 && advert_node != node_id_ && contact_service_)
            {
                peer_record_scratch_ = {};
                peer_record_scratch_.valid = true;
                if (::chat::makeMeshPeerPublicKeyIdentity(
                        ::chat::MeshProtocol::MeshCore,
                        pubkey,
                        ::chat::meshcore::kMeshCorePubKeySize,
                        peer_record_scratch_.identity))
                {
                    peer_record_scratch_.source =
                        ::chat::MeshPeerSource::RuntimeRx;
                    peer_record_scratch_.last_seen_s =
                        ::chat::now_epoch_seconds();
                    peer_record_scratch_.meshcore.has_public_key = true;
                    std::memcpy(peer_record_scratch_.meshcore.public_key,
                                pubkey,
                                ::chat::meshcore::kMeshCorePubKeySize);
                    peer_record_scratch_.meshcore.node_id_hint = advert_node;
                    (void)contact_service_->recordPeer(peer_record_scratch_);
                }
                ::chat::contacts::NodeUpdate update{};
                if (advert.has_name)
                {
                    update.short_name = advert.name;
                    update.long_name = advert.name;
                }
                update.has_last_seen = true;
                update.last_seen = ::chat::now_epoch_seconds();
                update.has_hops_away = parsed.path_len <= 255U;
                update.hops_away = static_cast<uint8_t>(parsed.path_len);
                update.has_channel = true;
                update.channel = static_cast<uint8_t>(::chat::ChannelId::PRIMARY);
                update.has_protocol = true;
                update.protocol =
                    static_cast<uint8_t>(::chat::contacts::NodeProtocolType::MeshCore);
                update.has_role = true;
                update.role = ::chat::meshcore::mapAdvertTypeToRole(advert.node_type);
                update.has_public_key = true;
                update.public_key_present = true;
                if (advert.has_location)
                {
                    update.has_position = true;
                    update.position.valid = true;
                    update.position.latitude_i = advert.latitude_i6 * 10;
                    update.position.longitude_i = advert.longitude_i6 * 10;
                    update.position.timestamp = update.last_seen;
                }
                contact_service_->applyNodeUpdate(advert_node, update);
                Serial.printf("[MESHCORE] RX advert node=%08lX name='%s' type=%u\n",
                              static_cast<unsigned long>(advert_node),
                              advert.has_name ? advert.name : "",
                              static_cast<unsigned>(advert.node_type));
            }
        }
        return;
    }

    ::chat::meshcore::DecodedDirectAppPayload direct_payload{};
    if (::chat::meshcore::decodeDirectAppPayload(parsed.payload, parsed.payload_len, &direct_payload) &&
        direct_payload.payload && direct_payload.payload_len > 0)
    {
        const uint32_t packet_sig = ::chat::meshcore::packetSignature(parsed.payload_type,
                                                                      parsed.path_len,
                                                                      parsed.payload,
                                                                      parsed.payload_len);
        if (direct_payload.want_ack && packet_sig != 0)
        {
            if (isLocalTextAck(packet_sig))
            {
                Serial.printf("[MESHCORE] RX direct self echo skip ACK sig=%08lX\n",
                              static_cast<unsigned long>(packet_sig));
            }
            else
            {
                const bool ack_ok = sendAppAck(packet_sig);
                Serial.printf("[MESHCORE] TX ACK sig=%08lX ok=%u\n",
                              static_cast<unsigned long>(packet_sig),
                              ack_ok ? 1U : 0U);
            }
        }

        ::chat::MeshIncomingData incoming{};
        incoming.from = node_id_;
        incoming.to = 0;
        incoming.portnum = direct_payload.portnum;
        if (incoming.portnum == ::chat::meshcore::kMeshCoreNodeInfoPortnum &&
            handleNodeInfoAppData(incoming, direct_payload.payload, direct_payload.payload_len))
        {
            return;
        }
        enqueueIncomingData(incoming, direct_payload.payload, direct_payload.payload_len);

        if (direct_payload.portnum == 0x1001 &&
            isPrintableTextPayload(direct_payload.payload, direct_payload.payload_len))
        {
            ::chat::MeshIncomingText text{};
            text.from = incoming.from;
            text.to = incoming.to;
            text.channel = ::chat::ChannelId::PRIMARY;
            enqueueIncomingText(text,
                                reinterpret_cast<const char*>(direct_payload.payload),
                                direct_payload.payload_len);
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
        if (incoming.portnum == ::chat::meshcore::kMeshCoreNodeInfoPortnum &&
            handleNodeInfoAppData(incoming, group_payload.payload, group_payload.payload_len))
        {
            return;
        }
        enqueueIncomingData(incoming, group_payload.payload, group_payload.payload_len);

        if (group_payload.portnum == 0x1001 &&
            isPrintableTextPayload(group_payload.payload, group_payload.payload_len))
        {
            ::chat::MeshIncomingText text{};
            text.from = incoming.from;
            text.to = incoming.to;
            text.channel = ::chat::ChannelId::PRIMARY;
            enqueueIncomingText(text,
                                reinterpret_cast<const char*>(group_payload.payload),
                                group_payload.payload_len);
        }
    }
}

void MeshCoreRadioAdapter::setLastRxStats(float rssi, float snr)
{
    (void)rssi;
    (void)snr;
}

bool MeshCoreRadioAdapter::handleNodeInfoAppData(const ::chat::MeshIncomingData& incoming,
                                                 const uint8_t* payload,
                                                 size_t payload_len)
{
    ::chat::meshcore::DecodedNodeInfoControl decoded{};
    if (!::chat::meshcore::decodeNodeInfoControlPayload(payload,
                                                        payload_len,
                                                        &decoded))
    {
        return false;
    }

    ::chat::runtime::IncomingPacket packet{};
    packet.protocol = ::chat::MeshProtocol::MeshCore;
    packet.channel = incoming.channel;
    packet.from = incoming.from;
    packet.to = incoming.to;
    packet.packet_id = incoming.packet_id;
    packet.request_id = incoming.request_id;
    packet.portnum = incoming.portnum;
    packet.want_response = incoming.want_response;
    if (!packet.payload.assign(payload, payload_len))
    {
        Serial.printf("[MESHCORE] RX runtime drop nodeinfo payload=%u cap=%u\n",
                      static_cast<unsigned>(payload_len),
                      static_cast<unsigned>(packet.payload.capacity()));
        return false;
    }
    packet.rx_meta = incoming.rx_meta;

    ::chat::runtime::FixedProtocolRuntimeContextProvider context_provider(buildRuntimeContext());
    const auto bundle = protocolRuntimeBundle(context_provider);
    if (!bundle.valid())
    {
        return false;
    }
    auto facade = bundle.createFacade(
        protocol_effect_workspace_,
        ::chat::runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
    const auto result = facade.handleIncoming(packet);
    if (result.effect_count == 0)
    {
        return decoded.type == ::chat::meshcore::MeshCoreNodeInfoControlType::Query;
    }

    if (decoded.type == ::chat::meshcore::MeshCoreNodeInfoControlType::Query)
    {
        return true;
    }
    return result.ok();
}

void MeshCoreRadioAdapter::processSendQueue()
{
    ::chat::runtime::FixedProtocolRuntimeContextProvider context_provider(buildRuntimeContext());
    const auto bundle = protocolRuntimeBundle(context_provider);
    if (!bundle.valid())
    {
        return;
    }
    auto facade = bundle.createFacade(
        protocol_effect_workspace_,
        ::chat::runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
    facade.tick();
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

    uint8_t key16[16] = {};
    uint8_t key32[32] = {};
    if (!::chat::runtime::MeshCoreDirectSecretCore::derivePeerKeys(private_key_, sizeof(private_key_),
                                                                   pubkey, len,
                                                                   key16, sizeof(key16),
                                                                   key32, sizeof(key32)))
    {
        return false;
    }

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
    const ::chat::meshcore::PayloadProfile profile = payloadProfile();
    uint8_t peer_hash[::chat::meshcore::kMeshCoreV2HashBytes] = {};
    uint8_t self_hash[::chat::meshcore::kMeshCoreV2HashBytes] = {};
    if (!::chat::meshcore::copyPublicHash(profile, pubkey, len,
                                          peer_hash, sizeof(peer_hash)) ||
        !::chat::meshcore::copyPublicHash(profile, public_key_, sizeof(public_key_),
                                          self_hash, sizeof(self_hash)) ||
        !::chat::meshcore::buildPeerDatagramPayload(profile,
                                                    peer_hash,
                                                    self_hash,
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
    if (!::chat::meshcore::buildFrameNoTransport(profile,
                                                 route_type,
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

    uint8_t key16[16] = {};
    uint8_t key32[32] = {};
    if (!::chat::runtime::MeshCoreDirectSecretCore::derivePeerKeys(private_key_, sizeof(private_key_),
                                                                   pubkey, len,
                                                                   key16, sizeof(key16),
                                                                   key32, sizeof(key32)))
    {
        return false;
    }

    const ::chat::meshcore::PayloadProfile profile = payloadProfile();
    uint8_t cipher[kMeshcoreMaxPayloadSize] = {};
    const size_t cipher_len = ::chat::meshcore::encryptThenMac(key16,
                                                               key32,
                                                               cipher,
                                                               sizeof(cipher),
                                                               payload,
                                                               payload_len,
                                                               ::chat::meshcore::payloadMacBytes(profile));
    if (cipher_len == 0)
    {
        return false;
    }

    uint8_t datagram[kMeshcoreMaxPayloadSize] = {};
    size_t datagram_len = 0;
    uint8_t peer_hash[::chat::meshcore::kMeshCoreV2HashBytes] = {};
    if (!::chat::meshcore::copyPublicHash(profile, pubkey, len,
                                          peer_hash, sizeof(peer_hash)))
    {
        return false;
    }
    const size_t hash_bytes = ::chat::meshcore::payloadHashBytes(profile);
    if (hash_bytes + sizeof(public_key_) + cipher_len > sizeof(datagram))
    {
        return false;
    }
    std::memcpy(datagram + datagram_len, peer_hash, hash_bytes);
    datagram_len += hash_bytes;
    std::memcpy(datagram + datagram_len, public_key_, sizeof(public_key_));
    datagram_len += sizeof(public_key_);
    std::memcpy(datagram + datagram_len, cipher, cipher_len);
    datagram_len += cipher_len;

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    if (!::chat::meshcore::buildFrameNoTransport(profile,
                                                 kRouteTypeFlood,
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
    if (!path || path_len == 0 ||
        !::chat::meshcore::isValidTracePathHashBytes(flags, path_len, 64))
    {
        return false;
    }

    uint8_t payload[kMeshcoreMaxPayloadSize] = {};
    size_t payload_len = 0;
    if (!::chat::meshcore::buildTracePayload(tag,
                                             auth,
                                             flags,
                                             path,
                                             path_len,
                                             payload,
                                             sizeof(payload),
                                             &payload_len))
    {
        return false;
    }

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    const ::chat::meshcore::PayloadProfile profile = payloadProfile();
    if (!::chat::meshcore::buildFrameNoTransport(profile,
                                                 kRouteTypeDirect,
                                                 kPayloadTypeTrace,
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
    const ::chat::meshcore::PayloadProfile profile = payloadProfile();
    if (!::chat::meshcore::buildFrameNoTransport(profile,
                                                 kRouteTypeDirect,
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
    return sendRawDataEx(::chat::meshcore::kMeshCorePayloadVer1,
                         path, path_len, payload, payload_len, out_est_timeout);
}

bool MeshCoreRadioAdapter::sendRawDataEx(uint8_t raw_profile, const uint8_t* path, size_t path_len,
                                         const uint8_t* payload, size_t payload_len,
                                         uint32_t* out_est_timeout)
{
    if (!payload || payload_len == 0 || path_len > 64 || (path_len > 0 && !path))
    {
        return false;
    }
    const ::chat::meshcore::PayloadProfile profile =
        raw_profile == ::chat::meshcore::kMeshCorePayloadVer2
            ? ::chat::meshcore::PayloadProfile::V2
            : ::chat::meshcore::PayloadProfile::V1;
    if (!::chat::meshcore::pathIsWellFormed(profile, path_len))
    {
        return false;
    }

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    if (!::chat::meshcore::buildFrameNoTransport(profile,
                                                 kRouteTypeDirect,
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
        *out_est_timeout = estimateTimeoutMs(config_,
                                             frame_len,
                                             ::chat::meshcore::pathHopCount(profile, path_len),
                                             false);
    }
    return true;
}

bool MeshCoreRadioAdapter::sendAppAck(uint32_t signature)
{
    if (signature == 0 || !config_.tx_enabled)
    {
        return false;
    }

    uint8_t payload[sizeof(uint32_t)] = {};
    std::memcpy(payload, &signature, sizeof(payload));

    uint8_t frame[kMeshcoreMaxFrameSize] = {};
    size_t frame_len = 0;
    const ::chat::meshcore::PayloadProfile profile = payloadProfile();
    if (!::chat::meshcore::buildFrameNoTransport(profile,
                                                 kRouteTypeFlood,
                                                 kPayloadTypeAck,
                                                 nullptr,
                                                 0,
                                                 payload,
                                                 sizeof(payload),
                                                 frame,
                                                 sizeof(frame),
                                                 &frame_len))
    {
        return false;
    }
    return transmitFrame(frame, frame_len);
}

::chat::MessageId MeshCoreRadioAdapter::allocateMessageId(::chat::MessageId forced_msg_id)
{
    if (forced_msg_id != 0)
    {
        if (forced_msg_id >= next_message_id_)
        {
            next_message_id_ = forced_msg_id + 1;
            if (next_message_id_ == 0)
            {
                next_message_id_ = 1;
            }
        }
        return forced_msg_id;
    }

    ::chat::MessageId id = next_message_id_++;
    if (id == 0)
    {
        id = next_message_id_++;
    }
    if (next_message_id_ == 0)
    {
        next_message_id_ = 1;
    }
    return id;
}

void MeshCoreRadioAdapter::rememberLocalTextAck(uint32_t signature)
{
    if (signature == 0)
    {
        return;
    }
    if (isLocalTextAck(signature))
    {
        return;
    }
    local_text_ack_signatures_[local_text_ack_next_ % local_text_ack_signatures_.size()] = signature;
    local_text_ack_next_ = static_cast<uint8_t>((local_text_ack_next_ + 1U) %
                                                local_text_ack_signatures_.size());
}

bool MeshCoreRadioAdapter::isLocalTextAck(uint32_t signature) const
{
    if (signature == 0)
    {
        return false;
    }
    for (const uint32_t local_signature : local_text_ack_signatures_)
    {
        if (local_signature == signature)
        {
            return true;
        }
    }
    return false;
}

void MeshCoreRadioAdapter::forgetLocalTextAck(uint32_t signature)
{
    if (signature == 0)
    {
        return;
    }
    for (uint32_t& local_signature : local_text_ack_signatures_)
    {
        if (local_signature == signature)
        {
            local_signature = 0;
        }
    }
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
    const bool ok = io && io->transmit(data, size);
    Serial.printf("[MESHCORE] TX raw len=%u ok=%u\n",
                  static_cast<unsigned>(size),
                  ok ? 1U : 0U);
    return ok;
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
