#pragma once

#include "chat/infra/meshcore/meshcore_identity_crypto.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"
#include "chat/runtime/protocol_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>

namespace chat::runtime
{

constexpr uint32_t kMeshCoreDiscoverRxGuardDefaultMs = 5000;
constexpr uint32_t kMeshCoreAutoDiscoverCooldownMs = 8000;
constexpr int32_t kMeshCoreActionDetailInvalidInput = -4;

struct MeshCoreAutoDiscoverMissingPeerInput
{
    uint8_t peer_hash = 0;
    uint8_t self_hash = 0;
    uint32_t cooldown_ms = kMeshCoreAutoDiscoverCooldownMs;
    uint8_t type_filter = chat::meshcore::kMeshCoreDiscoverTypeFilterAll;
    bool prefix_only = false;
    uint32_t since = 0;
    uint32_t rx_guard_ms = kMeshCoreDiscoverRxGuardDefaultMs;
};

struct MeshCoreAppAckRegistration
{
    uint32_t signature = 0;
    NodeId peer = 0;
    uint32_t portnum = 0;
    MessageId message_id = 0;
    uint32_t timeout_ms = 15000;
};

class MeshCoreRuntime final : public IProtocolRuntime
{
  public:
    void prepareOutgoing(const ProtocolIntent& intent,
                         const RuntimeContext& context,
                         ProtocolEffects& effects) override
    {
        std::visit(
            [this, &effects, &context](const auto& item)
            {
                using Intent = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<Intent, SendTextIntent>)
                {
                    resolveSendText(item, context, effects);
                }
                else if constexpr (std::is_same_v<Intent, RequestNodeInfoIntent>)
                {
                    effects.add(resolveRequestNodeInfo(item, context));
                }
                else if constexpr (std::is_same_v<Intent, TraceRouteIntent>)
                {
                    resolveTraceRoute(item, context, effects);
                }
                else if constexpr (std::is_same_v<Intent, DiscoverIntent>)
                {
                    resolveDiscover(item, context, effects);
                }
            },
            intent);
    }

    void handleIncoming(const IncomingPacket& packet,
                        const RuntimeContext& context,
                        ProtocolEffects& effects) override
    {
        (void)handleIncomingPacket(packet, context, effects);
    }

    IncomingPacketHandlingResult handleIncomingPacket(
        const IncomingPacket& packet,
        const RuntimeContext& context,
        ProtocolEffects& effects) override
    {
        IncomingPacketHandlingResult result{};
        if (packet.protocol != MeshProtocol::MeshCore)
        {
            return result;
        }

        if (absorbIncomingHandlingResult(result, handleIncomingAck(packet, context, effects)))
        {
            return result;
        }
        if (absorbIncomingHandlingResult(result,
                                         handleIncomingDiscoverControl(packet, context, effects)))
        {
            return result;
        }
        if (absorbIncomingHandlingResult(result, handleIncomingNodeInfoControl(packet,
                                                                               context,
                                                                               effects)))
        {
            return result;
        }
        if (absorbIncomingHandlingResult(result, handleIncomingTrace(packet,
                                                                     context,
                                                                     effects)))
        {
            return result;
        }
        absorbIncomingHandlingResult(result, handleIncomingTextOrAppData(packet,
                                                                         context,
                                                                         effects));
        return result;
    }

    static IncomingPacketHandlingResult handleIncomingAck(
        const IncomingPacket& packet,
        const RuntimeContext& context,
        ProtocolEffects& effects)
    {
        (void)packet;
        (void)context;
        (void)effects;
        return {};
    }

