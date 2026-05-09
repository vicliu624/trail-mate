#include "app/linux_demo_world.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "chat/domain/chat_model.h"
#include "chat/ports/i_mesh_adapter.h"
#include "sys/clock.h"
#include "team/protocol/team_pairing_wire.h"

namespace trailmate::cardputer_zero::linux_ui
{
namespace
{

// ---------------------------------------------------------------------------
// Peer seeds
// ---------------------------------------------------------------------------

constexpr ::chat::NodeId kAlpha = 0x435A1001U;
constexpr ::chat::NodeId kBravo = 0x435A1002U;
constexpr ::chat::NodeId kScout = 0x435A1003U;
constexpr ::chat::NodeId kNearby = 0x435A1004U;

constexpr uint32_t kAutoReplyDelayMs = 700U;

std::string makeAutoReplyTextImpl(::chat::NodeId peer, const std::string& text)
{
    const std::string trimmed = text.substr(0, std::min<std::size_t>(text.size(), 28U));
    switch (peer)
    {
    case kAlpha:
        return "Alice copied: " + trimmed;
    case kBravo:
        return "Bravo link OK: " + trimmed;
    case kScout:
        return "Scout received your ping.";
    default:
        return "Peer ack: " + trimmed;
    }
}

// ---------------------------------------------------------------------------
// Loopback mesh adapter
// ---------------------------------------------------------------------------

class LoopbackMeshAdapter final : public ::chat::IMeshAdapter
{
  public:
    explicit LoopbackMeshAdapter(::chat::NodeId self_node_id)
        : self_node_id_(self_node_id) {}

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
        if (text.empty()) return false;

        const ::chat::MessageId msg_id =
            (forced_msg_id != 0) ? forced_msg_id : nextMessageId();
        if (out_msg_id) *out_msg_id = msg_id;

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
            scheduleIncomingText(
                {.channel = channel, .from = peer, .to = self_node_id_, .msg_id = nextMessageId(), .timestamp = sys::epoch_seconds_now(), .text = makeAutoReplyTextImpl(peer, text)},
                kAutoReplyDelayMs);
        }
        else
        {
            scheduleIncomingText(
                {.channel = channel, .from = kAlpha, .to = 0, .msg_id = nextMessageId(), .timestamp = sys::epoch_seconds_now(), .text = "Broadcast heard: " + text.substr(0, std::min<std::size_t>(text.size(), 32U))},
                kAutoReplyDelayMs + 120U);
        }

        return true;
    }

    bool pollIncomingText(::chat::MeshIncomingText* out) override
    {
        if (out == nullptr || incoming_texts_.empty()) return false;
        *out = incoming_texts_.front();
        incoming_texts_.erase(incoming_texts_.begin());
        return true;
    }

    bool sendAppData(::chat::ChannelId, uint32_t, const uint8_t*, size_t,
                     ::chat::NodeId, bool, ::chat::MessageId, bool) override
    {
        return true;
    }

    bool pollIncomingData(::chat::MeshIncomingData* out) override
    {
        if (out == nullptr || incoming_data_.empty()) return false;
        *out = incoming_data_.front();
        incoming_data_.erase(incoming_data_.begin());
        return true;
    }

    bool requestNodeInfo(::chat::NodeId, bool) override { return true; }
    bool startKeyVerification(::chat::NodeId) override { return true; }

    bool submitKeyVerificationNumber(::chat::NodeId, uint64_t, uint32_t) override
    {
        return true;
    }

    ::chat::NodeId getNodeId() const override { return self_node_id_; }
    bool isPkiReady() const override { return true; }
    bool hasPkiKey(::chat::NodeId dest) const override { return dest != 0; }

