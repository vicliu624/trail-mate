#include "app/linux_app_facade.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "app/app_facade_access.h"
#include "chat/domain/chat_model.h"
#include "chat/infra/contact_store_core.h"
#include "chat/infra/node_store_core.h"
#include "chat/infra/store/ram_store.h"
#include "chat/ports/i_contact_blob_store.h"
#include "chat/ports/i_node_blob_store.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "sys/clock.h"
#include "sys/event_bus.h"
#include "team/ports/i_team_crypto.h"
#include "team/ports/i_team_event_sink.h"
#include "team/ports/i_team_pairing_event_sink.h"
#include "team/ports/i_team_pairing_transport.h"
#include "team/ports/i_team_runtime.h"
#include "team/ports/i_team_track_source.h"
#include "team/protocol/team_position.h"
#include "team/protocol/team_pairing_wire.h"
#include "team/usecase/team_controller.h"
#include "team/usecase/team_pairing_coordinator.h"
#include "team/usecase/team_pairing_service.h"
#include "team/usecase/team_service.h"
#include "team/usecase/team_track_sampler.h"
#include "ui/chat_ui_runtime.h"

namespace trailmate::cardputer_zero::linux_ui
{
namespace
{

TeamUiEventDispatcher s_team_ui_event_dispatcher = nullptr;

constexpr const char* kConfigNamespace = "linux_app_facade";
constexpr const char* kConfigBlobKey = "app_config_v1";
constexpr uint32_t kConfigBlobMagic = 0x544D4346U; // TMCF
constexpr uint32_t kConfigBlobVersion = 1U;

constexpr const char* kNodeStoreNamespace = "linux_contact_nodes";
constexpr const char* kNodeStoreKey = "nodes_v1";
constexpr const char* kContactStoreNamespace = "linux_contact_names";
constexpr const char* kContactStoreKey = "contacts_v1";

constexpr ::chat::NodeId kDemoAlphaNodeId = 0x435A1001U;
constexpr ::chat::NodeId kDemoBravoNodeId = 0x435A1002U;
constexpr ::chat::NodeId kDemoScoutNodeId = 0x435A1003U;
constexpr ::chat::NodeId kDemoNearbyNodeId = 0x435A1004U;
constexpr ::chat::NodeId kDemoBroadcastNodeId = kDemoAlphaNodeId;
constexpr ::chat::NodeId kSyntheticPairLeaderNodeId = kDemoAlphaNodeId;
constexpr ::chat::NodeId kSyntheticPairMemberNodeId = kDemoBravoNodeId;

constexpr std::array<uint8_t, 6> kSyntheticLeaderMac{{0x43, 0x5A, 0x20, 0x01, 0x00, 0x01}};
constexpr std::array<uint8_t, 6> kSyntheticMemberMac{{0x43, 0x5A, 0x20, 0x01, 0x00, 0x02}};

constexpr uint32_t kSyntheticPairKeyId = 1U;
constexpr uint32_t kSyntheticPairNonce = 0x5A431122U;
constexpr uint32_t kSyntheticPairDelayMs = 220U;
constexpr uint32_t kAutoReplyDelayMs = 700U;

team::TeamId makeSyntheticPairTeamId()
{
    team::TeamId team_id{};
    const std::array<uint8_t, 8> bytes{{'C', 'Z', 'T', 'E', 'A', 'M', '0', '1'}};
    for (size_t i = 0; i < team_id.size() && i < bytes.size(); ++i)
    {
        team_id[i] = bytes[i];
    }
    return team_id;
}

std::array<uint8_t, team::proto::kTeamChannelPskSize> makeSyntheticPairPsk()
{
    std::array<uint8_t, team::proto::kTeamChannelPskSize> psk{};
    for (size_t i = 0; i < psk.size(); ++i)
    {
        psk[i] = static_cast<uint8_t>(0x30U + (i * 7U + 3U) % 0x4FU);
    }
    return psk;
}

const char* syntheticPairTeamName()
{
    return "Field Team";
}

std::string makeAutoReplyText(::chat::NodeId peer, const std::string& text)
{
    const std::string trimmed = text.substr(0, std::min<std::size_t>(text.size(), 28U));
    switch (peer)
    {
    case kDemoAlphaNodeId:
        return "Alice copied: " + trimmed;
    case kDemoBravoNodeId:
        return "Bravo link OK: " + trimmed;
    case kDemoScoutNodeId:
        return "Scout received your ping.";
    default:
        return "Peer ack: " + trimmed;
    }
}

struct DemoPeerSeed
{
    ::chat::NodeId node_id = 0;
    const char* short_name = nullptr;
    const char* long_name = nullptr;
    const char* nickname = nullptr;
    bool ignored = false;
    float snr = 0.0f;
    float rssi = 0.0f;
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
};

std::array<DemoPeerSeed, 4> demoPeerSeeds()
{
    return {{
        {kDemoAlphaNodeId, "ALFA", "Alice Local", "Alice", false, 11.2f, -72.0f, 311214000, 1214737000},
        {kDemoBravoNodeId, "BRAV", "Bravo Pager", "Bravo", false, 8.6f, -79.0f, 311218500, 1214749000},
        {kDemoScoutNodeId, "SCOT", "Scout Relay", nullptr, true, 4.1f, -94.0f, 311227000, 1214762000},
        {kDemoNearbyNodeId, "NBY1", "Nearby Relay", nullptr, false, 6.8f, -88.0f, 311231200, 1214756000},
    }};
}

struct PersistedConfigBlob
{
    uint32_t magic = kConfigBlobMagic;
    uint32_t version = kConfigBlobVersion;
    ::app::AppConfig config{};
};

static_assert(std::is_trivially_copyable_v<::app::AppConfig>,
              "MinimalLinuxAppFacade persists AppConfig as an opaque blob.");

void copy_bounded(char* out, std::size_t out_len, const char* text)
{
    if (out == nullptr || out_len == 0)
    {
        return;
    }

    if (text == nullptr)
    {
        out[0] = '\0';
        return;
    }

    std::strncpy(out, text, out_len - 1U);
    out[out_len - 1U] = '\0';
}

bool is_supported_protocol(::chat::MeshProtocol protocol)
{
    switch (protocol)
    {
    case ::chat::MeshProtocol::Meshtastic:
    case ::chat::MeshProtocol::MeshCore:
    case ::chat::MeshProtocol::RNode:
    case ::chat::MeshProtocol::LXMF:
        return true;
    default:
        return false;
    }
}

class LinuxNodeBlobStore final : public ::chat::contacts::INodeBlobStore
{
  public:
    bool loadBlob(std::vector<uint8_t>& out) override
    {
        return ::platform::ui::settings_store::get_blob(kNodeStoreNamespace, kNodeStoreKey, out);
    }

    bool saveBlob(const uint8_t* data, size_t len) override
    {
        return ::platform::ui::settings_store::put_blob(kNodeStoreNamespace, kNodeStoreKey, data, len);
    }

    void clearBlob() override
    {
        ::platform::ui::settings_store::clear_namespace(kNodeStoreNamespace);
    }
};

class LinuxContactBlobStore final : public ::chat::IContactBlobStore
{
  public:
    bool loadBlob(std::vector<uint8_t>& out) override
    {
        return ::platform::ui::settings_store::get_blob(kContactStoreNamespace, kContactStoreKey, out);
    }