    static IncomingPacketHandlingResult handleIncomingNodeInfoControl(
        const IncomingPacket& packet,
        const RuntimeContext& context,
        ProtocolEffects& effects)
    {
        if (packet.portnum != chat::meshcore::kMeshCoreNodeInfoPortnum)
        {
            return {};
        }

        IncomingPacketHandlingResult result{};
        result.handling = PacketHandling::HandledStop;
        chat::meshcore::DecodedNodeInfoControl decoded{};
        if (!chat::meshcore::decodeNodeInfoControlPayload(packet.payload.data(),
                                                          packet.payload.size(),
                                                          &decoded))
        {
            return result;
        }

        if (decoded.type == chat::meshcore::MeshCoreNodeInfoControlType::Query)
        {
            if (decoded.request_reply)
            {
                SendNodeInfoEffect reply{};
                reply.protocol = MeshProtocol::MeshCore;
                reply.channel = packet.channel;
                reply.peer = (packet.from != 0 && packet.from != context.self_node)
                                 ? packet.from
                                 : 0;
                reply.want_response = false;
                effects.add(reply);
            }
            return result;
        }

        if (decoded.type == chat::meshcore::MeshCoreNodeInfoControlType::Info &&
            decoded.complete)
        {
            NodeId node = decoded.info.node_id;
            if (node == 0)
            {
                node = packet.from;
            }
            if (node == 0)
            {
                return result;
            }

            PublishNodeInfoEffect publish{};
            publish.protocol = MeshProtocol::MeshCore;
            publish.channel = packet.channel;
            publish.node_id = node;
            publish.short_name = decoded.info.short_name;
            publish.long_name = decoded.info.long_name;
            publish.timestamp = decoded.info.timestamp;
            publish.role = decoded.info.role;
            publish.hops = decoded.info.hops;
            if (packet.rx_meta.hop_count != 0xFF)
            {
                publish.hops = packet.rx_meta.hop_count;
            }
            publish.rx_meta = packet.rx_meta;
            effects.add(std::move(publish));
        }

        return result;
    }

    void handleTxResult(const TxResult& result,
                        const RuntimeContext& context,
                        ProtocolTxFeedbackEffects& effects) override
    {
        (void)context;
        if (result.protocol != MeshProtocol::MeshCore ||
            !pending_trace_.active ||
            result.request_id != pending_trace_.request_id)
        {
            return;
        }

        if (result.ok)
        {
            if (result.detail > 0)
            {
                pending_trace_.timeout_ms = static_cast<uint32_t>(result.detail);
            }
            EmitActionResultEffect delivered{};
            delivered.protocol = MeshProtocol::MeshCore;
            delivered.action = ProtocolActionKind::TraceRoute;
            delivered.state = ProtocolActionState::Delivered;
            delivered.peer = pending_trace_.peer;
            delivered.request_id = pending_trace_.request_id;
            delivered.detail = result.detail;
            effects.add(delivered);
            return;
        }

        effects.add(buildTraceResult(ProtocolActionState::Failed, result.detail));
        pending_trace_ = PendingTraceRoute{};
    }

    void tick(const RuntimeContext& context, ProtocolEffects& effects) override
    {
        pruneAppAcks(context, effects);

        if (pending_trace_.active)
        {
            const uint32_t elapsed = context.now_ms - pending_trace_.started_ms;
            if (elapsed >= pending_trace_.timeout_ms)
            {
                effects.add(buildTraceResult(ProtocolActionState::TimedOut, 0));
                pending_trace_ = PendingTraceRoute{};
            }
        }
    }

    void trackAppAck(const MeshCoreAppAckRegistration& input,
                     const RuntimeContext& context,
                     ProtocolEffects& effects)
    {
        pruneAppAcks(context, effects);
        if (input.signature == 0 || input.peer == 0)
        {
            return;
        }

        const uint32_t timeout_ms = input.timeout_ms == 0
                                        ? kDefaultAppAckTimeoutMs
                                        : input.timeout_ms;
        if (PendingAppAck* ack = pending_app_acks_.find(input.signature))
        {
            ack->peer = input.peer;
            ack->portnum = input.portnum;
            ack->message_id = input.message_id;
            ack->created_ms = context.now_ms;
            ack->timeout_ms = timeout_ms;
            return;
        }

        PendingAppAck ack{};
        ack.signature = input.signature;
        ack.peer = input.peer;
        ack.portnum = input.portnum;
        ack.message_id = input.message_id;
        ack.created_ms = context.now_ms;
        ack.timeout_ms = timeout_ms;
        PendingAppAck dropped{};
        if (pending_app_acks_.push(ack, &dropped))
        {
            effects.add(buildAppAckResult(dropped, ProtocolActionState::Failed, 0));
        }
    }

    void bindAppAckToMessage(uint32_t signature,
                             MessageId message_id,
                             ProtocolEffects& effects)
    {
        (void)effects;
        if (signature == 0 || message_id == 0)
        {
            return;
        }

        if (PendingAppAck* ack = pending_app_acks_.find(signature))
        {
            ack->message_id = message_id;
        }
    }

