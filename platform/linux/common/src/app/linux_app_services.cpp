#include "app/linux_app_services.h"
#include "app/linux_app_config_blob.h"
#include "app/linux_phone_runtime.h"
#include "app/linux_tms_store.h"
#include "platform/linux/application_lifecycle.h"
#include "platform/linux/identity_restore_bridge.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "platform/linux/runtime_paths.h"
#include "hostlink/identity_restore_request.h"
#include "hostlink/radio_identity_backup.h"
#include "platform/linux/mesh/linux_radio_identity_store.h"
#include "platform/linux/call_audio_service.h"
#include "platform/linux/tms_import_bridge.h"
#include "platform/linux/tms_snapshot_bridge.h"
#include "platform/ui/hostlink_runtime.h"
#include "platform/ui/reticulum_call_runtime.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

#include "chat/domain/chat_model.h"
#include "chat/linux_mesh_worker.h"
#include "chat/linux_raw_lora_mesh_adapter.h"
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
#include "platform/linux/runtime_paths.h"
#include "platform/linux/vmp_radio.h"
#include "platform/linux/voice_contacts.h"
#include "platform/linux/voice_service.h"
#endif
#include "chat/domain/reticulum_network_policy.h"
#include "chat/linux_reticulum_network_store.h"
#include "chat/linux_sqlite_chat_store.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/linux/node_identity.h"
#include "platform/linux/radio_power.h"
#include "platform/linux/resource_pack_tasks.h"
#include "platform/linux/reticulum_network_config_binding.h"
#include "platform/linux/route_image_tasks.h"
#include "platform/linux/settings_reset_binding.h"
#include "platform/linux/settings_reset_storage.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/network_runtime.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/reticulum_group_config_runtime.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include "sys/clock.h"
#include "sys/event_bus.h"
#include "team/ports/i_team_crypto.h"
#include "team/ports/i_team_event_sink.h"
#include "team/ports/i_team_pairing_event_sink.h"
#include "team/ports/i_team_pairing_transport.h"
#include "team/ports/i_team_runtime.h"
#include "team/ports/i_team_track_source.h"
#include "team/protocol/team_pairing_wire.h"
#include "team/protocol/team_position.h"
#include "team/usecase/team_controller.h"
#include "team/usecase/team_pairing_coordinator.h"
#include "team/usecase/team_pairing_service.h"
#include "team/usecase/team_service.h"
#include "team/usecase/team_track_sampler.h"

namespace trailmate::linux_app
{
namespace
{

constexpr const char* kConfigNamespace = ::platform::linux_runtime::settings_storage::kAppNamespace;
constexpr const char* kConfigBlobKey = ::platform::linux_runtime::settings_storage::kAppConfigKey;
constexpr uint32_t kConfigBlobMagic = LinuxAppConfigBlob::kMagic;
constexpr uint32_t kConfigBlobVersion = LinuxAppConfigBlob::kVersion;

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

using PersistedConfigBlob = LinuxAppConfigBlob;

static_assert(std::is_trivially_copyable_v<::app::AppConfig>,
              "LinuxAppServices persists AppConfig as an opaque blob.");

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
    switch (::chat::infra::normalizeMeshProtocol(protocol))
    {
    case ::chat::MeshProtocol::Meshtastic:
    case ::chat::MeshProtocol::MeshCore:
    case ::chat::MeshProtocol::Reticulum:
        return true;
    default:
        return false;
    }
}

bool raw_lora_enabled_for_mode(::platform::linux_runtime::LinuxRuntimeMode mode)
{
    if (const char* value = std::getenv("TRAIL_MATE_LORA_DISABLE"))
    {
        if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
            std::strcmp(value, "TRUE") == 0)
        {
            return false;
        }
    }

    if (const char* value = std::getenv("TRAIL_MATE_LORA_ADAPTER"))
    {
        if (std::strcmp(value, "raw") == 0 ||
            std::strcmp(value, "sx1262") == 0)
        {
            return true;
        }
        if (std::strcmp(value, "noop") == 0 ||
            std::strcmp(value, "off") == 0)
        {
            return false;
        }
    }

    return mode == ::platform::linux_runtime::LinuxRuntimeMode::DeviceRealMesh ||
           LinuxRawLoraMeshAdapter::hardwareCandidatePresent();
}

bool desktop_notifications_enabled()
{
    const char* value = std::getenv("TRAIL_MATE_DESKTOP_NOTIFICATIONS");
    if (value == nullptr || value[0] == '\0')
    {
        return false;
    }
    return std::strcmp(value, "freedesktop") == 0 ||
           std::strcmp(value, "1") == 0 ||
           std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "TRUE") == 0 ||
           std::strcmp(value, "yes") == 0 ||
           std::strcmp(value, "YES") == 0;
}

std::string notification_text(const std::string& text, std::size_t max_len)
{
    std::string out;
    out.reserve(std::min(text.size(), max_len));
    for (const char ch : text)
    {
        const auto byte = static_cast<unsigned char>(ch);
        if (ch == '\r' || ch == '\n' || ch == '\t')
        {
            if (!out.empty() && out.back() != ' ')
            {
                out.push_back(' ');
            }
        }
        else if (byte >= 0x20U)
        {
            out.push_back(ch);
        }
        if (out.size() >= max_len)
        {
            if (max_len >= 3U)
            {
                out.resize(max_len - 3U);
                out += "...";
            }
            break;
        }
    }
    return out.empty() ? "Message received" : out;
}

std::string chat_notification_summary(const ::chat::ChatMessage& msg)
{
    const char* scope = msg.peer == 0 ? "Broadcast" : "Direct";
    if (msg.from == 0)
    {
        return std::string("Trail Mate ") + scope;
    }

    char node[32] = {};
    std::snprintf(node,
                  sizeof(node),
                  "%08lX",
                  static_cast<unsigned long>(msg.from));
    return std::string(scope) + " from " + node;
}

void send_desktop_notification_async(std::string summary, std::string body)
{
#if defined(__linux__)
    std::thread([summary = std::move(summary), body = std::move(body)]() mutable
                {
        const char* urgency = "normal";
        const char* category = "im.received";
        char* const argv[] = {
            const_cast<char*>("notify-send"),
            const_cast<char*>("-a"),
            const_cast<char*>("Trail Mate"),
            const_cast<char*>("-u"),
            const_cast<char*>(urgency),
            const_cast<char*>("-c"),
            const_cast<char*>(category),
            const_cast<char*>(summary.c_str()),
            const_cast<char*>(body.c_str()),
            nullptr,
        };

        pid_t pid = 0;
        const int rc = posix_spawnp(&pid, "notify-send", nullptr, nullptr, argv, environ);
        if (rc != 0)
        {
            return;
        }
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        {
        } })
        .detach();
#else
    (void)summary;
    (void)body;
#endif
}

const ::chat::MeshConfig& mesh_config_for_protocol(
    const ::app::AppConfig& config)
{
    switch (::chat::infra::normalizeMeshProtocol(config.mesh_protocol))
    {
    case ::chat::MeshProtocol::MeshCore:
        return config.meshcore_config;
    case ::chat::MeshProtocol::Reticulum:
        return config.reticulumConfig();
    case ::chat::MeshProtocol::Meshtastic:
    default:
        return config.meshtastic_config;
    }
}

