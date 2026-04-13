#include "apps/esp_idf/app_facade_runtime.h"
#include "apps/esp_idf/meshtastic_radio_adapter.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "app/app_facades.h"
#include "boards/tab5/tab5_board.h"
#include "chat/domain/chat_model.h"
#include "chat/infra/contact_store_core.h"
#include "chat/infra/mesh_adapter_router_core.h"
#include "chat/infra/node_store_core.h"
#include "chat/infra/store/ram_store.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "esp_log.h"
#include "sys/clock.h"
#include "sys/event_bus.h"
#include "team/usecase/team_controller.h"
#include "team/usecase/team_pairing_service.h"
#include "team/usecase/team_service.h"
#include "ui/chat_ui_runtime.h"
#include "ui/screens/team/team_page_shell.h"
#include "ui/screens/team/team_ui_store.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace apps::esp_idf::app_facade_runtime
{
namespace
{

class InMemoryNodeBlobStore final : public chat::contacts::INodeBlobStore
{
  public:
    bool loadBlob(std::vector<uint8_t>& out) override
    {
        out = blob_;
        return !blob_.empty();
    }

    bool saveBlob(const uint8_t* data, size_t len) override
    {
        if (!data || len == 0)
        {
            blob_.clear();
            return true;
        }

        blob_.assign(data, data + len);
        return true;
    }

    void clearBlob() override
    {
        blob_.clear();
    }

  private:
    std::vector<uint8_t> blob_;
};

class InMemoryContactBlobStore final : public chat::IContactBlobStore
{
  public:
    bool loadBlob(std::vector<uint8_t>& out) override
    {
        out = blob_;
        return !blob_.empty();
    }

    bool saveBlob(const uint8_t* data, size_t len) override
    {
        if (!data || len == 0)
        {
            blob_.clear();
            return true;
        }

        blob_.assign(data, data + len);
        return true;
    }

  private:
    std::vector<uint8_t> blob_;
};

class LoopbackMeshAdapter final : public chat::IMeshAdapter
{
  public:
    explicit LoopbackMeshAdapter(chat::NodeId self_node_id)
        : self_node_id_(self_node_id)
    {
    }

    chat::MeshCapabilities getCapabilities() const override
    {
        chat::MeshCapabilities caps{};
        caps.supports_unicast_text = true;
        caps.supports_unicast_appdata = true;
        caps.supports_node_info = true;
        caps.supports_pki = true;
        return caps;
    }

    bool sendText(chat::ChannelId channel, const std::string& text,
                  chat::MessageId* out_msg_id, chat::NodeId peer) override
    {
        (void)channel;
        (void)peer;

        if (text.empty())
        {
            if (out_msg_id)
            {
                *out_msg_id = 0;
            }
            return false;
        }

        chat::MessageId msg_id = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            msg_id = next_msg_id_++;
            pending_send_results_.push_back(msg_id);
        }

        if (out_msg_id)
        {
            *out_msg_id = msg_id;
        }
        return true;
    }

    bool pollIncomingText(chat::MeshIncomingText* out) override
    {
        (void)out;
        return false;
    }

    bool sendAppData(chat::ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     chat::NodeId dest, bool want_ack,
                     chat::MessageId packet_id,
                     bool want_response) override
    {
        (void)channel;
        (void)portnum;
        (void)dest;
        (void)want_ack;
        (void)packet_id;
        (void)want_response;
        return payload && len > 0;
    }

    bool pollIncomingData(chat::MeshIncomingData* out) override
    {
        (void)out;
        return false;
    }

    void applyConfig(const chat::MeshConfig& config) override
    {
        config_ = config;
    }

    bool isReady() const override
    {
        return true;
    }

    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override
    {
        (void)out_data;
        (void)max_len;
        out_len = 0;
        return false;
    }

    chat::NodeId getNodeId() const override
    {
        return self_node_id_;
    }

    bool requestNodeInfo(chat::NodeId dest, bool want_response) override
    {
        (void)dest;
        (void)want_response;
        return true;
    }

    bool startKeyVerification(chat::NodeId dest) override
    {
        (void)dest;
        return true;
    }

    bool submitKeyVerificationNumber(chat::NodeId dest, uint64_t nonce, uint32_t number) override
    {
        (void)dest;
        (void)nonce;
        (void)number;
        return true;
    }

    void setUserInfo(const char* long_name, const char* short_name) override
    {
        copyString(long_name, long_name_, sizeof(long_name_));
        copyString(short_name, short_name_, sizeof(short_name_));
    }

    bool drainSendResults(std::vector<chat::MessageId>& out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_send_results_.empty())
        {
            return false;
        }
        out.swap(pending_send_results_);
        return true;
    }

  private:
    static void copyString(const char* src, char* dst, size_t dst_len)
    {
        if (!dst || dst_len == 0)
        {
            return;
        }

        if (!src)
        {
            dst[0] = '\0';
            return;
        }

        const size_t copy_len = std::min(std::strlen(src), dst_len - 1);
        std::memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }

    mutable std::mutex mutex_;
    std::vector<chat::MessageId> pending_send_results_;
    chat::MeshConfig config_{};
    chat::NodeId self_node_id_ = 0;
    chat::MessageId next_msg_id_ = 1;
    char long_name_[32] = {};
    char short_name_[16] = {};
};

class MinimalTeamRuntime final : public team::ITeamRuntime
{
  public:
    uint32_t nowMillis() override
    {
        return sys::millis_now();
    }

    uint32_t nowUnixSeconds() override
    {
        return sys::millis_now() / 1000U;
    }

    void fillRandomBytes(uint8_t* out, size_t len) override
    {
        if (!out)
        {
            return;
        }

        if (rng_state_ == 0)
        {
            rng_state_ = sys::millis_now() ^ 0x13579BDFu;
            if (rng_state_ == 0)
            {
                rng_state_ = 0x2468ACE1u;
            }
        }

        for (size_t index = 0; index < len; ++index)
        {
            rng_state_ ^= (rng_state_ << 13);
            rng_state_ ^= (rng_state_ >> 17);
            rng_state_ ^= (rng_state_ << 5);
            out[index] = static_cast<uint8_t>(rng_state_ & 0xFFu);
        }
    }