    void handleAppAck(uint32_t signature,
                      const RuntimeContext& context,
                      ProtocolEffects& effects)
    {
        pruneAppAcks(context, effects);
        if (signature == 0)
        {
            return;
        }

        PendingAppAck ack{};
        if (pending_app_acks_.removeBySignature(signature, &ack))
        {
            const int32_t trip_ms = static_cast<int32_t>(context.now_ms - ack.created_ms);
            effects.add(buildAppAckResult(ack, ProtocolActionState::Completed, trip_ms));
        }
    }

    void prepareAutoDiscoverMissingPeer(
        const MeshCoreAutoDiscoverMissingPeerInput& input,
        const RuntimeContext& context,
        ProtocolEffects& effects)
    {
        const uint8_t self_hash = input.self_hash != 0
                                      ? input.self_hash
                                      : static_cast<uint8_t>(context.self_node & 0xFFU);
        if (input.peer_hash == 0x00 ||
            input.peer_hash == 0xFF ||
            input.peer_hash == self_hash)
        {
            return;
        }

        if (auto_discover_.last_hash == input.peer_hash &&
            auto_discover_.last_success_ms != 0 &&
            (context.now_ms - auto_discover_.last_success_ms) < input.cooldown_ms)
        {
            return;
        }

        DiscoverIntent intent{};
        intent.action = MeshDiscoveryAction::ScanLocal;
        intent.type_filter = input.type_filter;
        intent.prefix_only = input.prefix_only;
        intent.since = input.since;
        intent.rx_guard_ms = input.rx_guard_ms;
        resolveDiscover(intent, context, effects);
    }

    void markAutoDiscoverMissingPeerTxResult(uint8_t peer_hash,
                                             const RuntimeContext& context,
                                             bool ok)
    {
        if (!ok)
        {
            return;
        }
        auto_discover_.last_hash = peer_hash;
        auto_discover_.last_success_ms = context.now_ms;
    }

    void resetAutoDiscoverState()
    {
        auto_discover_ = AutoDiscoverState{};
    }

    static SendNodeInfoEffect resolveRequestNodeInfo(const RequestNodeInfoIntent& intent,
                                                     const RuntimeContext& context)
    {
        (void)context;
        SendNodeInfoEffect effect{};
        effect.protocol = MeshProtocol::MeshCore;
        effect.peer = normalizePeer(intent.peer);
        effect.want_response = intent.want_response;
        return effect;
    }

  private:
    struct PendingTraceRoute
    {
        bool active = false;
        NodeId peer = 0;
        MessageId request_id = 0;
        uint32_t started_ms = 0;
        uint32_t timeout_ms = 0;
    };

    struct PendingAppAck
    {
        uint32_t signature = 0;
        NodeId peer = 0;
        uint32_t portnum = 0;
        MessageId message_id = 0;
        uint32_t created_ms = 0;
        uint32_t timeout_ms = 0;
    };

    static constexpr size_t kMaxPendingAppAcks = 32;

    class PendingAppAckTable
    {
      public:
        bool empty() const
        {
            return count_ == 0;
        }

        PendingAppAck* find(uint32_t signature)
        {
            for (size_t index = 0; index < count_; ++index)
            {
                if (items_[index].signature == signature)
                {
                    return &items_[index];
                }
            }
            return nullptr;
        }

        const PendingAppAck* oldest() const
        {
            return count_ == 0 ? nullptr : &items_[0];
        }

        bool push(PendingAppAck ack, PendingAppAck* dropped)
        {
            bool did_drop = false;
            if (count_ >= kMaxPendingAppAcks)
            {
                did_drop = removeOldest(dropped);
            }
            else if (dropped)
            {
                *dropped = PendingAppAck{};
            }

            items_[count_++] = ack;
            return did_drop;
        }

        bool removeOldest(PendingAppAck* out)
        {
            if (count_ == 0)
            {
                if (out)
                {
                    *out = PendingAppAck{};
                }
                return false;
            }
            if (out)
            {
                *out = items_[0];
            }
            removeAt(0);
            return true;
        }

        bool removeBySignature(uint32_t signature, PendingAppAck* out)
        {
            for (size_t index = 0; index < count_; ++index)
            {
                if (items_[index].signature != signature)
                {
                    continue;
                }
                if (out)
                {
                    *out = items_[index];
                }
                removeAt(index);
                return true;
            }
            if (out)
            {
                *out = PendingAppAck{};
            }
            return false;
        }

      private:
        void removeAt(size_t index)
        {
            if (index >= count_)
            {
                return;
            }
            for (size_t move = index + 1; move < count_; ++move)
            {
                items_[move - 1] = items_[move];
            }
            --count_;
            items_[count_] = PendingAppAck{};
        }