    bool saveBlob(const uint8_t* data, size_t len) override
    {
        return ::platform::ui::settings_store::put_blob(kContactStoreNamespace, kContactStoreKey, data, len);
    }

    void clear()
    {
        ::platform::ui::settings_store::clear_namespace(kContactStoreNamespace);
    }
};

class LinuxLoopbackMeshAdapter final : public ::chat::IMeshAdapter
{
  public:
    explicit LinuxLoopbackMeshAdapter(::chat::NodeId self_node_id) : self_node_id_(self_node_id) {}

    ::chat::MeshCapabilities getCapabilities() const override
    {
        return {
            .supports_unicast_text = true,
            .supports_unicast_appdata = true,
            .supports_broadcast_appdata = true,
            .supports_appdata_ack = true,
            .provides_appdata_sender = true,
            .supports_node_info = true,
            .supports_pki = true,
            .supports_discovery_actions = true,
        };
    }

    bool sendText(::chat::ChannelId channel, const std::string& text,
                  ::chat::MessageId* out_msg_id, ::chat::NodeId peer = 0) override
    {
        return sendTextWithId(channel, text, 0, out_msg_id, peer);
    }

    bool sendTextWithId(::chat::ChannelId channel, const std::string& text,
                        ::chat::MessageId forced_msg_id,
                        ::chat::MessageId* out_msg_id, ::chat::NodeId peer = 0) override
    {
        if (text.empty())
        {
            return false;
        }

        const ::chat::MessageId msg_id = (forced_msg_id != 0) ? forced_msg_id : nextMessageId();
        if (out_msg_id)
        {
            *out_msg_id = msg_id;
        }

        pending_send_results_.push_back({msg_id, true});

        if (peer != 0 && peer == self_node_id_)
        {
            ::chat::MeshIncomingText loopback{};
            loopback.channel = channel;
            loopback.from = self_node_id_;
            loopback.to = self_node_id_;
            loopback.msg_id = msg_id;
            loopback.timestamp = sys::epoch_seconds_now();
            loopback.text = text;
            incoming_texts_.push_back(loopback);
        }
        else if (peer != 0)
        {
            scheduleIncomingText({
                .channel = channel,
                .from = peer,
                .to = self_node_id_,
                .msg_id = nextMessageId(),
                .timestamp = sys::epoch_seconds_now(),
                .text = makeAutoReplyText(peer, text),
            },
                                 kAutoReplyDelayMs);
        }
        else
        {
            scheduleIncomingText({
                .channel = channel,
                .from = kDemoBroadcastNodeId,
                .to = 0,
                .msg_id = nextMessageId(),
                .timestamp = sys::epoch_seconds_now(),
                .text = "Broadcast heard: " + text.substr(0, std::min<std::size_t>(text.size(), 32U)),
            },
                                 kAutoReplyDelayMs + 120U);
        }

        return true;
    }

    bool pollIncomingText(::chat::MeshIncomingText* out) override
    {
        if (out == nullptr || incoming_texts_.empty())
        {
            return false;
        }

        *out = incoming_texts_.front();
        incoming_texts_.erase(incoming_texts_.begin());
        return true;
    }

    bool sendAppData(::chat::ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     ::chat::NodeId dest = 0, bool want_ack = false,
                     ::chat::MessageId packet_id = 0,
                     bool want_response = false) override
    {
        (void)channel;
        (void)portnum;
        (void)payload;
        (void)len;
        (void)dest;
        (void)want_ack;
        (void)packet_id;
        (void)want_response;
        return true;
    }

    bool pollIncomingData(::chat::MeshIncomingData* out) override
    {
        if (out == nullptr || incoming_data_.empty())
        {
            return false;
        }

        *out = incoming_data_.front();
        incoming_data_.erase(incoming_data_.begin());
        return true;
    }

    bool requestNodeInfo(::chat::NodeId dest, bool want_response) override
    {
        (void)dest;
        (void)want_response;
        return true;
    }

    bool startKeyVerification(::chat::NodeId dest) override
    {
        (void)dest;
        return true;
    }

    bool submitKeyVerificationNumber(::chat::NodeId dest, uint64_t nonce, uint32_t number) override
    {
        (void)dest;
        (void)nonce;
        (void)number;
        return true;
    }

    ::chat::NodeId getNodeId() const override
    {
        return self_node_id_;
    }

    bool isPkiReady() const override
    {
        return true;
    }

    bool hasPkiKey(::chat::NodeId dest) const override
    {
        return dest != 0;
    }

    bool triggerDiscoveryAction(::chat::MeshDiscoveryAction action) override
    {
        (void)action;
        return true;
    }

    void applyConfig(const ::chat::MeshConfig& config) override
    {
        config_ = config;
    }

    void setUserInfo(const char* long_name, const char* short_name) override
    {
        long_name_ = long_name ? long_name : "";
        short_name_ = short_name ? short_name : "";
    }

    bool isReady() const override
    {
        return true;
    }

    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override
    {
        (void)out_data;
        (void)out_len;
        (void)max_len;
        return false;
    }

    void setSelfNodeId(::chat::NodeId node_id)
    {
        self_node_id_ = node_id;
    }

    void queueIncomingText(const ::chat::MeshIncomingText& msg)
    {
        incoming_texts_.push_back(msg);
    }

    void queueIncomingData(const ::chat::MeshIncomingData& msg)
    {
        incoming_data_.push_back(msg);
    }

    void tick()
    {
        const uint32_t now_ms = sys::millis_now();
        while (!pending_incoming_texts_.empty())
        {
            const auto& pending = pending_incoming_texts_.front();
            if (pending.due_ms > now_ms)
            {
                break;
            }
            incoming_texts_.push_back(pending.message);
            pending_incoming_texts_.pop_front();
        }
    }

    bool takePendingSendResult(::chat::MessageId& out_msg_id, bool& out_ok)
    {
        if (pending_send_results_.empty())
        {
            return false;
        }

        const auto result = pending_send_results_.front();
        pending_send_results_.erase(pending_send_results_.begin());
        out_msg_id = result.first;
        out_ok = result.second;
        return true;
    }

  private:
    struct DelayedIncomingText
    {
        uint32_t due_ms = 0;
        ::chat::MeshIncomingText message{};
    };

    ::chat::MessageId nextMessageId()
    {
        if (next_message_id_ == 0)
        {
            next_message_id_ = 1;
        }
        return next_message_id_++;
    }

    void scheduleIncomingText(::chat::MeshIncomingText msg, uint32_t delay_ms)
    {
        msg.timestamp = sys::epoch_seconds_now();
        pending_incoming_texts_.push_back({
            .due_ms = sys::millis_now() + delay_ms,
            .message = std::move(msg),
        });
    }

    ::chat::NodeId self_node_id_ = 0;
    ::chat::MessageId next_message_id_ = 1;
    ::chat::MeshConfig config_{};
    std::string long_name_{};
    std::string short_name_{};
    std::vector<::chat::MeshIncomingText> incoming_texts_{};
    std::vector<::chat::MeshIncomingData> incoming_data_{};
    std::vector<std::pair<::chat::MessageId, bool>> pending_send_results_{};
    std::deque<DelayedIncomingText> pending_incoming_texts_{};
};

class LinuxTeamRuntime final : public ::team::ITeamRuntime
{
  public:
    uint32_t nowMillis() override
    {
        return sys::millis_now();
    }