  private:
    uint32_t rng_state_ = 0;
};

class MinimalTeamCrypto final : public team::ITeamCrypto
{
  public:
    bool deriveKey(const uint8_t* key, size_t key_len,
                   const char* info,
                   uint8_t* out, size_t out_len) override
    {
        if (!key || key_len == 0 || !info || !out || out_len == 0)
        {
            return false;
        }

        uint32_t state = 0x811C9DC5u;
        mixBytes(state, key, key_len);
        mixBytes(state,
                 reinterpret_cast<const uint8_t*>(info),
                 std::strlen(info));
        for (size_t index = 0; index < out_len; ++index)
        {
            state ^= (state << 13);
            state ^= (state >> 17);
            state ^= (state << 5);
            out[index] = static_cast<uint8_t>((state >> ((index & 0x3u) * 8)) & 0xFFu);
        }
        return true;
    }

    bool aeadEncrypt(const uint8_t* key, size_t key_len,
                     const uint8_t* nonce, size_t nonce_len,
                     const uint8_t* aad, size_t aad_len,
                     const uint8_t* plain, size_t plain_len,
                     std::vector<uint8_t>& out_cipher) override
    {
        if (!key || key_len == 0 || !nonce || nonce_len == 0 || !plain)
        {
            return false;
        }

        out_cipher.assign(plain_len + kPseudoTagSize, 0);
        for (size_t index = 0; index < plain_len; ++index)
        {
            out_cipher[index] = plain[index] ^ streamByte(key, key_len, nonce, nonce_len, aad, aad_len, index);
        }

        uint8_t tag[kPseudoTagSize] = {};
        computeTag(key, key_len, nonce, nonce_len, aad, aad_len,
                   out_cipher.data(), plain_len, tag);
        std::memcpy(out_cipher.data() + plain_len, tag, kPseudoTagSize);
        return true;
    }

    bool aeadDecrypt(const uint8_t* key, size_t key_len,
                     const uint8_t* nonce, size_t nonce_len,
                     const uint8_t* aad, size_t aad_len,
                     const uint8_t* cipher, size_t cipher_len,
                     std::vector<uint8_t>& out_plain) override
    {
        if (!key || key_len == 0 || !nonce || nonce_len == 0 || !cipher || cipher_len < kPseudoTagSize)
        {
            return false;
        }

        const size_t plain_len = cipher_len - kPseudoTagSize;
        const uint8_t* tag = cipher + plain_len;
        uint8_t expected[kPseudoTagSize] = {};
        computeTag(key, key_len, nonce, nonce_len, aad, aad_len, cipher, plain_len, expected);
        if (std::memcmp(tag, expected, kPseudoTagSize) != 0)
        {
            out_plain.clear();
            return false;
        }

        out_plain.assign(plain_len, 0);
        for (size_t index = 0; index < plain_len; ++index)
        {
            out_plain[index] = cipher[index] ^ streamByte(key, key_len, nonce, nonce_len, aad, aad_len, index);
        }
        return true;
    }

  private:
    static constexpr size_t kPseudoTagSize = 16;

    static void mixBytes(uint32_t& state, const uint8_t* data, size_t len)
    {
        if (!data)
        {
            return;
        }

        for (size_t index = 0; index < len; ++index)
        {
            state ^= data[index];
            state *= 16777619u;
            state ^= (state >> 13);
        }
    }

    static uint8_t streamByte(const uint8_t* key, size_t key_len,
                              const uint8_t* nonce, size_t nonce_len,
                              const uint8_t* aad, size_t aad_len,
                              size_t index)
    {
        uint8_t value = key[index % key_len] ^ nonce[index % nonce_len] ^ static_cast<uint8_t>(index * 31u);
        if (aad && aad_len > 0)
        {
            value ^= aad[index % aad_len];
        }
        value ^= static_cast<uint8_t>((index >> 3) & 0xFFu);
        return value;
    }

    static void computeTag(const uint8_t* key, size_t key_len,
                           const uint8_t* nonce, size_t nonce_len,
                           const uint8_t* aad, size_t aad_len,
                           const uint8_t* cipher, size_t cipher_len,
                           uint8_t out[kPseudoTagSize])
    {
        uint32_t state = 0x9E3779B9u;
        mixBytes(state, key, key_len);
        mixBytes(state, nonce, nonce_len);
        mixBytes(state, aad, aad_len);
        mixBytes(state, cipher, cipher_len);
        for (size_t index = 0; index < kPseudoTagSize; ++index)
        {
            state ^= (state << 13);
            state ^= (state >> 17);
            state ^= (state << 5);
            out[index] = static_cast<uint8_t>((state >> ((index & 0x3u) * 8)) & 0xFFu);
        }
    }
};

class MinimalTeamEventSink final : public team::ITeamEventSink
{
  public:
    void onTeamKick(const team::TeamKickEvent& event) override { (void)event; }
    void onTeamTransferLeader(const team::TeamTransferLeaderEvent& event) override { (void)event; }
    void onTeamKeyDist(const team::TeamKeyDistEvent& event) override { (void)event; }
    void onTeamStatus(const team::TeamStatusEvent& event) override { (void)event; }
    void onTeamPosition(const team::TeamPositionEvent& event) override { (void)event; }
    void onTeamWaypoint(const team::TeamWaypointEvent& event) override { (void)event; }
    void onTeamTrack(const team::TeamTrackEvent& event) override { (void)event; }
    void onTeamChat(const team::TeamChatEvent& event) override { (void)event; }
    void onTeamError(const team::TeamErrorEvent& event) override { (void)event; }
};

class MinimalTeamPairingService final : public team::TeamPairingService
{
  public:
    void setIdentity(uint32_t self_node_id, const char* self_name)
    {
        self_node_id_ = self_node_id;
        copyString(self_name, self_name_, sizeof(self_name_));
    }