        std::array<PendingAppAck, kMaxPendingAppAcks> items_{};
        size_t count_ = 0;
    };

    struct AutoDiscoverState
    {
        uint8_t last_hash = 0;
        uint32_t last_success_ms = 0;
    };

    static constexpr uint32_t kDefaultTraceTimeoutMs = 15000;
    static constexpr uint32_t kDefaultAppAckTimeoutMs = 15000;
    static constexpr uint32_t kDefaultDiscoverRxGuardMs = kMeshCoreDiscoverRxGuardDefaultMs;
    static constexpr uint32_t kTextMessageSalt = 0x4D435854UL;
    static constexpr uint32_t kMinValidEpochSeconds = 1577836800UL;

    static NodeId normalizePeer(NodeId peer)
    {
        return peer == 0xFFFFFFFFUL ? 0 : peer;
    }

    static MessageId makeTextMessageId(const SendTextIntent& intent,
                                       const RuntimeContext& context)
    {
        if (intent.message_id != 0)
        {
            return intent.message_id;
        }
        const NodeId peer = normalizePeer(intent.peer);
        MessageId id = static_cast<MessageId>(
            context.now_ms ^ context.self_node ^ peer ^ kTextMessageSalt);
        return id == 0 ? 1 : id;
    }

    static MessageId makeTraceRequestId(const TraceRouteIntent& intent,
                                        const RuntimeContext& context)
    {
        if (intent.request_id != 0)
        {
            return intent.request_id;
        }
        MessageId id = static_cast<MessageId>(
            context.now_ms ^ context.self_node ^ intent.peer ^ 0x4D435452UL);
        return id == 0 ? 1 : id;
    }

    static uint32_t makeDiscoverTag(const DiscoverIntent& intent,
                                    const RuntimeContext& context)
    {
        if (intent.tag != 0)
        {
            return intent.tag;
        }
        uint32_t tag = context.now_ms ^ context.self_node ^ 0x4D434453UL;
        return tag == 0 ? 1 : tag;
    }

    static bool isValidEpoch(uint32_t seconds)
    {
        return seconds >= kMinValidEpochSeconds;
    }

    static EmitActionResultEffect buildFailedAction(ProtocolActionKind action,
                                                    NodeId peer,
                                                    MessageId request_id,
                                                    int32_t detail)
    {
        EmitActionResultEffect failed{};
        failed.protocol = MeshProtocol::MeshCore;
        failed.action = action;
        failed.state = ProtocolActionState::Failed;
        failed.peer = peer;
        failed.request_id = request_id;
        failed.detail = detail;
        return failed;
    }

    static int16_t snrQuarterDbToX10(int8_t snr_qdb)
    {
        const int32_t value = static_cast<int32_t>(snr_qdb) * 10;
        return static_cast<int16_t>((value >= 0 ? value + 2 : value - 2) / 4);
    }

    static std::string makeDiscoverFallbackName(uint8_t peer_hash)
    {
        char name[8] = {};
        std::snprintf(name, sizeof(name), "%02X", static_cast<unsigned>(peer_hash));
        return std::string{name};
    }

    static void resolveSendText(const SendTextIntent& intent,
                                const RuntimeContext& context,
                                ProtocolEffects& effects)
    {
        const NodeId peer = normalizePeer(intent.peer);
        const MessageId message_id = makeTextMessageId(intent, context);
        if (intent.text.empty())
        {
            effects.add(buildFailedAction(ProtocolActionKind::SendText,
                                          peer,
                                          message_id,
                                          kMeshCoreActionDetailInvalidInput));
            return;
        }

        SendTextEffect text{};
        text.protocol = MeshProtocol::MeshCore;
        text.channel = intent.channel;
        text.peer = peer;
        text.message_id = message_id;
        text.text = intent.text;
        effects.add(std::move(text));
    }