    uint32_t nowUnixSeconds() override
    {
        return sys::epoch_seconds_now();
    }

    void fillRandomBytes(uint8_t* out, size_t len) override
    {
        if (out == nullptr || len == 0)
        {
            return;
        }

        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<int> dist(0, 255);
        for (size_t i = 0; i < len; ++i)
        {
            out[i] = static_cast<uint8_t>(dist(gen));
        }
    }
};

class LinuxTeamCrypto final : public ::team::ITeamCrypto
{
  public:
    bool deriveKey(const uint8_t* key, size_t key_len,
                   const char* info,
                   uint8_t* out, size_t out_len) override
    {
        if (key == nullptr || key_len == 0 || out == nullptr || out_len == 0)
        {
            return false;
        }

        uint32_t state = 2166136261u;
        for (size_t i = 0; i < key_len; ++i)
        {
            state ^= key[i];
            state *= 16777619u;
        }
        if (info)
        {
            for (const char* p = info; *p != '\0'; ++p)
            {
                state ^= static_cast<uint8_t>(*p);
                state *= 16777619u;
            }
        }

        for (size_t i = 0; i < out_len; ++i)
        {
            state ^= static_cast<uint32_t>(i + 1U);
            state *= 16777619u;
            out[i] = static_cast<uint8_t>((state >> ((i % 4U) * 8U)) & 0xFFU);
        }
        return true;
    }

    bool aeadEncrypt(const uint8_t* key, size_t key_len,
                     const uint8_t* nonce, size_t nonce_len,
                     const uint8_t* aad, size_t aad_len,
                     const uint8_t* plain, size_t plain_len,
                     std::vector<uint8_t>& out_cipher) override
    {
        return xorCipher(key, key_len, nonce, nonce_len, aad, aad_len, plain, plain_len, out_cipher);
    }

    bool aeadDecrypt(const uint8_t* key, size_t key_len,
                     const uint8_t* nonce, size_t nonce_len,
                     const uint8_t* aad, size_t aad_len,
                     const uint8_t* cipher, size_t cipher_len,
                     std::vector<uint8_t>& out_plain) override
    {
        return xorCipher(key, key_len, nonce, nonce_len, aad, aad_len, cipher, cipher_len, out_plain);
    }

  private:
    static bool xorCipher(const uint8_t* key, size_t key_len,
                          const uint8_t* nonce, size_t nonce_len,
                          const uint8_t* aad, size_t aad_len,
                          const uint8_t* input, size_t input_len,
                          std::vector<uint8_t>& output)
    {
        if (key == nullptr || key_len == 0 || input == nullptr)
        {
            return false;
        }

        uint32_t state = 0x9E3779B9u;
        for (size_t i = 0; i < key_len; ++i)
        {
            state = (state * 33u) ^ key[i];
        }
        for (size_t i = 0; i < nonce_len; ++i)
        {
            state = (state * 33u) ^ nonce[i];
        }
        for (size_t i = 0; i < aad_len; ++i)
        {
            state = (state * 33u) ^ aad[i];
        }

        output.resize(input_len);
        for (size_t i = 0; i < input_len; ++i)
        {
            state = state * 1664525u + 1013904223u;
            output[i] = static_cast<uint8_t>(input[i] ^ ((state >> 24U) & 0xFFU));
        }
        return true;
    }
};

class LinuxChatEventBusBridge final : public ::chat::ChatService::IncomingMessageObserver
{
  public:
    explicit LinuxChatEventBusBridge(::chat::ChatService& service) : service_(service)
    {
        service_.addIncomingMessageObserver(this);
    }

    ~LinuxChatEventBusBridge() override
    {
        service_.removeIncomingMessageObserver(this);
    }

    void onIncomingMessage(const ::chat::ChatMessage& msg, const ::chat::RxMeta* rx_meta) override
    {
        ::sys::EventBus::publish(
            new ::sys::ChatNewMessageEvent(static_cast<uint8_t>(msg.channel),
                                           msg.msg_id,
                                           msg.text.c_str(),
                                           rx_meta),
            0);
    }

  private:
    ::chat::ChatService& service_;
};

class LinuxTeamEventBusSink final : public ::team::ITeamEventSink
{
  public:
    void onTeamKick(const ::team::TeamKickEvent& event) override
    {
        ::sys::EventBus::publish(new ::sys::TeamKickEvent(event), 0);
    }

    void onTeamTransferLeader(const ::team::TeamTransferLeaderEvent& event) override
    {
        ::sys::EventBus::publish(new ::sys::TeamTransferLeaderEvent(event), 0);
    }

    void onTeamKeyDist(const ::team::TeamKeyDistEvent& event) override
    {
        ::sys::EventBus::publish(new ::sys::TeamKeyDistEvent(event), 0);
    }

    void onTeamStatus(const ::team::TeamStatusEvent& event) override
    {
        ::sys::EventBus::publish(new ::sys::TeamStatusEvent(event), 0);
    }

    void onTeamPosition(const ::team::TeamPositionEvent& event) override
    {
        ::team::proto::TeamPositionMessage pos{};
        if (event.ctx.from != 0 &&
            ::team::proto::decodeTeamPositionMessage(event.payload.data(),
                                                     event.payload.size(),
                                                     &pos))
        {
            const uint32_t timestamp = (pos.ts != 0) ? pos.ts : event.ctx.timestamp;
            ::sys::EventBus::publish(
                new ::sys::NodePositionUpdateEvent(
                    event.ctx.from,
                    pos.lat_e7,
                    pos.lon_e7,
                    ::team::proto::teamPositionHasAltitude(pos),
                    ::team::proto::teamPositionHasAltitude(pos) ? pos.alt_m : 0,
                    timestamp,
                    0,
                    0,
                    0,
                    0,
                    0),
                0);
        }

        ::sys::EventBus::publish(new ::sys::TeamPositionEvent(event), 0);
    }

    void onTeamWaypoint(const ::team::TeamWaypointEvent& event) override
    {
        ::sys::EventBus::publish(new ::sys::TeamWaypointEvent(event), 0);
    }

    void onTeamTrack(const ::team::TeamTrackEvent& event) override
    {
        ::sys::EventBus::publish(new ::sys::TeamTrackEvent(event), 0);
    }

    void onTeamChat(const ::team::TeamChatEvent& event) override
    {
        ::sys::EventBus::publish(new ::sys::TeamChatEvent(event), 0);
    }

    void onTeamError(const ::team::TeamErrorEvent& event) override
    {
        ::sys::EventBus::publish(new ::sys::TeamErrorEvent(event), 0);
    }
};

class LinuxTeamPairingEventQueue final : public ::team::ITeamPairingEventSink
{
  public:
    void onTeamPairingStateChanged(const ::team::TeamPairingEvent& event) override
    {
        ::sys::EventBus::publish(new ::sys::TeamPairingEvent(event), 0);
    }