    bool startLeader(const team::TeamId& team_id,
                     uint32_t key_id,
                     const uint8_t* psk,
                     size_t psk_len,
                     uint32_t leader_id,
                     const char* team_name) override
    {
        if (leader_id == 0 || !psk || psk_len == 0)
        {
            return false;
        }

        resetStatus();
        self_node_id_ = leader_id;
        status_.role = team::TeamPairingRole::Leader;
        status_.state = team::TeamPairingState::LeaderBeacon;
        status_.team_id = team_id;
        status_.has_team_id = true;
        status_.key_id = key_id;
        status_.peer_id = leader_id;
        copyTeamName(team_name);
        copyPsk(psk, psk_len);
        phase_started_ms_ = sys::millis_now();

        team::ui::TeamUiSnapshot snapshot = loadOrDefaultSnapshot();
        const uint32_t now_s = phase_started_ms_ / 1000U;
        snapshot.in_team = true;
        snapshot.pending_join = true;
        snapshot.pending_join_started_s = now_s;
        snapshot.kicked_out = false;
        snapshot.self_is_leader = true;
        snapshot.team_id = team_id;
        snapshot.has_team_id = true;
        snapshot.security_round = key_id;
        snapshot.last_update_s = now_s;
        if (status_.has_team_name)
        {
            snapshot.team_name = status_.team_name;
        }
        applyPsk(snapshot);
        ensureMember(snapshot, leader_id, selfDisplayName(), true, now_s);
        saveSnapshot(snapshot);
        return true;
    }

    bool startMember(uint32_t self_id) override
    {
        if (self_id == 0)
        {
            return false;
        }

        resetStatus();
        self_node_id_ = self_id;

        team::ui::TeamUiSnapshot snapshot = loadOrDefaultSnapshot();
        if (!snapshot.has_team_id)
        {
            snapshot.team_id = {0x54, 0x4D, 0x35, 0x2D, 0x4A, 0x4F, 0x49, 0x4E};
            snapshot.has_team_id = true;
        }
        if (snapshot.team_name.empty())
        {
            snapshot.team_name = "Tab5 Demo";
        }
        if (snapshot.security_round == 0)
        {
            snapshot.security_round = 1;
        }
        if (!snapshot.has_team_psk)
        {
            for (size_t i = 0; i < snapshot.team_psk.size(); ++i)
            {
                snapshot.team_psk[i] = static_cast<uint8_t>(0x40 + (i & 0x3F));
            }
            snapshot.has_team_psk = true;
        }

        status_.role = team::TeamPairingRole::Member;
        status_.state = team::TeamPairingState::MemberScanning;
        status_.team_id = snapshot.team_id;
        status_.has_team_id = true;
        status_.key_id = snapshot.security_round;
        status_.peer_id = leaderIdFromSnapshot(snapshot);
        copyTeamName(snapshot.team_name.c_str());
        copyPsk(snapshot.team_psk.data(), snapshot.team_psk.size());
        phase_started_ms_ = sys::millis_now();

        const uint32_t now_s = phase_started_ms_ / 1000U;
        snapshot.in_team = false;
        snapshot.pending_join = true;
        snapshot.pending_join_started_s = now_s;
        snapshot.kicked_out = false;
        snapshot.self_is_leader = false;
        snapshot.last_update_s = now_s;
        ensureMember(snapshot, self_id, selfDisplayName(), false, now_s);
        saveSnapshot(snapshot);
        return true;
    }

    void stop() override
    {
        if (status_.state != team::TeamPairingState::Idle)
        {
            team::ui::TeamUiSnapshot snapshot = loadOrDefaultSnapshot();
            snapshot.pending_join = false;
            snapshot.pending_join_started_s = 0;
            snapshot.last_update_s = sys::millis_now() / 1000U;
            if (status_.role == team::TeamPairingRole::Member &&
                status_.state != team::TeamPairingState::Completed)
            {
                snapshot.in_team = false;
                snapshot.self_is_leader = false;
            }
            saveSnapshot(snapshot);
        }
        resetStatus();
    }

    void update() override
    {
        if (status_.state == team::TeamPairingState::Idle ||
            status_.state == team::TeamPairingState::Completed ||
            status_.state == team::TeamPairingState::Failed)
        {
            return;
        }

        const uint32_t now_ms = sys::millis_now();
        const uint32_t now_s = now_ms / 1000U;
        const uint32_t elapsed_ms = now_ms - phase_started_ms_;

        if (status_.role == team::TeamPairingRole::Leader)
        {
            if (status_.state == team::TeamPairingState::LeaderBeacon &&
                elapsed_ms >= kLeaderCompleteDelayMs)
            {
                completeLeader(now_s);
            }
            return;
        }

        if (status_.role != team::TeamPairingRole::Member)
        {
            return;
        }

        if (status_.state == team::TeamPairingState::MemberScanning &&
            elapsed_ms >= kMemberScanDelayMs)
        {
            status_.state = team::TeamPairingState::JoinSent;
            phase_started_ms_ = now_ms;
            persistPendingSnapshot(now_s);
            return;
        }

        if (status_.state == team::TeamPairingState::JoinSent &&
            elapsed_ms >= kMemberJoinDelayMs)
        {
            status_.state = team::TeamPairingState::WaitingKey;
            phase_started_ms_ = now_ms;
            persistPendingSnapshot(now_s);
            return;
        }

        if (status_.state == team::TeamPairingState::WaitingKey &&
            elapsed_ms >= kMemberKeyDelayMs)
        {
            completeMember(now_s);
        }
    }

    team::TeamPairingStatus getStatus() const override
    {
        return status_;
    }

  private:
    static constexpr uint32_t kLeaderCompleteDelayMs = 2500;
    static constexpr uint32_t kMemberScanDelayMs = 1000;
    static constexpr uint32_t kMemberJoinDelayMs = 1200;
    static constexpr uint32_t kMemberKeyDelayMs = 1200;