    void resolveDiscover(const DiscoverIntent& intent,
                         const RuntimeContext& context,
                         ProtocolEffects& effects)
    {
        switch (intent.action)
        {
        case MeshDiscoveryAction::ScanLocal:
        {
            SendDiscoverRequestEffect effect{};
            effect.protocol = MeshProtocol::MeshCore;
            effect.tag = makeDiscoverTag(intent, context);
            effect.type_filter = intent.type_filter;
            effect.prefix_only = intent.prefix_only;
            effect.since = intent.since;
            effect.rx_guard_ms = intent.rx_guard_ms == 0
                                     ? kDefaultDiscoverRxGuardMs
                                     : intent.rx_guard_ms;
            effects.add(effect);
            break;
        }
        case MeshDiscoveryAction::SendIdLocal:
        case MeshDiscoveryAction::SendIdBroadcast:
        {
            SendSelfAnnouncementEffect effect{};
            effect.protocol = MeshProtocol::MeshCore;
            effect.broadcast = intent.action == MeshDiscoveryAction::SendIdBroadcast;
            effects.add(effect);
            break;
        }
        default:
            break;
        }
    }

    IncomingPacketHandlingResult handleIncomingDiscoverControl(
        const IncomingPacket& packet,
        const RuntimeContext& context,
        ProtocolEffects& effects)
    {
        if (packet.payload_type != chat::meshcore::kMeshCorePayloadTypeControl ||
            packet.payload.empty() ||
            (packet.payload[0] & 0x80U) == 0)
        {
            return {};
        }

        IncomingPacketHandlingResult result{};
        result.handling = PacketHandling::HandledStop;
        chat::meshcore::DecodedDiscoverRequest request{};
        if (chat::meshcore::decodeDiscoverRequest(packet.payload.data(),
                                                  packet.payload.size(),
                                                  &request))
        {
            if (!chat::meshcore::discoverFilterMatchesType(request.type_filter,
                                                           context.meshcore_discover_node_type))
            {
                return result;
            }

            if (request.since != 0 &&
                isValidEpoch(request.since) &&
                isValidEpoch(context.meshcore_local_modified_epoch) &&
                context.meshcore_local_modified_epoch < request.since)
            {
                return result;
            }

            SendDiscoverResponseEffect response{};
            response.protocol = MeshProtocol::MeshCore;
            response.tag = request.tag;
            response.prefix_only = request.prefix_only;
            effects.add(response);
            return result;
        }

        chat::meshcore::DecodedDiscoverResponse response{};
        if (!chat::meshcore::decodeDiscoverResponse(packet.payload.data(),
                                                    packet.payload.size(),
                                                    &response) ||
            response.pubkey_len == 0 ||
            response.pubkey == nullptr)
        {
            return result;
        }

        const uint8_t peer_hash = response.pubkey[0];
        const NodeId node = chat::meshcore::deriveNodeIdFromPubkey(response.pubkey,
                                                                   response.pubkey_len);
        // A one-byte routing hash is not an identity: distinct nearby nodes
        // frequently share that prefix.
        if (node == 0 || node == context.self_node)
        {
            return result;
        }

        const uint8_t hops = packet.path.size() <= 255
                                 ? static_cast<uint8_t>(packet.path.size())
                                 : 0xFF;
        auto rx_meta = packet.rx_meta;
        rx_meta.hop_count = hops;
        rx_meta.snr_db_x10 = snrQuarterDbToX10(response.snr_qdb);

        PublishNodeInfoEffect publish{};
        publish.protocol = MeshProtocol::MeshCore;
        publish.channel = packet.channel;
        publish.node_id = node;
        publish.short_name = makeDiscoverFallbackName(peer_hash);
        publish.long_name = publish.short_name;
        publish.role = chat::meshcore::mapAdvertTypeToRole(response.node_type);
        publish.hops = hops;
        publish.rx_meta = rx_meta;
        publish.has_public_key = response.pubkey_len == chat::meshcore::kMeshCorePubKeySize;
        effects.add(std::move(publish));

        UpdatePeerRouteEffect route{};
        route.protocol = MeshProtocol::MeshCore;
        route.peer = node;
        route.peer_hash = peer_hash;
        route.hops = hops;
        route.tag = response.tag;
        route.payload.assign(packet.payload.begin(), packet.payload.end());
        if (response.pubkey_len == chat::meshcore::kMeshCorePubKeySize)
        {
            route.public_key.assign(response.pubkey, response.pubkey + response.pubkey_len);
        }
        effects.add(std::move(route));
        return result;
    }