    void onTeamPairingKeyDist(const ::team::TeamKeyDistEvent& event) override
    {
        ::sys::EventBus::publish(new ::sys::TeamKeyDistEvent(event), 0);
    }
};

class LinuxLoopbackTeamPairingTransport final : public ::team::ITeamPairingTransport
{
  public:
    bool begin(Receiver& receiver) override
    {
        receiver_ = &receiver;
        pending_packets_.clear();
        synthetic_member_join_sent_ = false;
        synthetic_leader_ready_ = false;
        return true;
    }

    void end() override
    {
        receiver_ = nullptr;
        pending_packets_.clear();
        synthetic_member_join_sent_ = false;
        synthetic_leader_ready_ = false;
    }

    bool ensurePeer(const uint8_t* mac) override
    {
        return mac != nullptr;
    }

    bool send(const uint8_t* mac, const uint8_t* data, size_t len) override
    {
        if (!mac || !data || len == 0)
        {
            return false;
        }

        ::team::proto::pairing::MessageType type{};
        if (!::team::proto::pairing::decodeType(data, len, &type))
        {
            return false;
        }

        if (type == ::team::proto::pairing::MessageType::Beacon)
        {
            ::team::proto::pairing::BeaconPacket beacon{};
            if (!::team::proto::pairing::decodeBeacon(data, len, &beacon))
            {
                return false;
            }
            if (!synthetic_member_join_sent_)
            {
                scheduleSyntheticMemberJoin(beacon);
                synthetic_member_join_sent_ = true;
            }
            return true;
        }

        if (type == ::team::proto::pairing::MessageType::Join)
        {
            ::team::proto::pairing::JoinPacket join{};
            if (!::team::proto::pairing::decodeJoin(data, len, &join))
            {
                return false;
            }
            if (synthetic_leader_ready_)
            {
                scheduleSyntheticLeaderKey(join);
            }
            return true;
        }

        return true;
    }

    void scheduleSyntheticLeaderBeacon()
    {
        synthetic_leader_ready_ = true;
        synthetic_leader_team_id_ = makeSyntheticPairTeamId();
        synthetic_leader_key_id_ = kSyntheticPairKeyId;
        synthetic_leader_psk_ = makeSyntheticPairPsk();
        synthetic_leader_psk_len_ = static_cast<uint8_t>(synthetic_leader_psk_.size());

        ::team::proto::pairing::BeaconPacket beacon{};
        beacon.team_id = synthetic_leader_team_id_;
        beacon.key_id = synthetic_leader_key_id_;
        beacon.leader_id = kSyntheticPairLeaderNodeId;
        beacon.window_ms = 120000U;
        std::strncpy(beacon.team_name, syntheticPairTeamName(), sizeof(beacon.team_name) - 1U);
        beacon.team_name[sizeof(beacon.team_name) - 1U] = '\0';
        beacon.has_team_name = true;
        schedulePacket(kSyntheticLeaderMac, beacon, kSyntheticPairDelayMs);
    }

    void pump()
    {
        if (receiver_ == nullptr)
        {
            return;
        }

        const uint32_t now_ms = sys::millis_now();
        while (!pending_packets_.empty())
        {
            const auto& packet = pending_packets_.front();
            if (packet.due_ms > now_ms)
            {
                break;
            }
            receiver_->onPairingReceive(packet.mac.data(), packet.payload.data(), packet.payload.size());
            pending_packets_.pop_front();
        }
    }

  private:
    struct PendingPacket
    {
        std::array<uint8_t, 6> mac{};
        std::vector<uint8_t> payload{};
        uint32_t due_ms = 0;
    };

    void scheduleSyntheticMemberJoin(const ::team::proto::pairing::BeaconPacket& beacon)
    {
        ::team::proto::pairing::JoinPacket join{};
        join.team_id = beacon.team_id;
        join.member_id = kSyntheticPairMemberNodeId;
        join.nonce = kSyntheticPairNonce;
        schedulePacket(kSyntheticMemberMac, join, kSyntheticPairDelayMs);
    }

    void scheduleSyntheticLeaderKey(const ::team::proto::pairing::JoinPacket& join)
    {
        ::team::proto::pairing::KeyPacket key{};
        key.team_id = synthetic_leader_team_id_;
        key.key_id = synthetic_leader_key_id_;
        key.nonce = join.nonce;
        key.channel_psk = synthetic_leader_psk_;
        key.channel_psk_len = synthetic_leader_psk_len_;
        schedulePacket(kSyntheticLeaderMac, key, kSyntheticPairDelayMs);
    }

    template <typename PacketT>
    void schedulePacket(const std::array<uint8_t, 6>& mac, const PacketT& packet, uint32_t delay_ms)
    {
        std::vector<uint8_t> wire;
        bool ok = false;
        if constexpr (std::is_same_v<PacketT, ::team::proto::pairing::BeaconPacket>)
        {
            ok = ::team::proto::pairing::encodeBeacon(packet, wire);
        }
        else if constexpr (std::is_same_v<PacketT, ::team::proto::pairing::JoinPacket>)
        {
            ok = ::team::proto::pairing::encodeJoin(packet, wire);
        }
        else if constexpr (std::is_same_v<PacketT, ::team::proto::pairing::KeyPacket>)
        {
            ok = ::team::proto::pairing::encodeKey(packet, wire);
        }

        if (!ok)
        {
            return;
        }

        pending_packets_.push_back({
            .mac = mac,
            .payload = std::move(wire),
            .due_ms = sys::millis_now() + delay_ms,
        });
    }