    static void copyString(const char* src, char* dst, size_t dst_len)
    {
        if (!dst || dst_len == 0)
        {
            return;
        }

        if (!src)
        {
            dst[0] = '\0';
            return;
        }

        const size_t copy_len = std::min(std::strlen(src), dst_len - 1);
        std::memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }

    void copyTeamName(const char* team_name)
    {
        copyString(team_name, status_.team_name, sizeof(status_.team_name));
        status_.has_team_name = status_.team_name[0] != '\0';
    }

    void copyPsk(const uint8_t* psk, size_t psk_len)
    {
        psk_.fill(0);
        has_psk_ = psk && psk_len > 0;
        if (!has_psk_)
        {
            return;
        }

        const size_t copy_len = std::min(psk_len, psk_.size());
        std::copy_n(psk, copy_len, psk_.begin());
    }

    void applyPsk(team::ui::TeamUiSnapshot& snapshot) const
    {
        if (!has_psk_)
        {
            return;
        }
        snapshot.team_psk = psk_;
        snapshot.has_team_psk = true;
    }

    team::ui::TeamUiSnapshot loadOrDefaultSnapshot() const
    {
        team::ui::TeamUiSnapshot snapshot{};
        if (team::ui::team_ui_get_store().load(snapshot))
        {
            return snapshot;
        }

        snapshot.has_team_id = status_.has_team_id;
        snapshot.team_id = status_.team_id;
        snapshot.security_round = status_.key_id;
        if (status_.has_team_name)
        {
            snapshot.team_name = status_.team_name;
        }
        applyPsk(snapshot);
        return snapshot;
    }

    void saveSnapshot(const team::ui::TeamUiSnapshot& snapshot) const
    {
        team::ui::team_ui_get_store().save(snapshot);
    }

    uint32_t fallbackLeaderId() const
    {
        return self_node_id_ ? (self_node_id_ ^ 0x00A10001U) : 0xA11A0001U;
    }

    uint32_t leaderIdFromSnapshot(const team::ui::TeamUiSnapshot& snapshot) const
    {
        for (const auto& member : snapshot.members)
        {
            if (member.leader && member.node_id != 0)
            {
                return member.node_id;
            }
        }
        for (const auto& member : snapshot.members)
        {
            if (member.node_id != 0 && member.node_id != self_node_id_)
            {
                return member.node_id;
            }
        }
        return fallbackLeaderId();
    }

    const char* selfDisplayName() const
    {
        return self_name_[0] ? self_name_ : "You";
    }

    void ensureMember(team::ui::TeamUiSnapshot& snapshot,
                      uint32_t node_id,
                      const char* name,
                      bool leader,
                      uint32_t now_s) const
    {
        if (node_id == 0)
        {
            return;
        }

        if (leader)
        {
            for (auto& member : snapshot.members)
            {
                member.leader = false;
            }
        }

        auto it = std::find_if(snapshot.members.begin(), snapshot.members.end(),
                               [node_id](const team::ui::TeamMemberUi& member)
                               {
                                   return member.node_id == node_id;
                               });
        if (it == snapshot.members.end())
        {
            team::ui::TeamMemberUi member{};
            member.node_id = node_id;
            member.name = name ? name : "";
            member.online = true;
            member.leader = leader;
            member.last_seen_s = now_s;
            member.color_index = team::ui::team_color_index_from_node_id(node_id);
            snapshot.members.push_back(std::move(member));
            return;
        }

        it->online = true;
        it->leader = leader;
        it->last_seen_s = now_s;
        it->color_index = team::ui::team_color_index_from_node_id(node_id);
        if (name && name[0] != '\0')
        {
            it->name = name;
        }
    }

    void persistPendingSnapshot(uint32_t now_s)
    {
        team::ui::TeamUiSnapshot snapshot = loadOrDefaultSnapshot();
        snapshot.pending_join = true;
        if (snapshot.pending_join_started_s == 0)
        {
            snapshot.pending_join_started_s = now_s;
        }
        snapshot.last_update_s = now_s;
        snapshot.kicked_out = false;
        if (status_.has_team_id)
        {
            snapshot.team_id = status_.team_id;
            snapshot.has_team_id = true;
        }
        if (status_.has_team_name)
        {
            snapshot.team_name = status_.team_name;
        }
        if (status_.key_id != 0)
        {
            snapshot.security_round = status_.key_id;
        }
        applyPsk(snapshot);
        saveSnapshot(snapshot);
    }

    void completeLeader(uint32_t now_s)
    {
        team::ui::TeamUiSnapshot snapshot = loadOrDefaultSnapshot();
        snapshot.in_team = true;
        snapshot.pending_join = false;
        snapshot.pending_join_started_s = 0;
        snapshot.kicked_out = false;
        snapshot.self_is_leader = true;
        snapshot.last_update_s = now_s;
        if (status_.has_team_id)
        {
            snapshot.team_id = status_.team_id;
            snapshot.has_team_id = true;
        }
        if (status_.has_team_name)
        {
            snapshot.team_name = status_.team_name;
        }
        if (status_.key_id != 0)
        {
            snapshot.security_round = status_.key_id;
        }
        applyPsk(snapshot);
        ensureMember(snapshot, self_node_id_, selfDisplayName(), true, now_s);
        saveSnapshot(snapshot);
        status_.state = team::TeamPairingState::Completed;
    }