    void resolveTraceRoute(const TraceRouteIntent& intent,
                           const RuntimeContext& context,
                           ProtocolEffects& effects)
    {
        const NodeId peer = normalizePeer(intent.peer);
        const MessageId request_id = makeTraceRequestId(intent, context);
        if (peer == 0 || peer == context.self_node)
        {
            EmitActionResultEffect failed{};
            failed.protocol = MeshProtocol::MeshCore;
            failed.action = ProtocolActionKind::TraceRoute;
            failed.state = ProtocolActionState::Failed;
            failed.peer = peer;
            failed.request_id = request_id;
            failed.detail = -1;
            effects.add(failed);
            return;
        }

        pending_trace_.active = true;
        pending_trace_.peer = peer;
        pending_trace_.request_id = request_id;
        pending_trace_.started_ms = context.now_ms;
        pending_trace_.timeout_ms = intent.timeout_ms == 0
                                        ? kDefaultTraceTimeoutMs
                                        : intent.timeout_ms;

        SendTraceRouteEffect effect{};
        effect.protocol = MeshProtocol::MeshCore;
        effect.peer = peer;
        effect.request_id = request_id;
        effect.auth = intent.auth;
        effect.flags = intent.flags;
        effect.timeout_ms = pending_trace_.timeout_ms;
        effects.add(effect);
    }

    IncomingPacketHandlingResult handleIncomingTrace(const IncomingPacket& packet,
                                                     const RuntimeContext& context,
                                                     ProtocolEffects& effects)
    {
        (void)context;
        if (packet.payload_type != chat::meshcore::kMeshCorePayloadTypeTrace)
        {
            return {};
        }

        IncomingPacketHandlingResult result{};
        result.handling = PacketHandling::HandledStop;
        chat::meshcore::DecodedTracePayload trace{};
        if (!chat::meshcore::decodeTracePayload(packet.payload.data(),
                                                packet.payload.size(),
                                                packet.path.size(),
                                                &trace) ||
            !trace.terminal ||
            !pending_trace_.active ||
            trace.tag != pending_trace_.request_id)
        {
            return result;
        }

        EmitActionResultEffect completed{};
        completed.protocol = MeshProtocol::MeshCore;
        completed.action = ProtocolActionKind::TraceRoute;
        completed.state = ProtocolActionState::Completed;
        completed.peer = pending_trace_.peer;
        completed.request_id = pending_trace_.request_id;
        completed.detail = static_cast<int32_t>(packet.path.size());
        effects.add(completed);
        pending_trace_ = PendingTraceRoute{};
        return result;
    }

    static IncomingPacketHandlingResult handleIncomingTextOrAppData(
        const IncomingPacket& packet,
        const RuntimeContext& context,
        ProtocolEffects& effects)
    {
        (void)packet;
        (void)context;
        (void)effects;
        return {};
    }

    EmitActionResultEffect buildTraceResult(ProtocolActionState state, int32_t detail) const
    {
        EmitActionResultEffect effect{};
        effect.protocol = MeshProtocol::MeshCore;
        effect.action = ProtocolActionKind::TraceRoute;
        effect.state = state;
        effect.peer = pending_trace_.peer;
        effect.request_id = pending_trace_.request_id;
        effect.detail = detail;
        return effect;
    }

    static EmitActionResultEffect buildAppAckResult(const PendingAppAck& ack,
                                                    ProtocolActionState state,
                                                    int32_t detail)
    {
        EmitActionResultEffect effect{};
        effect.protocol = MeshProtocol::MeshCore;
        effect.action = ack.message_id != 0
                            ? ProtocolActionKind::SendText
                            : ProtocolActionKind::Unknown;
        effect.state = state;
        effect.peer = ack.peer;
        effect.request_id = ack.signature;
        effect.message_id = ack.message_id;
        effect.detail = detail;
        return effect;
    }

    void pruneAppAcks(const RuntimeContext& context, ProtocolEffects& effects)
    {
        while (!pending_app_acks_.empty())
        {
            const PendingAppAck* oldest = pending_app_acks_.oldest();
            if (!oldest)
            {
                break;
            }
            const uint32_t elapsed = context.now_ms - oldest->created_ms;
            if (elapsed < oldest->timeout_ms)
            {
                break;
            }
            if (effects.full())
            {
                effects.markOverflowed();
                break;
            }
            PendingAppAck ack{};
            pending_app_acks_.removeOldest(&ack);
            effects.add(buildAppAckResult(ack,
                                          ProtocolActionState::TimedOut,
                                          static_cast<int32_t>(elapsed)));
        }
    }

    PendingTraceRoute pending_trace_{};
    PendingAppAckTable pending_app_acks_{};
    AutoDiscoverState auto_discover_{};
};

} // namespace chat::runtime