    Receiver* receiver_ = nullptr;
    std::deque<PendingPacket> pending_packets_{};
    bool synthetic_member_join_sent_ = false;
    bool synthetic_leader_ready_ = false;
    ::team::TeamId synthetic_leader_team_id_{};
    uint32_t synthetic_leader_key_id_ = 0;
    std::array<uint8_t, ::team::proto::kTeamChannelPskSize> synthetic_leader_psk_{};
    uint8_t synthetic_leader_psk_len_ = 0;
};

class LinuxLoopbackTeamPairingService final : public ::team::TeamPairingService,
                                              private ::team::ITeamPairingTransport::Receiver
{
  public:
    LinuxLoopbackTeamPairingService(::team::ITeamRuntime& runtime,
                                    ::team::ITeamPairingEventSink& event_sink,
                                    LinuxLoopbackTeamPairingTransport& transport)
        : transport_(transport), core_(runtime, event_sink, transport)
    {
    }

    bool startLeader(const ::team::TeamId& team_id,
                     uint32_t key_id,
                     const uint8_t* psk,
                     size_t psk_len,
                     uint32_t leader_id,
                     const char* team_name) override
    {
        if (!ensureTransport())
        {
            return false;
        }
        if (!core_.startLeader(team_id, key_id, psk, psk_len, leader_id, team_name))
        {
            shutdownTransport();
            return false;
        }
        return true;
    }

    bool startMember(uint32_t self_id) override
    {
        if (!ensureTransport())
        {
            return false;
        }
        if (!core_.startMember(self_id))
        {
            shutdownTransport();
            return false;
        }
        transport_.scheduleSyntheticLeaderBeacon();
        return true;
    }

    void stop() override
    {
        core_.stop();
        shutdownTransport();
    }

    void update() override
    {
        if (!transport_ready_)
        {
            return;
        }

        transport_.pump();
        while (!rx_packets_.empty())
        {
            const auto packet = rx_packets_.front();
            rx_packets_.pop_front();
            core_.handleIncomingPacket(packet.mac.data(), packet.payload.data(), packet.payload.size());
        }

        core_.update();
        if (transport_ready_ && core_.getStatus().state == ::team::TeamPairingState::Idle)
        {
            shutdownTransport();
        }
    }

    ::team::TeamPairingStatus getStatus() const override
    {
        return core_.getStatus();
    }

  private:
    struct RxPacket
    {
        std::array<uint8_t, 6> mac{};
        std::vector<uint8_t> payload{};
    };

    bool ensureTransport()
    {
        if (transport_ready_)
        {
            return true;
        }
        if (!transport_.begin(*this))
        {
            return false;
        }
        transport_ready_ = true;
        return true;
    }

    void shutdownTransport()
    {
        if (!transport_ready_)
        {
            return;
        }
        transport_.end();
        transport_ready_ = false;
        rx_packets_.clear();
    }

    void onPairingReceive(const uint8_t* mac, const uint8_t* data, size_t len) override
    {
        if (!mac || !data || len == 0)
        {
            return;
        }
        RxPacket packet{};
        std::copy(mac, mac + 6, packet.mac.begin());
        packet.payload.assign(data, data + len);
        rx_packets_.push_back(std::move(packet));
    }

    LinuxLoopbackTeamPairingTransport& transport_;
    ::team::TeamPairingCoordinator core_;
    bool transport_ready_ = false;
    std::deque<RxPacket> rx_packets_{};
};

class LinuxNullTrackSource final : public ::team::ITeamTrackSource
{
  public:
    bool readTrackPoint(::team::proto::TeamTrackPoint* out_point) override
    {
        (void)out_point;
        return false;
    }
};

bool dispatchCoreEvent(MinimalLinuxAppFacade& app_facade, ::sys::Event* event)
{
    if (event == nullptr)
    {
        return true;
    }

    switch (event->type)
    {
    case ::sys::EventType::NodeInfoUpdate:
    {
        auto* node_event = static_cast<::sys::NodeInfoUpdateEvent*>(event);
        ::chat::contacts::NodeUpdate update{};
        update.short_name = node_event->short_name;
        update.long_name = node_event->long_name;
        update.has_last_seen = true;
        update.last_seen = node_event->event_timestamp != 0 ? node_event->event_timestamp : node_event->timestamp;
        update.has_snr = true;
        update.snr = node_event->snr;
        update.has_rssi = true;
        update.rssi = node_event->rssi;
        update.has_protocol = true;
        update.protocol = node_event->protocol;
        update.has_role = true;
        update.role = node_event->role;
        update.has_hops_away = true;
        update.hops_away = node_event->hops_away;
        update.has_hw_model = true;
        update.hw_model = node_event->hw_model;
        update.has_channel = true;
        update.channel = node_event->channel;
        update.has_macaddr = node_event->has_macaddr;
        if (node_event->has_macaddr)
        {
            std::memcpy(update.macaddr, node_event->macaddr, sizeof(update.macaddr));
        }
        update.has_via_mqtt = true;
        update.via_mqtt = node_event->via_mqtt;
        update.has_is_ignored = true;
        update.is_ignored = node_event->is_ignored;
        update.has_public_key = true;
        update.public_key_present = node_event->has_public_key;
        update.has_key_manually_verified = true;
        update.key_manually_verified = node_event->key_manually_verified;
        update.has_device_metrics = node_event->has_device_metrics;
        if (node_event->has_device_metrics)
        {
            update.device_metrics = node_event->device_metrics;
        }
        app_facade.getContactService().applyNodeUpdate(node_event->node_id, update);
        delete event;
        return true;
    }
    case ::sys::EventType::NodeProtocolUpdate:
    {
        auto* protocol_event = static_cast<::sys::NodeProtocolUpdateEvent*>(event);
        app_facade.getContactService().updateNodeProtocol(protocol_event->node_id,
                                                          protocol_event->protocol,
                                                          protocol_event->event_timestamp != 0
                                                              ? protocol_event->event_timestamp
                                                              : protocol_event->timestamp);
        delete event;
        return true;
    }
    case ::sys::EventType::NodePositionUpdate:
    {
        auto* pos_event = static_cast<::sys::NodePositionUpdateEvent*>(event);
        ::chat::contacts::NodePosition pos{};
        pos.valid = true;
        pos.latitude_i = pos_event->latitude_i;
        pos.longitude_i = pos_event->longitude_i;
        pos.has_altitude = pos_event->has_altitude;
        pos.altitude = pos_event->altitude;
        pos.timestamp = pos_event->event_timestamp != 0 ? pos_event->event_timestamp : pos_event->timestamp;
        pos.precision_bits = pos_event->precision_bits;
        pos.pdop = pos_event->pdop;
        pos.hdop = pos_event->hdop;
        pos.vdop = pos_event->vdop;
        pos.gps_accuracy_mm = pos_event->gps_accuracy_mm;
        app_facade.getContactService().updateNodePosition(pos_event->node_id, pos);
        delete event;
        return true;
    }
    default:
        return false;
    }
}

void applyPairingEventFallback(::chat::contacts::ContactService& contact_service,
                               ::chat::NodeId self_node_id,
                               const ::team::TeamPairingEvent& event)
{
    ::team::ui::TeamUiSnapshot snapshot{};
    (void)::team::ui::team_ui_get_store().load(snapshot);

    if (event.has_team_id)
    {
        snapshot.team_id = event.team_id;
        snapshot.has_team_id = true;
        if (snapshot.team_name.empty())
        {
            snapshot.team_name = syntheticPairTeamName();
        }
    }
    if (event.has_team_name)
    {
        snapshot.team_name = event.team_name;
    }

    const bool pairing_active =
        event.state != ::team::TeamPairingState::Idle &&
        event.state != ::team::TeamPairingState::Completed &&
        event.state != ::team::TeamPairingState::Failed;
    snapshot.pending_join = pairing_active;
    snapshot.pending_join_started_s = pairing_active ? sys::epoch_seconds_now() : 0U;

    auto upsert_member = [&](::chat::NodeId node_id, bool leader)
    {
        const bool is_self = (node_id == 0 || node_id == self_node_id);
        const uint32_t stored_node_id = is_self ? 0U : node_id;
        auto it = std::find_if(snapshot.members.begin(), snapshot.members.end(),
                               [&](const ::team::ui::TeamMemberUi& member)
                               {
                                   return member.node_id == stored_node_id;
                               });

        if (it == snapshot.members.end())
        {
            ::team::ui::TeamMemberUi member{};
            member.node_id = stored_node_id;
            member.name = is_self ? "You" : contact_service.getContactName(node_id);
            if (member.name.empty())
            {
                char buffer[16] = {};
                std::snprintf(buffer, sizeof(buffer), "%08lX", static_cast<unsigned long>(node_id));
                member.name = buffer;
            }
            member.leader = leader;
            member.last_seen_s = sys::epoch_seconds_now();
            member.color_index = ::team::ui::team_color_index_from_node_id(is_self ? self_node_id : node_id);
            snapshot.members.push_back(std::move(member));
            return;
        }

        it->leader = leader;
        it->last_seen_s = sys::epoch_seconds_now();
        if (it->name.empty())
        {
            it->name = is_self ? "You" : contact_service.getContactName(node_id);
        }
    };

    if (event.role == ::team::TeamPairingRole::Leader)
    {
        snapshot.in_team = true;
        snapshot.kicked_out = false;
        snapshot.self_is_leader = true;
        upsert_member(self_node_id, true);
    }

    if (event.role == ::team::TeamPairingRole::Leader &&
        event.peer_id != 0 &&
        event.state == ::team::TeamPairingState::LeaderBeacon)
    {
        upsert_member(event.peer_id, false);
        snapshot.last_update_s = sys::epoch_seconds_now();
    }

    if (event.state == ::team::TeamPairingState::Completed)
    {
        snapshot.in_team = true;
        snapshot.kicked_out = false;
        snapshot.pending_join = false;
        snapshot.pending_join_started_s = 0;
        snapshot.self_is_leader = (event.role == ::team::TeamPairingRole::Leader);
        upsert_member(self_node_id, snapshot.self_is_leader);
    }

    if (event.state == ::team::TeamPairingState::Failed)
    {
        snapshot.pending_join = false;
        snapshot.pending_join_started_s = 0;
    }

    ::team::ui::team_ui_get_store().save(snapshot);
}

void applyKeyDistEventFallback(::chat::contacts::ContactService& contact_service,
                               ::team::TeamController* team_controller,
                               ::chat::NodeId self_node_id,
                               const ::team::TeamKeyDistEvent& event)
{
    ::team::ui::TeamUiSnapshot snapshot{};
    (void)::team::ui::team_ui_get_store().load(snapshot);

    snapshot.team_id = event.msg.team_id;
    snapshot.has_team_id = true;
    snapshot.in_team = true;
    snapshot.kicked_out = false;
    snapshot.pending_join = false;
    snapshot.pending_join_started_s = 0;
    snapshot.self_is_leader = false;
    snapshot.security_round = event.msg.key_id;
    snapshot.last_update_s = event.ctx.timestamp != 0 ? event.ctx.timestamp : sys::epoch_seconds_now();
    snapshot.team_name = syntheticPairTeamName();
    if (event.msg.channel_psk_len > 0)
    {
        snapshot.team_psk = event.msg.channel_psk;
        snapshot.has_team_psk = true;
    }

    auto ensure_member = [&](::chat::NodeId node_id, bool leader)
    {
        const bool is_self = (node_id == 0 || node_id == self_node_id);
        const uint32_t stored_node_id = is_self ? 0U : node_id;
        auto it = std::find_if(snapshot.members.begin(), snapshot.members.end(),
                               [&](const ::team::ui::TeamMemberUi& member)
                               {
                                   return member.node_id == stored_node_id;
                               });
        if (it == snapshot.members.end())
        {
            ::team::ui::TeamMemberUi member{};
            member.node_id = stored_node_id;
            member.name = is_self ? "You" : contact_service.getContactName(node_id);
            if (member.name.empty())
            {
                char buffer[16] = {};
                std::snprintf(buffer, sizeof(buffer), "%08lX", static_cast<unsigned long>(node_id));
                member.name = buffer;
            }
            member.leader = leader;
            member.last_seen_s = snapshot.last_update_s;
            member.color_index = ::team::ui::team_color_index_from_node_id(is_self ? self_node_id : node_id);
            snapshot.members.push_back(std::move(member));
            return;
        }
        it->leader = leader;
        it->last_seen_s = snapshot.last_update_s;
    };

    ensure_member(self_node_id, false);
    if (event.ctx.from != 0)
    {
        ensure_member(event.ctx.from, true);
    }

    if (snapshot.has_team_psk)
    {
        if (team_controller)
        {
            (void)team_controller->setKeysFromPsk(snapshot.team_id,
                                                  snapshot.security_round,
                                                  snapshot.team_psk.data(),
                                                  snapshot.team_psk.size());
        }
        ::team::ui::team_ui_save_keys_now(snapshot.team_id,
                                          snapshot.security_round,
                                          snapshot.team_psk);
    }

    ::team::ui::team_ui_get_store().save(snapshot);
}

bool handleLinuxUiEvent(MinimalLinuxAppFacade& app_facade, ::sys::Event* event)
{
    if (event == nullptr)
    {
        return true;
    }

    if (event->type == ::sys::EventType::TeamKick ||
        event->type == ::sys::EventType::TeamTransferLeader ||
        event->type == ::sys::EventType::TeamKeyDist ||
        event->type == ::sys::EventType::TeamStatus ||
        event->type == ::sys::EventType::TeamPosition ||
        event->type == ::sys::EventType::TeamWaypoint ||
        event->type == ::sys::EventType::TeamTrack ||
        event->type == ::sys::EventType::TeamChat ||
        event->type == ::sys::EventType::TeamPairing ||
        event->type == ::sys::EventType::TeamError ||
        event->type == ::sys::EventType::SystemTick)
    {
        if (s_team_ui_event_dispatcher)
        {
            s_team_ui_event_dispatcher(event);
        }
        else if (event->type == ::sys::EventType::TeamPairing)
        {
            applyPairingEventFallback(app_facade.getContactService(),
                                      app_facade.getSelfNodeId(),
                                      static_cast<::sys::TeamPairingEvent*>(event)->data);
        }
        else if (event->type == ::sys::EventType::TeamKeyDist)
        {
            applyKeyDistEventFallback(app_facade.getContactService(),
                                      app_facade.getTeamController(),
                                      app_facade.getSelfNodeId(),
                                      static_cast<::sys::TeamKeyDistEvent*>(event)->data);
        }
        delete event;
        return true;
    }

    if (::chat::ui::IChatUiRuntime* chat_ui_runtime = app_facade.getChatUiRuntime())
    {
        chat_ui_runtime->onChatEvent(event);
        return true;
    }

    delete event;
    return true;
}

} // namespace

void setTeamUiEventDispatcher(TeamUiEventDispatcher dispatcher)
{
    s_team_ui_event_dispatcher = dispatcher;
}

struct MinimalLinuxAppFacade::Implementation
{
    explicit Implementation(::chat::NodeId self_node_id)
        : node_blob_store(),
          contact_blob_store(),
          node_store(node_blob_store),
          contact_store(contact_blob_store),
          contact_service(node_store, contact_store),
          chat_model(),
          chat_store(),
          mesh_adapter(self_node_id),
          chat_service(chat_model, mesh_adapter, chat_store),
          chat_event_bridge(chat_service),
          team_runtime(),
          team_crypto(),
          team_event_sink(),
          pairing_event_sink(),
          pairing_transport(),
          pairing_service(team_runtime, pairing_event_sink, pairing_transport),
          track_source(),
          team_service(team_crypto, mesh_adapter, team_event_sink, team_runtime),
          team_controller(team_service),
          team_track_sampler(team_runtime, track_source)
    {
    }