    void completeMember(uint32_t now_s)
    {
        team::ui::TeamUiSnapshot snapshot = loadOrDefaultSnapshot();
        snapshot.in_team = true;
        snapshot.pending_join = false;
        snapshot.pending_join_started_s = 0;
        snapshot.kicked_out = false;
        snapshot.self_is_leader = false;
        snapshot.last_update_s = now_s;
        if (status_.has_team_id)
        {
            snapshot.team_id = status_.team_id;
            snapshot.has_team_id = true;
        }
        if (status_.has_team_name)
        {
            snapshot.team_name = status_.team_name;
        }
        if (status_.key_id != 0)
        {
            snapshot.security_round = status_.key_id;
        }
        applyPsk(snapshot);

        const uint32_t leader_id = status_.peer_id != 0 ? status_.peer_id : fallbackLeaderId();
        ensureMember(snapshot, leader_id, "Leader", true, now_s);
        ensureMember(snapshot, self_node_id_, selfDisplayName(), false, now_s);
        saveSnapshot(snapshot);
        if (status_.has_team_id)
        {
            (void)team::ui::team_ui_chatlog_append(status_.team_id,
                                                   leader_id,
                                                   true,
                                                   now_s,
                                                   "Joined shared Team runtime.");
        }
        status_.state = team::TeamPairingState::Completed;
    }

    void resetStatus()
    {
        status_ = team::TeamPairingStatus{};
        phase_started_ms_ = 0;
        psk_.fill(0);
        has_psk_ = false;
    }

    team::TeamPairingStatus status_{};
    uint32_t self_node_id_ = 0;
    uint32_t phase_started_ms_ = 0;
    std::array<uint8_t, team::proto::kTeamChannelPskSize> psk_{};
    bool has_psk_ = false;
    char self_name_[16] = {};
};

class MinimalAppFacade final : public app::IAppFacade
{
  public:
    bool initialize(const RuntimeConfig& runtime_config)
    {
        if (initialized_)
        {
            return true;
        }

        runtime_config_ = runtime_config;
        if (!sys::EventBus::init())
        {
            return false;
        }
        configureIdentity(runtime_config);
        self_node_id_ = makeNodeId(runtime_config.target_name);

        node_blob_store_ = std::make_unique<InMemoryNodeBlobStore>();
        contact_blob_store_ = std::make_unique<InMemoryContactBlobStore>();
        node_store_ = std::make_unique<chat::contacts::NodeStoreCore>(*node_blob_store_);
        contact_store_ = std::make_unique<chat::contacts::ContactStoreCore>(*contact_blob_store_);
        contact_service_ = std::make_unique<chat::contacts::ContactService>(*node_store_, *contact_store_);
        contact_service_->begin();

        team_pairing_ = std::make_unique<MinimalTeamPairingService>();
        team_pairing_->setIdentity(self_node_id_, config_.short_name);

        chat_model_ = std::make_unique<chat::ChatModel>();
        chat_store_ = std::make_unique<chat::RamStore>();
        mesh_router_ = std::make_unique<chat::MeshAdapterRouterCore>();
        installMeshBackend(config_.mesh_protocol);
        if (mesh_router_)
        {
            self_node_id_ = mesh_router_->getNodeId();
        }
        team_pairing_->setIdentity(self_node_id_, config_.short_name);
        chat_service_ = std::make_unique<chat::ChatService>(*chat_model_, *mesh_router_, *chat_store_, config_.mesh_protocol);

        team_runtime_ = std::make_unique<MinimalTeamRuntime>();
        team_crypto_ = std::make_unique<MinimalTeamCrypto>();
        team_event_sink_ = std::make_unique<MinimalTeamEventSink>();
        team_service_ = std::make_unique<team::TeamService>(*team_crypto_, *mesh_router_, *team_event_sink_, *team_runtime_);
        team_controller_ = std::make_unique<team::TeamController>(*team_service_);

        applyUserInfo();
        applyChatDefaults();
        clearLegacyDemoState();
        syncTeamKeysFromStore();

        app::bindAppFacade(*this);
        initialized_ = true;
        return true;
    }

    void tick()
    {
        if (!initialized_)
        {
            return;
        }

        if (mesh_router_)
        {
            mesh_router_->processSendQueue();
        }

        if (loopback_adapter_ && chat_service_)
        {
            std::vector<chat::MessageId> sent_ids;
            if (loopback_adapter_->drainSendResults(sent_ids))
            {
                for (const chat::MessageId msg_id : sent_ids)
                {
                    chat_service_->handleSendResult(msg_id, true);
                }
            }
        }

        if (chat_service_)
        {
            chat_service_->processIncoming();
            chat_service_->flushStore();
        }

        if (team_pairing_)
        {
            team_pairing_->update();
        }

        syncTeamKeysFromStore();

        if (team_service_)
        {
            team_service_->processIncoming();
        }
    }

    bool isBound() const
    {
        return initialized_ && app::hasAppFacade();
    }

    app::AppConfig& getConfig() override
    {
        return config_;
    }

    const app::AppConfig& getConfig() const override
    {
        return config_;
    }

    void saveConfig() override {}

    void applyMeshConfig() override
    {
        if (mesh_router_)
        {
            mesh_router_->applyConfig(config_.activeMeshConfig());
        }
    }

    void applyUserInfo() override
    {
        if (mesh_router_)
        {
            mesh_router_->setUserInfo(config_.node_name, config_.short_name);
        }
    }

    void applyPositionConfig() override {}
    void applyNetworkLimits() override {}
    void applyPrivacyConfig() override {}

    void applyChatDefaults() override
    {
        if (!chat_service_)
        {
            return;
        }

        const chat::ChannelId channel = (config_.chat_channel == 1)
                                            ? chat::ChannelId::SECONDARY
                                            : chat::ChannelId::PRIMARY;
        chat_service_->switchChannel(channel);
    }

    void getEffectiveUserInfo(char* out_long, std::size_t long_len,
                              char* out_short, std::size_t short_len) const override
    {
        copyString(config_.node_name, out_long, long_len);
        copyString(config_.short_name, out_short, short_len);
    }

    bool switchMeshProtocol(chat::MeshProtocol protocol, bool persist = true) override
    {
        (void)persist;
        config_.mesh_protocol = protocol;
        if (chat_service_)
        {
            chat_service_->setActiveProtocol(protocol);
        }
        installMeshBackend(protocol);
        if (mesh_router_)
        {
            self_node_id_ = mesh_router_->getNodeId();
        }
        if (team_pairing_)
        {
            team_pairing_->setIdentity(self_node_id_, config_.short_name);
        }
        applyMeshConfig();
        return true;
    }