    bool triggerDiscoveryAction(::chat::MeshDiscoveryAction) override
    {
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

    bool isReady() const override { return true; }

    bool pollIncomingRawPacket(uint8_t*, size_t&, size_t) override
    {
        return false;
    }

    // Additional helpers used by the facade.
    void setSelfNodeId(::chat::NodeId id) { self_node_id_ = id; }
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
        const uint32_t now = sys::millis_now();
        while (!pending_incoming_texts_.empty())
        {
            const auto& p = pending_incoming_texts_.front();
            if (p.due_ms > now) break;
            incoming_texts_.push_back(p.message);
            pending_incoming_texts_.pop_front();
        }
    }

    bool takePendingSendResult(::chat::MessageId& out_msg_id, bool& out_ok)
    {
        if (pending_send_results_.empty()) return false;
        const auto r = pending_send_results_.front();
        pending_send_results_.erase(pending_send_results_.begin());
        out_msg_id = r.first;
        out_ok = r.second;
        return true;
    }

  private:
    struct Delayed
    {
        uint32_t due_ms = 0;
        ::chat::MeshIncomingText message{};
    };

    ::chat::MessageId nextMessageId()
    {
        if (next_message_id_ == 0) next_message_id_ = 1;
        return next_message_id_++;
    }

    void scheduleIncomingText(::chat::MeshIncomingText msg, uint32_t delay_ms)
    {
        msg.timestamp = sys::epoch_seconds_now();
        pending_incoming_texts_.push_back(
            {.due_ms = sys::millis_now() + delay_ms, .message = std::move(msg)});
    }

    ::chat::NodeId self_node_id_ = 0;
    ::chat::MessageId next_message_id_ = 1;
    ::chat::MeshConfig config_{};
    std::string long_name_{};
    std::string short_name_{};
    std::vector<::chat::MeshIncomingText> incoming_texts_{};
    std::vector<::chat::MeshIncomingData> incoming_data_{};
    std::vector<std::pair<::chat::MessageId, bool>> pending_send_results_{};
    std::deque<Delayed> pending_incoming_texts_{};
};

} // namespace

// ---------------------------------------------------------------------------
// DemoWorld
// ---------------------------------------------------------------------------

struct DemoWorld::Impl
{
};

DemoWorld::DemoWorld() : impl_(std::make_unique<Impl>()) {}
DemoWorld::~DemoWorld() = default;

std::array<DemoPeerSeed, 4> DemoWorld::peerSeeds()
{
    return {{
        {kAlpha, "ALFA", "Alice Local", "Alice", false, 11.2f, -72.0f, 311214000, 1214737000},
        {kBravo, "BRAV", "Bravo Pager", "Bravo", false, 8.6f, -79.0f, 311218500, 1214749000},
        {kScout, "SCOT", "Scout Relay", nullptr, true, 4.1f, -94.0f, 311227000, 1214762000},
        {kNearby, "NBY1", "Nearby Relay", nullptr, false, 6.8f, -88.0f, 311231200, 1214756000},
    }};
}

std::unique_ptr<::chat::IMeshAdapter>
DemoWorld::createLoopbackMesh(::chat::NodeId self_node_id)
{
    return std::make_unique<LoopbackMeshAdapter>(self_node_id);
}

std::string DemoWorld::autoReplyText(::chat::NodeId peer,
                                     const std::string& received_text)
{
    return makeAutoReplyTextImpl(peer, received_text);
}

::team::TeamId DemoWorld::syntheticPairTeamId()
{
    ::team::TeamId id{};
    const std::array<uint8_t, 8> bytes{{'C', 'Z', 'T', 'E', 'A', 'M', '0', '1'}};
    for (size_t i = 0; i < id.size() && i < bytes.size(); ++i) id[i] = bytes[i];
    return id;
}

std::array<uint8_t, 32> DemoWorld::syntheticPairPsk()
{
    std::array<uint8_t, 32> psk{};
    for (size_t i = 0; i < psk.size(); ++i)
        psk[i] = static_cast<uint8_t>(0x30U + (i * 7U + 3U) % 0x4FU);
    return psk;
}

const char* DemoWorld::syntheticPairTeamName() { return "Field Team"; }

} // namespace trailmate::cardputer_zero::linux_ui