    void ensureStarted(::chat::NodeId self_node_id)
    {
        if (started)
        {
            mesh_adapter.setSelfNodeId(self_node_id);
            return;
        }

        mesh_adapter.setSelfNodeId(self_node_id);
        node_store.setProtectedNodeChecker(
            [self_node_id](uint32_t node_id)
            {
                return node_id == self_node_id;
            });
        contact_service.begin();
        seedDemoWorld(self_node_id);
        started = true;
    }

    void clearContactAndNodeData()
    {
        contact_blob_store.clear();
        node_store.clear();
        contact_service.begin();
        demo_seeded = false;
    }

    void seedDemoWorld(::chat::NodeId self_node_id)
    {
        if (demo_seeded)
        {
            return;
        }

        const uint32_t now_secs = sys::epoch_seconds_now();
        const auto seeds = demoPeerSeeds();
        for (size_t i = 0; i < seeds.size(); ++i)
        {
            const auto& seed = seeds[i];
            contact_service.updateNodeInfo(seed.node_id,
                                           seed.short_name,
                                           seed.long_name,
                                           seed.snr,
                                           seed.rssi,
                                           now_secs > (30U * (i + 1U)) ? now_secs - (30U * static_cast<uint32_t>(i + 1U)) : now_secs,
                                           static_cast<uint8_t>(::chat::MeshProtocol::Meshtastic));

            ::chat::contacts::NodePosition pos{};
            pos.valid = true;
            pos.latitude_i = seed.lat_e7;
            pos.longitude_i = seed.lon_e7;
            pos.has_altitude = true;
            pos.altitude = 14 + static_cast<int32_t>(i * 3);
            pos.timestamp = now_secs;
            pos.hdop = 85;
            pos.gps_accuracy_mm = 2200;
            contact_service.updateNodePosition(seed.node_id, pos);

            if (seed.nickname && seed.nickname[0] != '\0')
            {
                (void)contact_service.addContact(seed.node_id, seed.nickname);
            }
            if (seed.ignored)
            {
                (void)contact_service.setNodeIgnored(seed.node_id, true);
            }
        }

        (void)contact_service.setNodeKeyManuallyVerified(kDemoAlphaNodeId, true);

        mesh_adapter.queueIncomingText({
            .channel = ::chat::ChannelId::PRIMARY,
            .from = kDemoAlphaNodeId,
            .to = self_node_id,
            .msg_id = 1001U,
            .timestamp = now_secs,
            .text = "Alice: local link ready.",
        });
        mesh_adapter.queueIncomingText({
            .channel = ::chat::ChannelId::PRIMARY,
            .from = kDemoBravoNodeId,
            .to = self_node_id,
            .msg_id = 1002U,
            .timestamp = now_secs,
            .text = "Bravo: route package synced.",
        });
        mesh_adapter.queueIncomingText({
            .channel = ::chat::ChannelId::PRIMARY,
            .from = kDemoBroadcastNodeId,
            .to = 0,
            .msg_id = 1003U,
            .timestamp = now_secs,
            .text = "Broadcast: Cardputer Zero local mesh online.",
        });
        demo_seeded = true;
    }