bool sync_reticulum_group_config(::app::AppConfig& config)
{
    bool reset_completed = false;
    const auto marker = ::platform::ui::settings_store::read_bool_into(kConfigNamespace,
                                                                       ::platform::linux_runtime::settings_storage::kResetMarker, &reset_completed);
    if (marker != ::platform::ui::settings_store::ScalarReadResult::Missing &&
        (marker != ::platform::ui::settings_store::ScalarReadResult::Ok || reset_completed)) return false;
    // Startup migration only. A modern application snapshot owns these groups;
    // reloading the legacy file during apply would discard live edits.
    const auto status = ::platform::ui::reticulum_groups::load(
        config.reticulumConfig().reticulum_groups,
        ::chat::kReticulumGroupDestinationMaxCount);
    std::printf("[RTGroupConfig] sync sd=%u loaded=%u file=%u message=%s detail=%s\n",
                status.sd_present ? 1U : 0U,
                status.loaded ? 1U : 0U,
                status.file_present ? 1U : 0U,
                status.message,
                status.detail);
    return status.loaded;
}

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
            .supports_node_info_query = true,
            .supports_node_info_reply = true,
            .supports_node_info_reannounce = true,
            .supports_position_request = true,
            .supports_position_reply = true,
            .supports_trace_route_request = true,
            .supports_trace_route_reply = true,
            .supports_protocol_app_response = true,
            .supports_protocol_ack_tracking = true,
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
                                     .hop_limit = 0xFF,
                                     .encrypted = false,
                                     .rx_meta = {},
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
                                     .hop_limit = 0xFF,
                                     .encrypted = false,
                                     .rx_meta = {},
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
        ::platform::ui::device::play_message_tone();
        if (desktop_notifications_enabled())
        {
            send_desktop_notification_async(
                chat_notification_summary(msg),
                notification_text(msg.text, 160U));
        }
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

    void onTeamKeyRequest(const ::team::TeamKeyRequestEvent& event) override
    {
        ::sys::EventBus::publish(new ::sys::TeamKeyRequestEvent(event), 0);
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

class LinuxGpsTrackSource final : public ::team::ITeamTrackSource
{
  public:
    bool readTrackPoint(::team::proto::TeamTrackPoint* out_point) override
    {
        if (out_point == nullptr)
        {
            return false;
        }

        const auto gps = ::platform::ui::gps::get_data();
        if (!gps.valid)
        {
            return false;
        }

        out_point->lat_e7 = static_cast<int32_t>(std::llround(gps.lat * 10000000.0));
        out_point->lon_e7 = static_cast<int32_t>(std::llround(gps.lng * 10000000.0));
        return true;
    }
};

bool dispatchCoreEvent(LinuxAppServices& services, ::sys::Event* event)
{
    if (event == nullptr)
    {
        return true;
    }

    switch (event->type)
    {
    case ::sys::EventType::ChatSendResult:
    {
        const auto& result = *static_cast<::sys::ChatSendResultEvent*>(event);
        services.getChatService().handleSendResultForProtocol(result.msg_id,
                                                              result.has_protocol ? result.protocol : services.getMeshProtocol(),
                                                              result.status, 0, result.failure);
        // Continue to the UI notification path after the durable core update.
        return false;
    }
    case ::sys::EventType::NodeInfoUpdate:
    {
        auto* node_event = static_cast<::sys::NodeInfoUpdateEvent*>(event);
        ::chat::contacts::NodeUpdate update{};
        update.short_name = node_event->short_name;
        update.long_name = node_event->long_name;
        update.has_last_seen = true;
        update.last_seen = node_event->event_timestamp != 0 ? node_event->event_timestamp : node_event->timestamp;
        update.has_snr = !node_event->via_mqtt && std::isfinite(node_event->snr);
        update.snr = node_event->snr;
        update.has_rssi = !node_event->via_mqtt && std::isfinite(node_event->rssi);
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
        // Radio observations do not own the user's local ignore policy.
        // Local administration uses ContactService::setNodeIgnored directly.
        update.has_is_ignored = false;
        update.has_public_key = node_event->has_public_key_state;
        update.public_key_present = node_event->has_public_key;
        update.has_key_manually_verified = node_event->has_key_manually_verified_state;
        update.key_manually_verified = node_event->key_manually_verified;
        update.reticulum_identity = node_event->reticulum_identity;
        update.has_device_metrics = node_event->has_device_metrics;
        if (node_event->has_device_metrics)
        {
            update.device_metrics = node_event->device_metrics;
        }
        services.getContactService().applyNodeUpdate(node_event->node_id, update);
        delete event;
        return true;
    }
    case ::sys::EventType::NodeProtocolUpdate:
    {
        auto* protocol_event = static_cast<::sys::NodeProtocolUpdateEvent*>(event);
        services.getContactService().updateNodeProtocol(protocol_event->node_id,
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
        services.getContactService().updateNodePosition(pos_event->node_id, pos);
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
    (void)::team::ui::team_ui_snapshot_store().load(snapshot);

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

    ::team::ui::team_ui_snapshot_store().save(snapshot);
}

void applyKeyDistEventFallback(::chat::contacts::ContactService& contact_service,
                               ::team::TeamController* team_controller,
                               ::chat::NodeId self_node_id,
                               const ::team::TeamKeyDistEvent& event)
{
    ::team::ui::TeamUiSnapshot snapshot{};
    (void)::team::ui::team_ui_snapshot_store().load(snapshot);

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

    ::team::ui::team_ui_snapshot_store().save(snapshot);
}

bool isTeamUiEvent(const ::sys::Event& event)
{
    return event.type == ::sys::EventType::TeamKick ||
           event.type == ::sys::EventType::TeamTransferLeader ||
           event.type == ::sys::EventType::TeamKeyDist ||
           event.type == ::sys::EventType::TeamKeyRequest ||
           event.type == ::sys::EventType::TeamStatus ||
           event.type == ::sys::EventType::TeamPosition ||
           event.type == ::sys::EventType::TeamWaypoint ||
           event.type == ::sys::EventType::TeamTrack ||
           event.type == ::sys::EventType::TeamChat ||
           event.type == ::sys::EventType::TeamPairing ||
           event.type == ::sys::EventType::TeamError ||
           event.type == ::sys::EventType::SystemTick;
}

bool handleLinuxUiEvent(LinuxAppServices& services, ::sys::Event* event)
{
    if (event == nullptr)
    {
        return true;
    }

    if (services.dispatchUiEvent(event))
    {
        return true;
    }

    if (isTeamUiEvent(*event))
    {
        if (event->type == ::sys::EventType::TeamPairing)
        {
            applyPairingEventFallback(services.getContactService(),
                                      services.getSelfNodeId(),
                                      static_cast<::sys::TeamPairingEvent*>(event)->data);
        }
        else if (event->type == ::sys::EventType::TeamKeyDist)
        {
            applyKeyDistEventFallback(services.getContactService(),
                                      services.getTeamController(),
                                      services.getSelfNodeId(),
                                      static_cast<::sys::TeamKeyDistEvent*>(event)->data);
        }
        delete event;
        return true;
    }

    delete event;
    return true;
}

} // namespace

struct LinuxAppServices::Implementation
{
    explicit Implementation(LinuxAppServicesOptions options_in, ::chat::NodeId self_node_id)
        : options(options_in),
          demo_world_enabled(
              ::platform::linux_runtime::demo_world_enabled(options.runtime_mode)),
          mesh_peer_directory(
              ::platform::ui::reticulum_directory::mesh_peer_directory()),
          contact_service(mesh_peer_directory),
          chat_model(),
          chat_store(),
          raw_lora_enabled(raw_lora_enabled_for_mode(options.runtime_mode)),
          raw_lora_mesh_adapter(self_node_id, options.packet_radio, options.radio_power, raw_lora_enabled),
          loopback_mesh_adapter(self_node_id),
          mesh_adapter(demo_world_enabled
                           ? static_cast<::chat::IMeshAdapter&>(
                                 loopback_mesh_adapter)
                           : static_cast<::chat::IMeshAdapter&>(raw_lora_mesh_adapter)),
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
            raw_lora_mesh_adapter.setSelfNodeId(self_node_id);
            loopback_mesh_adapter.setSelfNodeId(self_node_id);
            return;
        }

        raw_lora_mesh_adapter.setSelfNodeId(self_node_id);
        loopback_mesh_adapter.setSelfNodeId(self_node_id);
        if (!demo_world_enabled) (void)raw_lora_mesh_adapter.initialize();
        contact_service.begin();
        ::platform::ui::reticulum_directory::bind_mesh_peer_directory(
            &mesh_peer_directory);
        if (demo_world_enabled)
        {
            seedDemoWorld(self_node_id);
        }
        started = true;
    }

    void clearContactAndNodeData()
    {
        (void)mesh_peer_directory.clearProtocol(::chat::MeshProtocol::Meshtastic);
        (void)mesh_peer_directory.clearProtocol(::chat::MeshProtocol::MeshCore);
        (void)mesh_peer_directory.clearProtocol(::chat::MeshProtocol::RNode);
        (void)mesh_peer_directory.clearProtocol(::chat::MeshProtocol::Reticulum);
        contact_service.begin();
        demo_seeded = false;
    }

    void seedDemoWorld(::chat::NodeId self_node_id)
    {
        if (!demo_world_enabled || demo_seeded)
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

        loopback_mesh_adapter.queueIncomingText({
            .channel = ::chat::ChannelId::PRIMARY,
            .from = kDemoAlphaNodeId,
            .to = self_node_id,
            .msg_id = 1001U,
            .timestamp = now_secs,
            .text = "Alice: local link ready.",
            .hop_limit = 0xFF,
            .encrypted = false,
            .rx_meta = {},
        });
        loopback_mesh_adapter.queueIncomingText({
            .channel = ::chat::ChannelId::PRIMARY,
            .from = kDemoBravoNodeId,
            .to = self_node_id,
            .msg_id = 1002U,
            .timestamp = now_secs,
            .text = "Bravo: route package synced.",
            .hop_limit = 0xFF,
            .encrypted = false,
            .rx_meta = {},
        });
        loopback_mesh_adapter.queueIncomingText({
            .channel = ::chat::ChannelId::PRIMARY,
            .from = kDemoBroadcastNodeId,
            .to = 0,
            .msg_id = 1003U,
            .timestamp = now_secs,
            .text = options.demo_broadcast_text ? options.demo_broadcast_text
                                                : "Broadcast: Linux local mesh online.",
            .hop_limit = 0xFF,
            .encrypted = false,
            .rx_meta = {},
        });
        demo_seeded = true;
    }

    void tickMeshAdapter()
    {
        if (demo_world_enabled)
        {
            loopback_mesh_adapter.tick();
            return;
        }
        raw_lora_mesh_adapter.tick();
    }

    bool takePendingSendResult(::chat::MessageId& out_msg_id, bool& out_ok)
    {
        if (demo_world_enabled)
        {
            return loopback_mesh_adapter.takePendingSendResult(out_msg_id,
                                                               out_ok);
        }
        // Worker outcomes carry their own protocol and are drained separately.
        return false;
    }

    LinuxAppServicesOptions options{};
    bool demo_world_enabled = false;
    ::chat::IMeshPeerDirectory& mesh_peer_directory;
    ::chat::contacts::ContactService contact_service;
    ::chat::ChatModel chat_model;
    LinuxSqliteChatStore chat_store;
    bool raw_lora_enabled = false;
    LinuxMeshWorker raw_lora_mesh_adapter;
    LinuxLoopbackMeshAdapter loopback_mesh_adapter;
    ::chat::IMeshAdapter& mesh_adapter;
    ::chat::ChatService chat_service;
    LinuxChatEventBusBridge chat_event_bridge;
    LinuxTeamRuntime team_runtime;
    LinuxTeamCrypto team_crypto;
    LinuxTeamEventBusSink team_event_sink;
    LinuxTeamPairingEventQueue pairing_event_sink;
    LinuxLoopbackTeamPairingTransport pairing_transport;
    LinuxLoopbackTeamPairingService pairing_service;
    LinuxGpsTrackSource track_source;
    ::team::TeamService team_service;
    ::team::TeamController team_controller;
    ::team::TeamTrackSampler team_track_sampler;
    bool started = false;
    bool demo_seeded = false;
    void syncPeerPublicKeys(::chat::MeshProtocol protocol)
    {
        if (!raw_lora_enabled || demo_world_enabled) return;
        const auto snapshot = raw_lora_mesh_adapter.peerPublicKeys();
        if (snapshot.protocol != protocol) return;
        for (const auto& key : snapshot.keys)
        {
            ::chat::MeshPeerRecord previous;
            const bool found = mesh_peer_directory.findByNodeId(protocol, key.node_id.value, previous).succeeded();
            const uint8_t* existing = protocol == ::chat::MeshProtocol::Meshtastic ? previous.meshtastic.public_key : previous.meshcore.public_key;
            const bool has_key = protocol == ::chat::MeshProtocol::Meshtastic ? previous.meshtastic.has_public_key : previous.meshcore.has_public_key;
            if (found && has_key && std::equal(key.public_key, key.public_key + 32, existing)) continue;
            const bool pinned = protocol == ::chat::MeshProtocol::Meshtastic ? previous.meshtastic.key_manually_verified : previous.meshcore.public_key_verified;
            if (found && has_key && pinned) continue;
            ::chat::MeshPeerRecord update;
            update.valid = true;
            update.source = ::chat::MeshPeerSource::RuntimeRx;
            update.identity = found ? previous.identity : ::chat::makeMeshPeerNodeIdentity(protocol, key.node_id.value);
            if (protocol == ::chat::MeshProtocol::Meshtastic)
            {
                update.meshtastic.has_public_key = true;
                std::copy_n(key.public_key, 32, update.meshtastic.public_key);
            }
            else if (protocol == ::chat::MeshProtocol::MeshCore)
            {
                update.meshcore.has_public_key = true;
                update.meshcore.node_id_hint = key.node_id.value;
                std::copy_n(key.public_key, 32, update.meshcore.public_key);
            }
            else continue;
            (void)contact_service.recordPeer(update);
        }
    }
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
    std::array<std::shared_ptr<::platform::linux_runtime::LinuxVoiceContacts>, 2> voice_contacts;
    std::unique_ptr<::platform::linux_runtime::IPacketRadio> voice_network_only_radio;
    std::unique_ptr<::platform::linux_runtime::NativeVoiceService> voice;
#endif
    std::unique_ptr<::platform::linux_runtime::NativeCallAudioBinding> call_audio;
};

LinuxAppServices::LinuxAppServices(LinuxAppServicesOptions options)
    : options_(options)
{
    // Phone BLE is intentionally outside the current Linux support scope.
    // Ignore legacy composition flags and persisted requests until it returns.
    options_.ble_supported = false;
    options_.phone_pairing_presenter = {};
}

LinuxAppServices::~LinuxAppServices()
{
    shutdown();
}

LinuxAppServices::Implementation& LinuxAppServices::impl()
{
    ensureServicesReady();
    return *impl_;
}

const LinuxAppServices::Implementation& LinuxAppServices::impl() const
{
    return *impl_;
}

bool LinuxAppServices::initialize()
{
    if (initialized_)
    {
        return true;
    }

    (void)::platform::ui::network::status(); // Warm the cached platform source before UI actions.
    if (!ensureNodeIdentity()) return false;
    if (!loadPersistedConfig()) return false;
    using NetworkStore = ::platform::linux_runtime::LinuxReticulumNetworkStore;
    const auto network_loaded = NetworkStore::load(reticulum_network_config_);
    if (network_loaded == NetworkStore::Result::Unavailable) return false;
    has_reticulum_network_config_ = network_loaded != NetworkStore::Result::Missing;
    if (network_loaded == NetworkStore::Result::Invalid)
    {
        reticulum_network_config_ = ::chat::reticulum::ReticulumNetworkConfig{};
        std::fprintf(stderr, "[Reticulum][Config] Invalid saved network configuration; network interfaces remain disabled\n");
    }
    seedDefaultIdentity();
    config_persistence_runtime_.initialize();
    (void)::sys::EventBus::init();
    ensureServicesReady();
    if (!impl_->call_audio) impl_->call_audio = std::make_unique<::platform::linux_runtime::NativeCallAudioBinding>();
    if (!impl_->demo_world_enabled)
    {
        ::platform::linux_runtime::bind_radio_power(impl_->raw_lora_mesh_adapter.powerRequests());
    }
    impl().mesh_adapter.setUserInfo(config_.node_name, config_.short_name);
    applyNetworkLimits();
    applyPrivacyConfig();
    applyMeshConfig();
    syncLocalIdentity();
    applyPositionConfig();
    applyChatDefaults();
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
    if (impl_->raw_lora_enabled && !impl_->demo_world_enabled)
    {
        auto* foreground = options_.foreground_radio ? options_.foreground_radio : (options_.packet_radio ? nullptr : &::platform::linux_runtime::foreground_packet_radio());
        if (!foreground)
        {
            impl_->voice_network_only_radio = ::platform::linux_runtime::make_unavailable_packet_radio("No foreground radio assigned to voice");
            foreground = impl_->voice_network_only_radio.get();
        }
        if (foreground)
        {
            ::platform::linux_runtime::VoiceServiceOptions voice;
            voice.database = ::platform::linux_runtime::sqlite_database_path();
            voice.self = getSelfNodeId();
            voice.protocol = config_.mesh_protocol;
            voice.mqtt_publish = [this](const uint8_t* bytes, size_t size, const std::atomic_bool& cancel)
            { return impl_->raw_lora_mesh_adapter.publishVoiceMqtt(bytes, size, cancel); };
            for (auto& contacts : impl_->voice_contacts) contacts = std::make_shared<::platform::linux_runtime::LinuxVoiceContacts>();
            const auto contacts = impl_->voice_contacts;
            voice.contacts = [contacts](::chat::MeshProtocol protocol) -> const ::chat::voice::vmp::IVerifiedContactSecretProvider*
            {
                if (protocol == ::chat::MeshProtocol::Meshtastic) return contacts[0].get();
                if (protocol == ::chat::MeshProtocol::MeshCore) return contacts[1].get();
                return nullptr;
            };
            voice.is_verified = [contacts](uint32_t peer, ::chat::MeshProtocol protocol)
            {
                if (protocol == ::chat::MeshProtocol::Meshtastic) return contacts[0]->contains(peer, protocol);
                if (protocol == ::chat::MeshProtocol::MeshCore) return contacts[1]->contains(peer, protocol);
                return false;
            };
            voice.changed = [](uint8_t channel, uint64_t id, bool)
            { ::sys::EventBus::publish(new ::sys::ChatVoiceMessageEvent(channel, id), 0); };
            impl_->voice = std::make_unique<::platform::linux_runtime::NativeVoiceService>(*foreground, std::move(voice));
            impl_->raw_lora_mesh_adapter.setVoiceControlSink(impl_->voice.get());
        }
    }
#endif
    initialized_ = true;
    ::platform::linux_runtime::bind_tms_snapshot(this);
    ::platform::linux_runtime::bind_tms_import(this);
    ::platform::linux_runtime::bind_identity_restore(this, ::platform::linux_runtime::sqlite_database_path());
    ::platform::linux_runtime::bind_settings_reset({this, [](void* owner)
                                                    { return static_cast<LinuxAppServices*>(owner)->prepareSettingsFactoryReset(); }});
    ::platform::linux_runtime::bind_reticulum_network_config(
        {this,
         [](void* owner, const ::chat::reticulum::ReticulumNetworkConfig& network)
         { return static_cast<LinuxAppServices*>(owner)->setReticulumNetworkConfig(network); },
         [](void* owner)
         {
             const auto* service = static_cast<LinuxAppServices*>(owner);
             return service->impl_ && !service->impl_->demo_world_enabled && service->impl_->raw_lora_mesh_adapter.reticulumNetworkConfigPending();
         }},
        reticulum_network_config_, has_reticulum_network_config_);
    if (options_.ble_supported)
    {
        phone_ = std::make_unique<LinuxPhoneRuntime>(*this, options_.phone_pairing_presenter);
        updatePhoneRuntime();
    }
    // The native application owns its local HostLink endpoint. Bind frontend
    // configuration services before admitting worker requests; shutdown stops
    // that worker before releasing domain services. The transport defaults to
    // loopback and retains its configurable bind address/port selection.
    ::platform::ui::hostlink::start();
    return true;
}

bool LinuxAppServices::ensureNodeIdentity()
{
    if (!self_node_id_)
    {
        std::string identity_error;
        self_node_id_ = options_.runtime_mode == ::platform::linux_runtime::LinuxRuntimeMode::SimulatorDemo
                            ? 0x435A0001U
                            : ::platform::linux_runtime::load_or_create_node_id(identity_error);
        if (!self_node_id_)
        {
            std::fprintf(stderr, "[Linux] %s\n", identity_error.c_str());
            return false;
        }
    }
    return true;
}

chat::DeviceIdentityRestoreResult LinuxAppServices::restoreDeviceIdentity(const ::platform::linux_runtime::IdentityRestoreCommand& command)
{
    using Result = chat::DeviceIdentityRestoreResult;
#if !defined(TRAIL_MATE_HAS_OPENSSL)
    (void)command;
    return Result::Unsupported;
#else
    namespace native = ::platform::linux_runtime;
    namespace call = ::platform::ui::reticulum_call;
    if (!initialized_ || !impl_ || settings_reset_pending_ || settings_import_pending_ || identity_restore_pending_ ||
        !native::application_lifecycle_active() || native::application_restart_requested()) return Result::Unavailable;
    const bool radio = command.target == native::IdentityRestoreTarget::Radio;
    if (!radio && command.target != native::IdentityRestoreTarget::DeviceAuthor) return Result::InvalidInput;
    if ((radio ? native::validate_radio_identity_restore(command.body.data(), command.size) : native::validate_identity_restore(command.body.data(), command.size)) != ::hostlink::management::Status::Ok) return Result::InvalidInput;
    ::hostlink::management::IdentityRestoreView request;
    ::hostlink::management::RadioIdentityRestoreView wireless;
    if (radio)
    {
        if (::hostlink::management::parseRadioIdentityRestoreRequest({command.body.data(), command.size}, wireless) != ::hostlink::management::Status::Ok) return Result::InvalidInput;
    }
    else if (::hostlink::management::parseIdentityRestoreRequest({command.body.data(), command.size}, request) != ::hostlink::management::Status::Ok) return Result::InvalidInput;
    if (!call::try_begin_configuration_change()) return Result::Unavailable;
    struct CallGate
    {
        bool retain = false;
        ~CallGate() { if (!retain) call::end_configuration_change(); }
    } gate;
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
    if (impl_->voice && impl_->voice->snapshot().busy) return Result::Unavailable;
#endif
    native::LinuxReticulumIdentity identity(native::sqlite_database_path());
    if (!identity.begin()) return Result::StorageError;
    const auto expected_generation = radio ? wireless.expected_author_generation : request.expected_generation;
    const auto expected_public = radio ? wireless.expected_author.data : request.expected_public.data;
    if (identity.generation() != expected_generation || std::memcmp(identity.publicKey().data(), expected_public, 64)) return Result::StaleIdentity;
    native::LinuxRadioIdentityStore radios(native::sqlite_database_path());
    if (radio)
    {
        native::RadioIdentitySnapshot current;
        if (!radios.read(wireless.kind, current).ok) return Result::StorageError;
        if (current.generation != wireless.expected_radio_generation || current.present != bool(wireless.expected_radio_public.data) ||
            (current.present && std::memcmp(current.public_key.data(), wireless.expected_radio_public.data, 32))) return Result::StaleRadioIdentity;
    }
    // Once resident users stop, restart on every outcome. A failed storage
    // transaction keeps the old key; restarting reopens its normal services.
    struct Restart
    {
        ~Restart() { (void)native::request_application_restart(); }
    } restart;
    identity_restore_pending_ = true;
    gate.retain = true;
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
    if (impl_->voice) impl_->voice->shutdown();
#endif
    impl_->raw_lora_mesh_adapter.shutdown();
    if (radio) return radios.replace(wireless.kind, wireless.expected_author_generation, wireless.expected_author.data,
        wireless.expected_radio_generation, wireless.expected_radio_public.data, wireless.public_key.data, wireless.private_key.data, wireless.private_key.size);
    return identity.replaceKeyPair(request.expected_generation, request.expected_public.data, request.format,
        request.private_key.data, request.private_key.size, request.public_key.data);
#endif
}

void LinuxAppServices::shutdown()
{
    ::platform::linux_runtime::unbind_identity_restore(this);
    ::platform::linux_runtime::unbind_tms_snapshot(this);
    ::platform::linux_runtime::unbind_tms_import(this);
    ::platform::linux_runtime::unbind_settings_reset(this);
    ::platform::linux_runtime::unbind_reticulum_network_config(this);
    ::platform::ui::hostlink::stop();
    ::platform::linux_runtime::stop_resource_pack_tasks();
    ::platform::linux_runtime::stop_route_image_tasks();
    // Phone callbacks use the domain services; stop them before radio/storage teardown.
    phone_.reset();
    if (initialized_ && config_persistence_runtime_.hasPending())
    {
        const auto now = ::sys::millis_now();
        (void)config_persistence_runtime_.submit(::app::AppConfigChangeSet::allPersisted(), now,
                                                 ::app::ConfigPersistenceUrgency::Immediate);
        flushConfigPersistence(now);
        if (config_persistence_runtime_.hasPending()) std::fprintf(stderr, "[Linux] Pending configuration could not be saved at shutdown\n");
    }
    if (impl_ && !impl_->demo_world_enabled)
    {
        ::platform::linux_runtime::unbind_radio_power(impl_->raw_lora_mesh_adapter.powerRequests());
        impl_->raw_lora_mesh_adapter.shutdown();
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
        impl_->raw_lora_mesh_adapter.setVoiceControlSink(nullptr);
        if (impl_->voice) impl_->voice->shutdown();
        impl_->voice.reset();
#endif
        LinuxMeshWorker::Outcome outcome;
        while (impl_->raw_lora_mesh_adapter.takeOutcome(outcome))
            impl_->chat_service.handleSendResultForProtocol(outcome.message, outcome.protocol,
                                                            outcome.status, 0, outcome.failure);
        impl_->chat_service.flushStore();
    }
    if (impl_) impl_->call_audio.reset();
    if (initialized_) ::platform::ui::gps::set_enabled(false);
    initialized_ = false;
}

bool LinuxAppServices::isInitialized() const noexcept
{
    return initialized_;
}

::platform::linux_runtime::IVoiceSource* LinuxAppServices::voiceSource()
{
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
    return impl_ ? impl_->voice.get() : nullptr;
#else
    return nullptr;
#endif
}

void LinuxAppServices::setUiEventDispatcher(UiEventDispatcher dispatcher,
                                            void* context) noexcept
{
    ui_event_dispatcher_ = dispatcher;
    ui_event_context_ = context;
}

bool LinuxAppServices::dispatchUiEvent(::sys::Event* event)
{
    if (ui_event_dispatcher_ == nullptr)
    {
        return false;
    }
    return ui_event_dispatcher_(ui_event_context_, event);
}

void LinuxAppServices::tick(std::size_t max_events)
{
    if (!initialized_ || settings_import_pending_ || identity_restore_pending_)
    {
        return;
    }
    updateCoreServices();
    if (identity_restore_pending_) return;
    tickEventRuntime();
    dispatchPendingEvents(max_events);
}

::app::AppConfig& LinuxAppServices::config()
{
    return getConfig();
}

const ::app::AppConfig& LinuxAppServices::config() const
{
    return getConfig();
}

::app::AppConfig& LinuxAppServices::getConfig()
{
    return config_;
}

const ::app::AppConfig& LinuxAppServices::getConfig() const
{
    return config_;
}

::app::AppConfigEdit LinuxAppServices::beginConfigEdit()
{
    return ::app::AppConfigEdit(&config_,
                                this,
                                &LinuxAppServices::commitConfigEdit,
                                &LinuxAppServices::cancelConfigEdit);
}

void LinuxAppServices::saveConfig()
{
    saveConfig(::app::AppConfigChangeSet::allPersisted());
}

void LinuxAppServices::saveConfig(::app::AppConfigChangeSet changes)
{
    if (settings_reset_pending_ || settings_import_pending_ || identity_restore_pending_) return;
    // The Linux blob backend has no section-level writer yet. Keep the
    // scoped request explicit while preserving the complete blob contract.
    config_.mesh_protocol =
        ::chat::infra::normalizeMeshProtocol(config_.mesh_protocol);
    const uint32_t now_ms = ::sys::millis_now();
    const auto submission = config_persistence_runtime_.submit(
        changes, now_ms, ::app::ConfigPersistenceUrgency::Debounced);
    (void)submission;
}

void LinuxAppServices::flushConfigPersistence(uint32_t now_ms)
{
    ::app::ConfigPersistenceWork work{};
    if (!config_persistence_runtime_.takeDue(now_ms, work))
    {
        return;
    }

    const bool ok = writePersistedConfig(config_);
    config_persistence_runtime_.complete(
        work.generation,
        ok ? ::app::ConfigPersistenceResultKind::Completed
           : ::app::ConfigPersistenceResultKind::IoError,
        ::sys::millis_now());
}

bool LinuxAppServices::writePersistedConfig(const ::app::AppConfig& config)
{
    const PersistedConfigBlob blob{.magic = kConfigBlobMagic,
                                   .version = kConfigBlobVersion,
                                   .config = config};
    return ::platform::ui::settings_store::put_blob(
        kConfigNamespace, kConfigBlobKey, &blob, sizeof(blob));
}

bool LinuxAppServices::prepareSettingsFactoryReset()
{
    if (!initialized_ || identity_restore_pending_) return false;
    if (settings_reset_pending_) return true;
    // The legacy Linux backend stores an object representation. Clear padding
    // and inactive text tails before constructing defaults in that storage.
    alignas(PersistedConfigBlob) unsigned char default_bytes[sizeof(PersistedConfigBlob)]{};
    auto* defaults = ::new (default_bytes) PersistedConfigBlob{.magic = kConfigBlobMagic,
                                                               .version = kConfigBlobVersion,
                                                               .config = ::app::AppConfig{}};
    defaults->config.ble_enabled = false;
    const bool committed = ::platform::linux_runtime::settings_storage::commit_factory_reset(defaults, sizeof(*defaults));
    defaults->~PersistedConfigBlob();
    if (!committed) return false;
    settings_reset_pending_ = true;
    config_persistence_runtime_.initialize();
    ::platform::linux_runtime::unbind_reticulum_network_config(this);
    return true;
}

void LinuxAppServices::commitConfigEdit(void* context,
                                        ::app::AppConfigChangeSet changes)
{
    auto* self = static_cast<LinuxAppServices*>(context);
    if (self != nullptr)
    {
        self->saveConfig(changes);
    }
}

void LinuxAppServices::cancelConfigEdit(void*)
{
}

void LinuxAppServices::applyMeshConfig()
{
    ensureServicesReady();
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
    if (impl_->voice) impl_->voice->invalidateContext();
#endif
    if (!impl().demo_world_enabled)
    {
        LinuxMeshWorker::MqttSettings mqtt;
        bool mqtt_fields_valid = true;
        const auto capture = [&mqtt_fields_valid](const auto& value)
        {
            const auto length = strnlen(value, sizeof(value));
            if (length == sizeof(value))
            {
                mqtt_fields_valid = false;
                return std::string{};
            }
            return std::string(value, length);
        };
        if (config_.mesh_protocol == chat::MeshProtocol::Meshtastic)
        {
            mqtt.enabled = config_.meshtastic_mqtt_enabled;
            mqtt.uplink = config_.meshtastic_mqtt_uplink_enabled;
            mqtt.downlink = config_.meshtastic_mqtt_downlink_enabled;
            mqtt.host = capture(config_.meshtastic_mqtt_host);
            mqtt.port = config_.meshtastic_mqtt_port;
            mqtt.root = capture(config_.meshtastic_mqtt_root);
            mqtt.username = capture(config_.meshtastic_mqtt_username);
            mqtt.password = capture(config_.meshtastic_mqtt_password);
        }
        else if (config_.mesh_protocol == chat::MeshProtocol::MeshCore)
        {
            const auto& mesh = config_.meshcore_config;
            mqtt.enabled = mesh.meshcore_mqtt_enabled;
            mqtt.uplink = mesh.meshcore_mqtt_uplink_enabled;
            mqtt.downlink = mesh.meshcore_mqtt_downlink_enabled;
            mqtt.host = capture(mesh.meshcore_mqtt_host);
            mqtt.port = mesh.meshcore_mqtt_port;
            mqtt.root = capture(mesh.meshcore_mqtt_root);
            mqtt.username = capture(mesh.meshcore_mqtt_username);
            mqtt.password = capture(mesh.meshcore_mqtt_password);
        }
        if (mqtt.root.empty()) mqtt.root = config_.mesh_protocol == chat::MeshProtocol::MeshCore ? app::AppConfig::kDefaultMeshCoreMqttRoot : app::AppConfig::kDefaultMeshtasticMqttRoot;
        if (!mqtt_fields_valid) mqtt.host = "/"; // Invalid enabled settings fail closed; never use a truncated endpoint.
        impl().raw_lora_mesh_adapter.applyProtocolConfig(
            config_.mesh_protocol, mesh_config_for_protocol(config_), mqtt);
        if (has_reticulum_network_config_)
            impl().raw_lora_mesh_adapter.applyReticulumNetworkConfig(reticulum_network_config_);
        else
        {
            if (::chat::reticulum::network_config_policy::build_defaults(config_.reticulumConfig(), &reticulum_network_config_))
                impl().raw_lora_mesh_adapter.applyReticulumNetworkConfig(reticulum_network_config_);
        }
    }
    else
    {
        impl().mesh_adapter.applyConfig(mesh_config_for_protocol(config_));
    }
    impl().chat_service.setActiveProtocol(config_.mesh_protocol);
    impl().contact_service.setActiveProtocol(config_.mesh_protocol);
    ::platform::linux_runtime::publish_reticulum_network_config(this, reticulum_network_config_, has_reticulum_network_config_);
}

bool LinuxAppServices::setReticulumNetworkConfig(const ::chat::reticulum::ReticulumNetworkConfig& config)
{
    if (!initialized_ || settings_reset_pending_ || identity_restore_pending_ || !::platform::linux_runtime::LinuxReticulumNetworkStore::save(config, config_)) return false;
    reticulum_network_config_ = config;
    for (size_t index = config.interface_count; index < ::chat::reticulum::kMaxNetworkInterfaces; ++index)
        reticulum_network_config_.interfaces[index] = ::chat::reticulum::NetworkInterfaceConfig{};
    has_reticulum_network_config_ = true;
    ::platform::linux_runtime::publish_reticulum_network_config(this, reticulum_network_config_, true);
    if (!impl_->demo_world_enabled) impl_->raw_lora_mesh_adapter.applyReticulumNetworkConfig(reticulum_network_config_);
    return true;
}

bool LinuxAppServices::getReticulumNetworkConfig(::chat::reticulum::ReticulumNetworkConfig* output) const
{
    if (!output || !initialized_ || !has_reticulum_network_config_) return false;
    *output = reticulum_network_config_;
    return true;
}

bool LinuxAppServices::exportSettingsDocument(::app::tms::DocumentKind kind, std::string& output)
{
    if (!initialized_ || settings_reset_pending_ || settings_import_pending_ || identity_restore_pending_) return false;
    if (config_persistence_runtime_.hasPending())
    {
        const auto now = ::sys::millis_now();
        (void)config_persistence_runtime_.submit(::app::AppConfigChangeSet::allPersisted(), now,
                                                 ::app::ConfigPersistenceUrgency::Immediate);
        flushConfigPersistence(now);
        if (config_persistence_runtime_.hasPending()) return false;
    }
    return export_linux_tms_snapshot(kind, output);
}

LinuxAppServices::SettingsImportResult LinuxAppServices::importSettingsDocument(
    const char* bytes, size_t length, ::app::tms::DocumentKind kind, const TmsCommitOptions* commit)
{
    using Result = SettingsImportResult;
    namespace call = ::platform::ui::reticulum_call;
    if (!initialized_ || settings_reset_pending_ || settings_import_pending_ || identity_restore_pending_ ||
        !::platform::linux_runtime::application_lifecycle_active()) return Result::Unavailable;
    auto plan = LinuxTmsImport::prepare(bytes, length, kind);
    if (!plan) return Result::InvalidDocument;
    if (commit)
    {
        std::string current;
        if (!exportSettingsDocument(::app::tms::DocumentKind::Working, current)) return Result::StorageError;
    }
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
    if (impl_->voice && impl_->voice->snapshot().busy) return Result::Busy;
#endif
    if (!call::try_begin_configuration_change()) return Result::Busy;
    struct CallGate
    {
        bool retain = false;
        ~CallGate()
        {
            if (!retain) ::platform::ui::reticulum_call::end_configuration_change();
        }
    } gate;
    const auto committed = commit ? plan->commit(*commit) : (plan->commit() ? LinuxTmsImport::CommitResult::Committed : LinuxTmsImport::CommitResult::StorageError);
    if (committed != LinuxTmsImport::CommitResult::Committed)
    {
        return committed == LinuxTmsImport::CommitResult::Conflict ? Result::Conflict : Result::StorageError;
    }
    gate.retain = true;
    settings_import_pending_ = true;
    config_persistence_runtime_.initialize();
    ::platform::linux_runtime::unbind_reticulum_network_config(this);
    // Existing UI loops observe the restart intent and perform normal teardown
    // before exec. Alt+F4 still wins and applies the committed set at next start.
    (void)::platform::linux_runtime::request_application_restart();
    return Result::RestartPending;
}

void LinuxAppServices::applyUserInfo()
{
    seedDefaultIdentity();
    syncLocalIdentity();
}

void LinuxAppServices::applyPositionConfig()
{
    platform::ui::gps::set_fallback_mode(
        impl().demo_world_enabled
            ? platform::ui::gps::FallbackMode::DemoDefaults
            : platform::ui::gps::FallbackMode::LiveOnly);
    platform::ui::gps::set_enabled(config_.gps_enabled);
    platform::ui::gps::GpsReceiverInitConfig receiver_init{};
    receiver_init.baud = config_.gps_init_baud;
    receiver_init.probe_ms = config_.gps_init_probe_ms;
    receiver_init.profile = config_.gps_init_profile;
    receiver_init.rxm_policy = config_.gps_init_rxm_policy;
    receiver_init.gnss_policy = config_.gps_init_gnss_policy;
    receiver_init.nmea_policy = config_.gps_init_nmea_policy;
    platform::ui::gps::set_receiver_init_config(receiver_init);
    platform::ui::gps::set_collection_interval(config_.gps_interval_ms);
    platform::ui::gps::set_power_strategy(config_.gps_strategy);
    platform::ui::gps::set_gnss_config(config_.gps_mode, config_.gps_sat_mask);
    platform::ui::gps::set_external_nmea_config(config_.external_nmea_output_hz,
                                                config_.external_nmea_sentence_mask);
    platform::ui::gps::set_motion_idle_timeout(config_.motion_config.idle_timeout_ms);
    platform::ui::gps::set_motion_sensor_id(config_.motion_config.sensor_id);
    platform::ui::tracker::set_interval_seconds(
        std::max<uint32_t>(1U, config_.map_track_interval));
    platform::ui::tracker::set_format(
        static_cast<platform::ui::tracker::Format>(
            std::min<std::uint8_t>(config_.map_track_format, 2U)));
    platform::ui::tracker::set_auto_recording(config_.map_track_enabled);
    (void)platform::ui::gps::diagnostics();
}

void LinuxAppServices::applyNetworkLimits()
{
    ensureServicesReady();
    impl().mesh_adapter.setNetworkLimits(config_.net_duty_cycle,
                                         config_.net_channel_util);
}

void LinuxAppServices::applyPrivacyConfig()
{
    ensureServicesReady();
    impl().mesh_adapter.setPrivacyConfig(config_.privacy_encrypt_mode);
}

void LinuxAppServices::applyChatDefaults()
{
    if (config_.chat_channel > 1U)
    {
        config_.chat_channel = 0U;
    }
    if (impl_)
    {
        impl_->chat_model.setPolicy(config_.chat_policy);
    }
}

::chat::MeshProtocol LinuxAppServices::meshProtocol() const
{
    return getMeshProtocol();
}

::chat::MeshProtocol LinuxAppServices::getMeshProtocol() const
{
    return config_.mesh_protocol;
}

bool LinuxAppServices::switchMeshProtocol(::chat::MeshProtocol protocol,
                                          bool persist)
{
    const auto normalized = ::chat::infra::normalizeMeshProtocol(protocol);
    if (!is_supported_protocol(normalized))
    {
        return false;
    }

    config_.mesh_protocol = normalized;
    if (impl_)
    {
        applyMeshConfig();
    }
    if (persist)
    {
        saveConfig();
    }
    return true;
}

::chat::ChatService& LinuxAppServices::chat()
{
    return getChatService();
}

::chat::ChatService& LinuxAppServices::getChatService()
{
    return impl().chat_service;
}

::chat::contacts::ContactService& LinuxAppServices::contacts()
{
    return getContactService();
}

::chat::contacts::ContactService& LinuxAppServices::getContactService()
{
    return impl().contact_service;
}

::chat::IMeshAdapter* LinuxAppServices::meshAdapter()
{
    return getMeshAdapter();
}

::chat::IMeshAdapter* LinuxAppServices::getMeshAdapter()
{
    return &impl().mesh_adapter;
}

const ::chat::IMeshAdapter* LinuxAppServices::meshAdapter() const
{
    return getMeshAdapter();
}

const ::chat::IMeshAdapter* LinuxAppServices::getMeshAdapter() const
{
    return &impl().mesh_adapter;
}

::chat::NodeId LinuxAppServices::selfNodeId() const
{
    return getSelfNodeId();
}

::chat::NodeId LinuxAppServices::getSelfNodeId() const
{
    if (impl_ && !impl_->demo_world_enabled &&
        config_.mesh_protocol == ::chat::MeshProtocol::MeshCore)
        return impl_->raw_lora_mesh_adapter.getNodeId();
    return self_node_id_;
}

::team::TeamController* LinuxAppServices::teamController()
{
    return getTeamController();
}

::team::TeamController* LinuxAppServices::getTeamController()
{
    return &impl().team_controller;
}

::team::TeamPairingService* LinuxAppServices::teamPairing()
{
    return getTeamPairing();
}

::team::TeamPairingService* LinuxAppServices::getTeamPairing()
{
    return &impl().pairing_service;
}

::team::TeamService* LinuxAppServices::teamService()
{
    return getTeamService();
}

::team::TeamService* LinuxAppServices::getTeamService()
{
    return &impl().team_service;
}

const ::team::TeamService* LinuxAppServices::teamService() const
{
    return getTeamService();
}

const ::team::TeamService* LinuxAppServices::getTeamService() const
{
    return &impl().team_service;
}

::team::TeamTrackSampler* LinuxAppServices::teamTrackSampler()
{
    return getTeamTrackSampler();
}

::team::TeamTrackSampler* LinuxAppServices::getTeamTrackSampler()
{
    return &impl().team_track_sampler;
}

void LinuxAppServices::setTeamModeActive(bool active)
{
    (void)active;
}

void LinuxAppServices::broadcastNodeInfo()
{
    syncLocalIdentity();
    (void)impl().mesh_adapter.broadcastSelfIdentity();
}

void LinuxAppServices::clearNodeDb()
{
    ensureServicesReady();
    impl().clearContactAndNodeData();
    syncLocalIdentity();
}

void LinuxAppServices::clearMessageDb()
{
    ensureServicesReady();
    impl().chat_model.clearAll();
    impl().chat_store.clearAll();
    ::team::ui::team_ui_snapshot_store().clear();
}

bool LinuxAppServices::isBleEnabled() const
{
    return config_.ble_enabled && phone_ && phone_->status().ready;
}

void LinuxAppServices::setBleEnabled(bool enabled)
{
    config_.ble_enabled = options_.ble_supported && enabled;
    saveConfig();
}

void LinuxAppServices::restartDevice()
{
    ::platform::ui::device::restart();
}
void LinuxAppServices::setPhonePairingPresenter(std::function<bool(const ::platform::linux_runtime::PhonePairingDisplay&)> presenter)
{
    options_.phone_pairing_presenter = std::move(presenter);
    if (phone_) phone_->setPairingPresenter(options_.phone_pairing_presenter);
}

void LinuxAppServices::updateCoreServices()
{
    if (!impl_ || settings_import_pending_ || identity_restore_pending_)
    {
        return;
    }
    ::platform::linux_runtime::service_tms_snapshot(this, [](void* owner, std::string& output)
                                                    { return static_cast<LinuxAppServices*>(owner)->exportSettingsDocument(::app::tms::DocumentKind::Working, output); });
    ::platform::linux_runtime::service_tms_import(this, [](void* owner, const ::platform::linux_runtime::TmsImportCommand& command)
                                                  {
            using Status = ::hostlink::management::Status;
            const auto result = static_cast<LinuxAppServices*>(owner)->importSettingsDocument(command.document.data(), command.document.size(),
                ::app::tms::DocumentKind::Working, &command.commit);
            switch (result)
            {
            case SettingsImportResult::RestartPending: return Status::Ok;
            case SettingsImportResult::Busy: return Status::Busy;
            case SettingsImportResult::Conflict: return Status::Conflict;
            case SettingsImportResult::InvalidDocument: return Status::InvalidField;
            case SettingsImportResult::StorageError: return Status::StorageUnavailable;
            default: return Status::Unsupported;
            } });
    if (settings_import_pending_) return;

    ::platform::linux_runtime::service_identity_restore(this, [](void* owner, const ::platform::linux_runtime::IdentityRestoreCommand& command)
        { return static_cast<LinuxAppServices*>(owner)->restoreDeviceIdentity(command); });
    if (identity_restore_pending_) return;

    // Both tick() and the explicit SDL/Cardputer loops pass through here.
    flushConfigPersistence(::sys::millis_now());

    impl_->syncPeerPublicKeys(config_.mesh_protocol);
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
    if (impl_->voice)
    {
        bool changed = false;
        const auto self = getSelfNodeId();
        const ::chat::MeshProtocol protocols[] = {::chat::MeshProtocol::Meshtastic, ::chat::MeshProtocol::MeshCore};
        for (size_t index = 0; index < impl_->voice_contacts.size(); ++index)
            changed = impl_->voice_contacts[index]->refresh(config_.mesh_protocol == protocols[index] ? self : 0,
                                                            protocols[index], impl_->mesh_peer_directory) ||
                      changed;
        if (changed) impl_->voice->invalidateContext();
        ::chat::MeshTransportStatus voice_transport;
        (void)impl_->raw_lora_mesh_adapter.getTransportStatus(&voice_transport);
        // MQTT media has its own carrier owner; generic network readiness must
        // never authorize the still-radio-only VMP session to acquire hardware.
        impl_->voice->setContext(self, config_.mesh_protocol, voice_transport.radio_ready);
        impl_->voice->setMqttReceiveReady(voice_transport.ready && voice_transport.network_ready &&
                                          config_.mesh_protocol == ::chat::MeshProtocol::Meshtastic &&
                                          config_.meshtastic_mqtt_enabled && config_.meshtastic_mqtt_downlink_enabled);
        const bool voice_mqtt = config_.mesh_protocol == ::chat::MeshProtocol::Meshtastic && config_.meshtastic_mqtt_enabled;
        impl_->voice->setMqttSendReady(voice_mqtt, voice_transport.ready && voice_transport.network_ready && config_.meshtastic_mqtt_uplink_enabled);
    }
#endif
    ::team::ui::TeamUiSnapshot snap;
    const bool has_team = ::team::ui::team_ui_snapshot_store().load(snap) && snap.in_team;
    ::platform::ui::gps::tick_service();
    impl_->team_track_sampler.update(&impl_->team_controller, has_team);
    ::platform::ui::tracker::poll();
    updatePhoneRuntime();
}

void LinuxAppServices::updatePhoneRuntime()
{
    if (!phone_) return;
    auto options = options_.phone_ble;
    const bool compatible = config_.mesh_protocol == ::chat::MeshProtocol::Meshtastic ||
                            config_.mesh_protocol == ::chat::MeshProtocol::MeshCore;
    options.protocol = config_.mesh_protocol == ::chat::MeshProtocol::MeshCore
                           ? ::platform::linux_runtime::PhoneGattProtocol::MeshCore
                           : ::platform::linux_runtime::PhoneGattProtocol::Meshtastic;
    phone_->configure(config_.ble_enabled && compatible, options);
    phone_->tick();
}

void LinuxAppServices::tickEventRuntime()
{
    if (!impl_ || identity_restore_pending_)
    {
        return;
    }

    impl_->pairing_service.update();
    ::sys::EventBus::publish(new ::sys::Event(::sys::EventType::SystemTick), 0);
}

void LinuxAppServices::dispatchPendingEvents(std::size_t max_events)
{
    if (!impl_ || identity_restore_pending_)
    {
        return;
    }

    impl_->tickMeshAdapter();
    impl_->chat_service.processIncoming();
    impl_->team_service.processIncoming();

    if (!impl_->demo_world_enabled)
    {
        LinuxMeshWorker::Outcome outcome;
        for (unsigned count = 0; count < 8 && impl_->raw_lora_mesh_adapter.takeOutcome(outcome); ++count)
        {
            impl_->chat_service.handleSendResultForProtocol(outcome.message, outcome.protocol,
                                                            outcome.status, 0, outcome.failure);
            ::sys::EventBus::publish(new ::sys::ChatSendResultEvent(outcome.message, outcome.status,
                                                                    outcome.protocol, outcome.failure),
                                     0);
        }
    }

    while (true)
    {
        ::chat::MessageId msg_id = 0;
        bool ok = false;
        if (!impl_->takePendingSendResult(msg_id, ok))
        {
            break;
        }
        const auto protocol = impl_->chat_service.getActiveProtocol();
        const auto status = ok ? ::chat::MessageStatus::Sent
                               : ::chat::MessageStatus::Failed;
        const auto failure =
            ok ? ::chat::delivery::SendFailureKind::None
               : ::chat::delivery::SendFailureKind::RadioSendFailed;
        impl_->chat_service.handleSendResultForProtocol(
            msg_id,
            protocol,
            status,
            0,
            failure);
        ::sys::EventBus::publish(
            new ::sys::ChatSendResultEvent(msg_id, status, protocol, failure),
            0);
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

bool LinuxAppServices::loadPersistedConfig()
{
    PersistedConfigBlob blob{};
    size_t length = 0;
    using Result = ::platform::ui::settings_store::BlobReadResult;
    const auto read = ::platform::ui::settings_store::read_blob_into(kConfigNamespace, kConfigBlobKey, &blob, sizeof(blob), &length);
    if (read == Result::Missing)
    {
        config_ = ::app::AppConfig{};
        config_.ble_enabled = false;
        return !sync_reticulum_group_config(config_) || writePersistedConfig(config_);
    }
    if (read != Result::Ok || length != sizeof(blob))
    {
        std::fprintf(stderr, "[Config] Saved application configuration is %s; initialization stopped\n",
                     read == Result::Unavailable ? "unavailable" : "invalid");
        return false;
    }
    if (blob.magic != kConfigBlobMagic || (blob.version != 2U && blob.version != kConfigBlobVersion) ||
        !is_supported_protocol(blob.config.mesh_protocol))
    {
        std::fprintf(stderr, "[Config] Unsupported application configuration header; initialization stopped\n");
        return false;
    }

    config_ = blob.config;
    config_.mesh_protocol = ::chat::infra::normalizeMeshProtocol(config_.mesh_protocol);
    if (!options_.ble_supported) config_.ble_enabled = false;
    if (blob.version == 2U)
    {
        const auto& groups = config_.reticulumConfig().reticulum_groups;
        const bool populated = std::any_of(std::begin(groups), std::end(groups), [](const auto& group)
                                           { return ::chat::hasReticulumDestinationIdentity(group.identity); });
        // Preserve existing snapshot data; otherwise import the legacy source
        // once. Version 3 also records an intentionally empty modern group list.
        if ((populated || sync_reticulum_group_config(config_)) && !writePersistedConfig(config_)) return false;
    }
    return true;
}

void LinuxAppServices::seedDefaultIdentity()
{
    if (config_.node_name[0] == '\0')
    {
        copy_bounded(config_.node_name, sizeof(config_.node_name), options_.default_node_name);
    }
    if (config_.short_name[0] == '\0')
    {
        copy_bounded(config_.short_name, sizeof(config_.short_name), options_.default_short_name);
    }
    if (!options_.ble_supported) config_.ble_enabled = false;
    applyChatDefaults();
}

void LinuxAppServices::syncLocalIdentity()
{
    ensureServicesReady();
    impl().mesh_adapter.setUserInfo(config_.node_name, config_.short_name);
    impl().chat_service.setActiveProtocol(config_.mesh_protocol);
    if (!getSelfNodeId()) return; // A failed protocol identity must never create a local node 0.
    impl().contact_service.updateNodeInfo(getSelfNodeId(),
                                          config_.short_name,
                                          config_.node_name,
                                          0.0f,
                                          0.0f,
                                          sys::epoch_seconds_now(),
                                          static_cast<uint8_t>(
                                              ::chat::infra::normalizeMeshProtocol(config_.mesh_protocol)));
    ::chat::contacts::NodeUpdate self_update{};
    self_update.has_is_ignored = true;
    self_update.is_ignored = true;
    impl().contact_service.applyNodeUpdate(getSelfNodeId(), self_update);
}

void LinuxAppServices::ensureServicesReady()
{
    if (!ensureNodeIdentity()) throw std::runtime_error("Linux device identity is unavailable");
    if (!impl_)
    {
        impl_ = std::make_unique<Implementation>(options_, self_node_id_);
    }
    impl_->ensureStarted(self_node_id_);
}

} // namespace trailmate::linux_app