    chat::ChatService& getChatService() override
    {
        return *chat_service_;
    }

    chat::contacts::ContactService& getContactService() override
    {
        return *contact_service_;
    }

    chat::IMeshAdapter* getMeshAdapter() override
    {
        return mesh_router_.get();
    }

    const chat::IMeshAdapter* getMeshAdapter() const override
    {
        return mesh_router_.get();
    }

    chat::NodeId getSelfNodeId() const override
    {
        return self_node_id_;
    }

    team::TeamController* getTeamController() override
    {
        return team_controller_.get();
    }

    team::TeamPairingService* getTeamPairing() override
    {
        return team_pairing_.get();
    }

    team::TeamService* getTeamService() override
    {
        return team_service_.get();
    }

    const team::TeamService* getTeamService() const override
    {
        return team_service_.get();
    }

    team::TeamTrackSampler* getTeamTrackSampler() override
    {
        return nullptr;
    }

    void setTeamModeActive(bool active) override
    {
        (void)active;
    }

    void broadcastNodeInfo() override
    {
        if (!mesh_router_)
        {
            return;
        }

        if (meshtastic_adapter_)
        {
            (void)meshtastic_adapter_->broadcastNodeInfo();
        }
    }

    void clearNodeDb() override
    {
        if (node_store_)
        {
            node_store_->clear();
        }
        if (contact_service_)
        {
            contact_service_->clearCache();
        }
    }

    void clearMessageDb() override
    {
        if (chat_service_)
        {
            chat_service_->clearAllMessages();
        }
    }

    ble::BleManager* getBleManager() override
    {
        return nullptr;
    }

    const ble::BleManager* getBleManager() const override
    {
        return nullptr;
    }

    bool isBleEnabled() const override
    {
        return ble_enabled_;
    }

    void setBleEnabled(bool enabled) override
    {
        ble_enabled_ = enabled;
    }

    void restartDevice() override
    {
        esp_restart();
    }

    chat::ui::IChatUiRuntime* getChatUiRuntime() override
    {
        return chat_ui_runtime_;
    }

    void setChatUiRuntime(chat::ui::IChatUiRuntime* runtime) override
    {
        chat_ui_runtime_ = runtime;
    }

    BoardBase* getBoard() override
    {
        return &::boards::tab5::Tab5Board::instance();
    }

    const BoardBase* getBoard() const override
    {
        return &::boards::tab5::Tab5Board::instance();
    }

    void updateCoreServices() override
    {
        tick();
    }

    void tickEventRuntime() override {}

    void dispatchPendingEvents(std::size_t max_events = 32) override
    {
        sys::Event* event = nullptr;
        for (std::size_t processed = 0;
             processed < max_events && sys::EventBus::subscribe(&event, 0);)
        {
            if (!event)
            {
                continue;
            }
            ++processed;

            switch (event->type)
            {
            case sys::EventType::ChatSendResult:
            {
                auto* result_event = static_cast<sys::ChatSendResultEvent*>(event);
                if (chat_service_)
                {
                    chat_service_->handleSendResult(result_event->msg_id, result_event->success);
                }
                delete event;
                continue;
            }
            case sys::EventType::NodeInfoUpdate:
            {
                auto* node_event = static_cast<sys::NodeInfoUpdateEvent*>(event);
                contact_service_->updateNodeInfo(node_event->node_id,
                                                 node_event->short_name,
                                                 node_event->long_name,
                                                 node_event->snr,
                                                 node_event->rssi,
                                                 node_event->timestamp,
                                                 node_event->protocol,
                                                 node_event->role,
                                                 node_event->hops_away,
                                                 node_event->hw_model,
                                                 node_event->channel);
                delete event;
                continue;
            }
            case sys::EventType::NodeProtocolUpdate:
            {
                auto* node_event = static_cast<sys::NodeProtocolUpdateEvent*>(event);
                contact_service_->updateNodeProtocol(node_event->node_id,
                                                     node_event->protocol,
                                                     node_event->timestamp);
                delete event;
                continue;
            }
            case sys::EventType::NodePositionUpdate:
            {
                auto* pos_event = static_cast<sys::NodePositionUpdateEvent*>(event);
                chat::contacts::NodePosition pos{};
                pos.valid = true;
                pos.latitude_i = pos_event->latitude_i;
                pos.longitude_i = pos_event->longitude_i;
                pos.has_altitude = pos_event->has_altitude;
                pos.altitude = pos_event->altitude;
                pos.timestamp = pos_event->timestamp;
                pos.precision_bits = pos_event->precision_bits;
                pos.pdop = pos_event->pdop;
                pos.hdop = pos_event->hdop;
                pos.vdop = pos_event->vdop;
                pos.gps_accuracy_mm = pos_event->gps_accuracy_mm;
                contact_service_->updateNodePosition(pos_event->node_id, pos);
                delete event;
                continue;
            }
            case sys::EventType::TeamKick:
            case sys::EventType::TeamTransferLeader:
            case sys::EventType::TeamKeyDist:
            case sys::EventType::TeamStatus:
            case sys::EventType::TeamPosition:
            case sys::EventType::TeamWaypoint:
            case sys::EventType::TeamTrack:
            case sys::EventType::TeamChat:
            case sys::EventType::TeamPairing:
            case sys::EventType::TeamError:
            case sys::EventType::SystemTick:
                team::ui::shell::handle_event(nullptr, event);
                delete event;
                continue;
            default:
                break;
            }

            if (chat_ui_runtime_)
            {
                chat_ui_runtime_->onChatEvent(event);
                continue;
            }

            delete event;
        }
    }