    LinuxNodeBlobStore node_blob_store;
    LinuxContactBlobStore contact_blob_store;
    ::chat::contacts::NodeStoreCore node_store;
    ::chat::contacts::ContactStoreCore contact_store;
    ::chat::contacts::ContactService contact_service;
    ::chat::ChatModel chat_model;
    ::chat::RamStore chat_store;
    LinuxLoopbackMeshAdapter mesh_adapter;
    ::chat::ChatService chat_service;
    LinuxChatEventBusBridge chat_event_bridge;
    LinuxTeamRuntime team_runtime;
    LinuxTeamCrypto team_crypto;
    LinuxTeamEventBusSink team_event_sink;
    LinuxTeamPairingEventQueue pairing_event_sink;
    LinuxLoopbackTeamPairingTransport pairing_transport;
    LinuxLoopbackTeamPairingService pairing_service;
    LinuxNullTrackSource track_source;
    ::team::TeamService team_service;
    ::team::TeamController team_controller;
    ::team::TeamTrackSampler team_track_sampler;
    bool started = false;
    bool demo_seeded = false;
};

MinimalLinuxAppFacade::MinimalLinuxAppFacade() = default;
MinimalLinuxAppFacade::~MinimalLinuxAppFacade() = default;

MinimalLinuxAppFacade::Implementation& MinimalLinuxAppFacade::impl()
{
    ensureServicesReady();
    return *impl_;
}

const MinimalLinuxAppFacade::Implementation& MinimalLinuxAppFacade::impl() const
{
    return *impl_;
}

bool MinimalLinuxAppFacade::initialize()
{
    if (initialized_)
    {
        if (!::app::hasAppFacade())
        {
            ::app::bindAppFacade(*this);
        }
        return true;
    }

    loadPersistedConfig();
    seedDefaultIdentity();
    (void)::sys::EventBus::init();
    ensureServicesReady();
    syncLocalIdentity();
    ::app::bindAppFacade(*this);
    initialized_ = true;
    return true;
}

void MinimalLinuxAppFacade::shutdown()
{
    if (::app::hasAppFacade())
    {
        ::app::unbindAppFacade();
    }
    initialized_ = false;
}

bool MinimalLinuxAppFacade::is_initialized() const noexcept
{
    return initialized_;
}

::app::AppConfig& MinimalLinuxAppFacade::getConfig()
{
    return config_;
}

const ::app::AppConfig& MinimalLinuxAppFacade::getConfig() const
{
    return config_;
}

void MinimalLinuxAppFacade::saveConfig()
{
    const PersistedConfigBlob blob{.magic = kConfigBlobMagic, .version = kConfigBlobVersion, .config = config_};
    (void)::platform::ui::settings_store::put_blob(
        kConfigNamespace, kConfigBlobKey, &blob, sizeof(blob));
}

void MinimalLinuxAppFacade::applyMeshConfig()
{
    ensureServicesReady();
    ::chat::MeshConfig mesh_config{};
    impl().mesh_adapter.applyConfig(mesh_config);
    impl().chat_service.setActiveProtocol(config_.mesh_protocol);
}

void MinimalLinuxAppFacade::applyUserInfo()
{
    seedDefaultIdentity();
    syncLocalIdentity();
}

void MinimalLinuxAppFacade::applyPositionConfig()
{
    platform::ui::gps::set_enabled(config_.gps_enabled);
    platform::ui::gps::set_collection_interval(config_.gps_interval_ms);
    platform::ui::gps::set_power_strategy(config_.gps_strategy);
    platform::ui::gps::set_gnss_config(config_.gps_mode, config_.gps_sat_mask);
    platform::ui::gps::set_external_nmea_config(config_.external_nmea_output_hz,
                                                config_.external_nmea_sentence_mask);
    platform::ui::gps::set_motion_idle_timeout(config_.motion_config.idle_timeout_ms);
    platform::ui::gps::set_motion_sensor_id(config_.motion_config.sensor_id);
}

void MinimalLinuxAppFacade::applyNetworkLimits()
{
}

void MinimalLinuxAppFacade::applyPrivacyConfig()
{
}

void MinimalLinuxAppFacade::applyChatDefaults()
{
    if (config_.chat_channel > 1U)
    {
        config_.chat_channel = 0U;
    }
}

::chat::MeshProtocol MinimalLinuxAppFacade::getMeshProtocol() const
{
    return config_.mesh_protocol;
}

void MinimalLinuxAppFacade::getEffectiveUserInfo(char* out_long,
                                                 std::size_t long_len,
                                                 char* out_short,
                                                 std::size_t short_len) const
{
    copy_bounded(out_long, long_len, config_.node_name);
    copy_bounded(out_short, short_len, config_.short_name);
}

bool MinimalLinuxAppFacade::switchMeshProtocol(::chat::MeshProtocol protocol, bool persist)
{
    if (!is_supported_protocol(protocol))
    {
        return false;
    }

    config_.mesh_protocol = protocol;
    if (impl_)
    {
        impl_->chat_service.setActiveProtocol(protocol);
    }
    if (persist)
    {
        saveConfig();
    }
    return true;
}

::chat::ChatService& MinimalLinuxAppFacade::getChatService()
{
    return impl().chat_service;
}

::chat::contacts::ContactService& MinimalLinuxAppFacade::getContactService()
{
    return impl().contact_service;
}

::chat::IMeshAdapter* MinimalLinuxAppFacade::getMeshAdapter()
{
    return &impl().mesh_adapter;
}

const ::chat::IMeshAdapter* MinimalLinuxAppFacade::getMeshAdapter() const
{
    return &impl().mesh_adapter;
}

::chat::NodeId MinimalLinuxAppFacade::getSelfNodeId() const
{
    return 0x435A0001U;
}

::team::TeamController* MinimalLinuxAppFacade::getTeamController()
{
    return &impl().team_controller;
}

::team::TeamPairingService* MinimalLinuxAppFacade::getTeamPairing()
{
    return &impl().pairing_service;
}

::team::TeamService* MinimalLinuxAppFacade::getTeamService()
{
    return &impl().team_service;
}

const ::team::TeamService* MinimalLinuxAppFacade::getTeamService() const
{
    return &impl().team_service;
}

::team::TeamTrackSampler* MinimalLinuxAppFacade::getTeamTrackSampler()
{
    return &impl().team_track_sampler;
}

void MinimalLinuxAppFacade::setTeamModeActive(bool active)
{
    (void)active;
}

void MinimalLinuxAppFacade::broadcastNodeInfo()
{
    syncLocalIdentity();
}

void MinimalLinuxAppFacade::clearNodeDb()
{
    ensureServicesReady();
    impl().clearContactAndNodeData();
    syncLocalIdentity();
}

void MinimalLinuxAppFacade::clearMessageDb()
{
    ensureServicesReady();
    impl().chat_model.clearAll();
    impl().chat_store.clearAll();
    ::team::ui::team_ui_get_store().clear();
}

::ble::BleManager* MinimalLinuxAppFacade::getBleManager()
{
    return nullptr;
}

const ::ble::BleManager* MinimalLinuxAppFacade::getBleManager() const
{
    return nullptr;
}

bool MinimalLinuxAppFacade::isBleEnabled() const
{
    return config_.ble_enabled;
}

void MinimalLinuxAppFacade::setBleEnabled(bool enabled)
{
    config_.ble_enabled = enabled;
    saveConfig();
}

void MinimalLinuxAppFacade::restartDevice()
{
    ::platform::ui::device::restart();
}

::chat::ui::IChatUiRuntime* MinimalLinuxAppFacade::getChatUiRuntime()
{
    return chat_ui_runtime_;
}

void MinimalLinuxAppFacade::setChatUiRuntime(::chat::ui::IChatUiRuntime* runtime)
{
    chat_ui_runtime_ = runtime;
}

::BoardBase* MinimalLinuxAppFacade::getBoard()
{
    return nullptr;
}

const ::BoardBase* MinimalLinuxAppFacade::getBoard() const
{
    return nullptr;
}

void MinimalLinuxAppFacade::updateCoreServices()
{
    if (!impl_)
    {
        return;
    }

    ::team::ui::TeamUiSnapshot snap;
    const bool has_team = ::team::ui::team_ui_get_store().load(snap) && snap.in_team;
    impl_->team_track_sampler.update(&impl_->team_controller, has_team);
}

void MinimalLinuxAppFacade::tickEventRuntime()
{
    if (!impl_)
    {
        return;
    }

    impl_->pairing_service.update();
    ::sys::EventBus::publish(new ::sys::Event(::sys::EventType::SystemTick), 0);
}

void MinimalLinuxAppFacade::dispatchPendingEvents(std::size_t max_events)
{
    if (!impl_)
    {
        return;
    }

    impl_->mesh_adapter.tick();
    impl_->chat_service.processIncoming();
    impl_->team_service.processIncoming();

    while (true)
    {
        ::chat::MessageId msg_id = 0;
        bool ok = false;
        if (!impl_->mesh_adapter.takePendingSendResult(msg_id, ok))
        {
            break;
        }
        impl_->chat_service.handleSendResult(msg_id, ok);
        ::sys::EventBus::publish(new ::sys::ChatSendResultEvent(msg_id, ok), 0);
    }

    std::size_t processed = 0;
    ::sys::Event* event = nullptr;
    while (processed < max_events && ::sys::EventBus::subscribe(&event, 0))
    {
        if (event == nullptr)
        {
            continue;
        }
        ++processed;

        if (dispatchCoreEvent(*this, event))
        {
            continue;
        }

        if (handleLinuxUiEvent(*this, event))
        {
            continue;
        }

        delete event;
    }
}

void MinimalLinuxAppFacade::loadPersistedConfig()
{
    std::vector<uint8_t> blob_bytes;
    if (!::platform::ui::settings_store::get_blob(kConfigNamespace, kConfigBlobKey, blob_bytes) ||
        blob_bytes.size() != sizeof(PersistedConfigBlob))
    {
        config_ = ::app::AppConfig{};
        return;
    }

    PersistedConfigBlob blob{};
    std::memcpy(&blob, blob_bytes.data(), sizeof(blob));
    if (blob.magic != kConfigBlobMagic || blob.version != kConfigBlobVersion ||
        !is_supported_protocol(blob.config.mesh_protocol))
    {
        config_ = ::app::AppConfig{};
        return;
    }

    config_ = blob.config;
}

void MinimalLinuxAppFacade::seedDefaultIdentity()
{
    if (config_.node_name[0] == '\0')
    {
        copy_bounded(config_.node_name, sizeof(config_.node_name), "Cardputer Zero");
    }
    if (config_.short_name[0] == '\0')
    {
        copy_bounded(config_.short_name, sizeof(config_.short_name), "CZ");
    }
    applyChatDefaults();
}

void MinimalLinuxAppFacade::syncLocalIdentity()
{
    ensureServicesReady();
    impl().mesh_adapter.setUserInfo(config_.node_name, config_.short_name);
    impl().chat_service.setActiveProtocol(config_.mesh_protocol);
    impl().contact_service.updateNodeInfo(getSelfNodeId(),
                                          config_.short_name,
                                          config_.node_name,
                                          0.0f,
                                          0.0f,
                                          sys::epoch_seconds_now(),
                                          static_cast<uint8_t>(config_.mesh_protocol));
    ::chat::contacts::NodeUpdate self_update{};
    self_update.has_is_ignored = true;
    self_update.is_ignored = true;
    impl().contact_service.applyNodeUpdate(getSelfNodeId(), self_update);
}

void MinimalLinuxAppFacade::ensureServicesReady()
{
    if (!impl_)
    {
        impl_ = std::make_unique<Implementation>(getSelfNodeId());
    }
    impl_->ensureStarted(getSelfNodeId());
}

} // namespace trailmate::cardputer_zero::linux_ui