  private:
    void clearLegacyDemoState()
    {
        team::ui::TeamUiSnapshot snapshot{};
        if (!team::ui::team_ui_get_store().load(snapshot))
        {
            return;
        }

        static constexpr team::TeamId kLegacyDemoTeamId = {0x54, 0x4D, 0x35, 0x2D, 0x44, 0x45, 0x4D, 0x4F};
        const bool looks_like_demo = snapshot.team_name == "Tab5 Demo" ||
                                     (snapshot.has_team_id && snapshot.team_id == kLegacyDemoTeamId);
        if (looks_like_demo)
        {
            team::ui::team_ui_get_store().clear();
        }
    }

    void syncTeamKeysFromStore()
    {
        if (!team_controller_)
        {
            return;
        }

        team::ui::TeamUiSnapshot snapshot{};
        const bool has_snapshot = team::ui::team_ui_get_store().load(snapshot);
        const bool has_keys = has_snapshot && snapshot.has_team_id && snapshot.has_team_psk && snapshot.security_round != 0;
        if (!has_keys)
        {
            if (team_keys_synced_)
            {
                team_controller_->clearKeys();
            }
            team_keys_synced_ = false;
            synced_team_key_id_ = 0;
            synced_team_id_ = {};
            synced_team_psk_.fill(0);
            return;
        }

        if (team_keys_synced_ &&
            synced_team_key_id_ == snapshot.security_round &&
            synced_team_id_ == snapshot.team_id &&
            synced_team_psk_ == snapshot.team_psk)
        {
            return;
        }

        if (team_controller_->setKeysFromPsk(snapshot.team_id,
                                             snapshot.security_round,
                                             snapshot.team_psk.data(),
                                             snapshot.team_psk.size()))
        {
            synced_team_id_ = snapshot.team_id;
            synced_team_key_id_ = snapshot.security_round;
            synced_team_psk_ = snapshot.team_psk;
            team_keys_synced_ = true;
            return;
        }

        team_keys_synced_ = false;
    }

    static void copyString(const char* src, char* dst, size_t dst_len)
    {
        if (!dst || dst_len == 0)
        {
            return;
        }

        if (!src)
        {
            dst[0] = '\0';
            return;
        }

        const size_t copy_len = std::min(std::strlen(src), dst_len - 1);
        std::memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }

    static chat::NodeId makeNodeId(const char* target_name)
    {
        constexpr uint32_t kFnvOffset = 2166136261u;
        constexpr uint32_t kFnvPrime = 16777619u;

        uint32_t hash = kFnvOffset;
        const char* text = target_name ? target_name : "idf";
        while (*text)
        {
            hash ^= static_cast<uint8_t>(*text++);
            hash *= kFnvPrime;
        }

        return hash != 0 ? hash : 0x00FDFD01u;
    }

    static void makeShortName(const char* target_name, char* out, size_t out_len)
    {
        if (!out || out_len == 0)
        {
            return;
        }

        size_t pos = 0;
        const char* text = target_name ? target_name : "IDF";
        while (*text && pos + 1 < out_len)
        {
            const unsigned char ch = static_cast<unsigned char>(*text++);
            if (!std::isalnum(ch))
            {
                continue;
            }
            out[pos++] = static_cast<char>(std::toupper(ch));
        }

        if (pos == 0)
        {
            copyString("IDF", out, out_len);
            return;
        }
        out[pos] = '\0';
    }

    void configureIdentity(const RuntimeConfig& runtime_config)
    {
        const char* target_name = runtime_config.target_name ? runtime_config.target_name : "IDF";
        std::snprintf(config_.node_name, sizeof(config_.node_name), "%s",
                      ::boards::tab5::Tab5Board::defaultLongName());
        std::snprintf(config_.short_name, sizeof(config_.short_name), "%s",
                      ::boards::tab5::Tab5Board::defaultShortName());
        if (target_name && target_name[0] != '\0' &&
            std::strcmp(target_name, "tab5") != 0 &&
            std::strcmp(target_name, "TAB5") != 0)
        {
            makeShortName(target_name, config_.short_name, sizeof(config_.short_name));
        }
        config_.mesh_protocol = chat::MeshProtocol::Meshtastic;
        config_.chat_channel = 0;
    }

    void installMeshBackend(chat::MeshProtocol protocol)
    {
        if (!mesh_router_)
        {
            return;
        }

        loopback_adapter_ = nullptr;
        meshtastic_adapter_ = nullptr;
        if (protocol == chat::MeshProtocol::Meshtastic)
        {
            auto backend = std::make_unique<apps::esp_idf::MeshtasticRadioAdapter>(
                ::boards::tab5::Tab5Board::instance());
            meshtastic_adapter_ = backend.get();
            (void)mesh_router_->installBackend(protocol, std::move(backend));
            return;
        }

        auto backend = std::make_unique<LoopbackMeshAdapter>(self_node_id_);
        loopback_adapter_ = backend.get();
        (void)mesh_router_->installBackend(protocol, std::move(backend));
    }

    void seedDemoData()
    {
        if (!contact_service_ || !chat_store_)
        {
            return;
        }

        const uint32_t now = sys::epoch_seconds_now();
        const uint32_t alpha = 0xA17A0001u;
        const uint32_t bravo = 0xB22B0002u;

        contact_service_->updateNodeInfo(alpha,
                                         "ALPHA",
                                         "Alpha Scout",
                                         7.5f,
                                         -62.0f,
                                         now,
                                         static_cast<uint8_t>(config_.mesh_protocol));
        contact_service_->updateNodeInfo(bravo,
                                         "BRAVO",
                                         "Bravo Relay",
                                         5.0f,
                                         -71.0f,
                                         now,
                                         static_cast<uint8_t>(config_.mesh_protocol));
        (void)contact_service_->addContact(alpha, "Alpha");
        (void)contact_service_->addContact(bravo, "Bravo");

        appendSeedMessage(config_.mesh_protocol,
                          chat::ChannelId::PRIMARY,
                          alpha,
                          0,
                          1,
                          now > 180 ? now - 180 : now,
                          "Tab5 shared startup/menu shell is online.",
                          chat::MessageStatus::Incoming);

        appendSeedMessage(config_.mesh_protocol,
                          chat::ChannelId::PRIMARY,
                          bravo,
                          bravo,
                          2,
                          now > 120 ? now - 120 : now,
                          "Direct chat runtime is now driven by the IDF facade.",
                          chat::MessageStatus::Incoming);

        appendSeedMessage(config_.mesh_protocol,
                          chat::ChannelId::PRIMARY,
                          0,
                          bravo,
                          3,
                          now > 60 ? now - 60 : now,
                          "Main menu -> shared Chat page is ready for migration.",
                          chat::MessageStatus::Sent);

        seedTeamUiSnapshot(now, alpha, bravo);
    }

    void seedTeamUiSnapshot(uint32_t now, uint32_t alpha, uint32_t bravo)
    {
        team::ui::TeamUiSnapshot snapshot{};
        snapshot.in_team = true;
        snapshot.self_is_leader = true;
        snapshot.last_event_seq = 1;
        snapshot.team_chat_unread = 1;
        snapshot.has_team_id = true;
        snapshot.team_id = {0x54, 0x4D, 0x35, 0x2D, 0x44, 0x45, 0x4D, 0x4F};
        snapshot.team_name = "Tab5 Demo";
        snapshot.security_round = 1;
        snapshot.last_update_s = now;
        for (size_t i = 0; i < snapshot.team_psk.size(); ++i)
        {
            snapshot.team_psk[i] = static_cast<uint8_t>(0x30 + (i & 0x3F));
        }
        snapshot.has_team_psk = true;

        team::ui::TeamMemberUi self{};
        self.node_id = self_node_id_;
        self.name = config_.short_name[0] ? config_.short_name : "You";
        self.online = true;
        self.leader = true;
        self.last_seen_s = now;
        self.color_index = team::ui::team_color_index_from_node_id(self.node_id);

        team::ui::TeamMemberUi member_alpha{};
        member_alpha.node_id = alpha;
        member_alpha.name = "Alpha";
        member_alpha.online = true;
        member_alpha.leader = false;
        member_alpha.last_seen_s = now > 45 ? now - 45 : now;
        member_alpha.color_index = team::ui::team_color_index_from_node_id(member_alpha.node_id);

        team::ui::TeamMemberUi member_bravo{};
        member_bravo.node_id = bravo;
        member_bravo.name = "Bravo";
        member_bravo.online = true;
        member_bravo.leader = false;
        member_bravo.last_seen_s = now > 90 ? now - 90 : now;
        member_bravo.color_index = team::ui::team_color_index_from_node_id(member_bravo.node_id);

        snapshot.members.push_back(self);
        snapshot.members.push_back(member_alpha);
        snapshot.members.push_back(member_bravo);

        team::ui::team_ui_get_store().save(snapshot);
        (void)team::ui::team_ui_chatlog_append(snapshot.team_id, alpha, true, now > 75 ? now - 75 : now, "Team demo channel ready.");
        (void)team::ui::team_ui_chatlog_append(snapshot.team_id, bravo, true, now > 30 ? now - 30 : now, "Shared Team page is now running on IDF.");
    }

    void appendSeedMessage(chat::MeshProtocol protocol,
                           chat::ChannelId channel,
                           chat::NodeId from,
                           chat::NodeId peer,
                           chat::MessageId msg_id,
                           uint32_t timestamp,
                           const char* text,
                           chat::MessageStatus status)
    {
        chat::ChatMessage message{};
        message.protocol = protocol;
        message.channel = channel;
        message.from = from;
        message.peer = peer;
        message.msg_id = msg_id;
        message.timestamp = timestamp;
        message.text = text ? text : "";
        message.status = status;
        chat_store_->append(message);
    }

    bool initialized_ = false;
    bool ble_enabled_ = false;
    chat::NodeId self_node_id_ = 0;
    RuntimeConfig runtime_config_{};
    app::AppConfig config_{};
    chat::ui::IChatUiRuntime* chat_ui_runtime_ = nullptr;

    std::unique_ptr<InMemoryNodeBlobStore> node_blob_store_;
    std::unique_ptr<InMemoryContactBlobStore> contact_blob_store_;
    std::unique_ptr<chat::contacts::NodeStoreCore> node_store_;
    std::unique_ptr<chat::contacts::ContactStoreCore> contact_store_;
    std::unique_ptr<chat::contacts::ContactService> contact_service_;

    std::unique_ptr<chat::ChatModel> chat_model_;
    std::unique_ptr<chat::RamStore> chat_store_;
    std::unique_ptr<chat::MeshAdapterRouterCore> mesh_router_;
    LoopbackMeshAdapter* loopback_adapter_ = nullptr;
    apps::esp_idf::MeshtasticRadioAdapter* meshtastic_adapter_ = nullptr;
    std::unique_ptr<chat::ChatService> chat_service_;

    std::unique_ptr<MinimalTeamRuntime> team_runtime_;
    std::unique_ptr<MinimalTeamCrypto> team_crypto_;
    std::unique_ptr<MinimalTeamEventSink> team_event_sink_;
    std::unique_ptr<team::TeamService> team_service_;
    std::unique_ptr<team::TeamController> team_controller_;
    std::unique_ptr<MinimalTeamPairingService> team_pairing_;
    team::TeamId synced_team_id_{};
    std::array<uint8_t, team::proto::kTeamChannelPskSize> synced_team_psk_{};
    uint32_t synced_team_key_id_ = 0;
    bool team_keys_synced_ = false;
};

MinimalAppFacade s_facade;

} // namespace

bool initialize(const RuntimeConfig& config)
{
    const bool ok = s_facade.initialize(config);
    if (ok)
    {
        ESP_LOGI(config.log_tag,
                 "%s minimal shared AppFacade bound for Chat/Contacts/Team runtime",
                 config.target_name);
    }
    return ok;
}

void tick()
{
    s_facade.tick();
}

bool isBound()
{
    return s_facade.isBound();
}

} // namespace apps::esp_idf::app_facade_runtime
