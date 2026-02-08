#include "team_pairing_service.h"

#include "../../sys/event_bus.h"
#include "../domain/team_events.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include <cstring>

namespace team
{
namespace
{
constexpr uint8_t kPairingMagic0 = 'T';
constexpr uint8_t kPairingMagic1 = 'M';
constexpr uint8_t kPairingVersion = 1;
constexpr uint8_t kMaxTeamNameLen = 15;
constexpr uint8_t kPairingChannel = 1;
constexpr uint32_t kLeaderWindowMs = 120000;
constexpr uint32_t kMemberTimeoutMs = 30000;
constexpr uint32_t kBeaconIntervalMs = 600;
constexpr uint32_t kJoinRetryMs = 1500;
constexpr uint8_t kJoinRetryMax = 6;
constexpr uint32_t kJoinSentHoldMs = 800;

enum class PairingMsgType : uint8_t
{
    Beacon = 1,
    Join = 2,
    Key = 3
};

uint32_t read_u32_le(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void write_u32_le(uint8_t* out, uint32_t v)
{
    out[0] = static_cast<uint8_t>(v & 0xFF);
    out[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    out[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void format_mac(const uint8_t* mac, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (!mac)
    {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

uint32_t node_id_from_mac(const uint8_t* mac)
{
    if (!mac)
    {
        return 0;
    }
    return (static_cast<uint32_t>(mac[2]) << 24) |
           (static_cast<uint32_t>(mac[3]) << 16) |
           (static_cast<uint32_t>(mac[4]) << 8) |
           static_cast<uint32_t>(mac[5]);
}

void log_local_mac()
{
    uint8_t mac[6] = {0};
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err != ESP_OK)
    {
        Serial.printf("[Pairing] get mac err=%d (%s)\n",
                      static_cast<int>(err),
                      esp_err_to_name(err));
        return;
    }
    char mac_str[20];
    format_mac(mac, mac_str, sizeof(mac_str));
    Serial.printf("[Pairing] local sta mac=%s node_id=%08lX\n",
                  mac_str,
                  static_cast<unsigned long>(node_id_from_mac(mac)));
}

bool init_wifi_stack()
{
    static bool netif_ready = false;
    if (!netif_ready)
    {
        esp_err_t netif_err = esp_netif_init();
        if (netif_err != ESP_OK && netif_err != ESP_ERR_INVALID_STATE)
        {
            Serial.printf("[Pairing] netif init err=%d (%s)\n",
                          static_cast<int>(netif_err),
                          esp_err_to_name(netif_err));
        }
        esp_err_t loop_err = esp_event_loop_create_default();
        if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE)
        {
            Serial.printf("[Pairing] event loop err=%d (%s)\n",
                          static_cast<int>(loop_err),
                          esp_err_to_name(loop_err));
        }
        esp_netif_t* sta = esp_netif_create_default_wifi_sta();
        if (!sta)
        {
            Serial.printf("[Pairing] netif create sta failed\n");
        }
        netif_ready = true;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t init_err = esp_wifi_init(&cfg);
    if (init_err != ESP_OK && init_err != ESP_ERR_WIFI_INIT_STATE)
    {
        Serial.printf("[Pairing] wifi init err=%d (%s)\n",
                      static_cast<int>(init_err),
                      esp_err_to_name(init_err));
        return false;
    }
    esp_err_t storage_err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (storage_err != ESP_OK)
    {
        Serial.printf("[Pairing] wifi storage err=%d (%s)\n",
                      static_cast<int>(storage_err),
                      esp_err_to_name(storage_err));
    }
    esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (mode_err != ESP_OK)
    {
        Serial.printf("[Pairing] wifi set mode err=%d (%s)\n",
                      static_cast<int>(mode_err),
                      esp_err_to_name(mode_err));
        return false;
    }
    esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_err != ESP_OK)
    {
        Serial.printf("[Pairing] wifi ps err=%d (%s)\n",
                      static_cast<int>(ps_err),
                      esp_err_to_name(ps_err));
    }
    esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_NOT_STOPPED)
    {
        Serial.printf("[Pairing] wifi start err=%d (%s)\n",
                      static_cast<int>(start_err),
                      esp_err_to_name(start_err));
        return false;
    }
    esp_err_t ch_err = esp_wifi_set_channel(kPairingChannel, WIFI_SECOND_CHAN_NONE);
    if (ch_err != ESP_OK)
    {
        Serial.printf("[Pairing] set channel failed err=%d (%s)\n",
                      static_cast<int>(ch_err),
                      esp_err_to_name(ch_err));
    }
    return true;
}

bool decode_header(const uint8_t* data, size_t len, PairingMsgType* out_type, size_t* out_off)
{
    if (!data || len < 4)
    {
        return false;
    }
    if (data[0] != kPairingMagic0 || data[1] != kPairingMagic1)
    {
        return false;
    }
    if (data[2] != kPairingVersion)
    {
        return false;
    }
    if (out_type)
    {
        *out_type = static_cast<PairingMsgType>(data[3]);
    }
    if (out_off)
    {
        *out_off = 4;
    }
    return true;
}

void fill_team_id(TeamId& out, const uint8_t* data)
{
    for (size_t i = 0; i < out.size(); ++i)
    {
        out[i] = data[i];
    }
}

} // namespace

TeamPairingService* TeamPairingService::instance_ = nullptr;

TeamPairingService::TeamPairingService()
{
    team_psk_.fill(0);
}

bool TeamPairingService::ensureInit()
{
    if (initialized_)
    {
        return true;
    }
    bool init_ok = init_wifi_stack();
    if (!init_ok)
    {
        return false;
    }

    esp_err_t now_err = esp_now_init();
    if (now_err != ESP_OK)
    {
        Serial.printf("[Pairing] esp_now init failed err=%d (%s)\n",
                      static_cast<int>(now_err),
                      esp_err_to_name(now_err));
        return false;
    }
    esp_now_register_recv_cb([](const uint8_t* mac, const uint8_t* data, int len)
                             {
        if (instance_)
        {
            instance_->handleRx(mac, data, static_cast<size_t>(len));
        } });
    initialized_ = true;
    instance_ = this;
    return true;
}

void TeamPairingService::shutdown()
{
    if (!initialized_)
    {
        return;
    }
    esp_now_deinit();
    esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK)
    {
        Serial.printf("[Pairing] wifi stop err=%d (%s)\n",
                      static_cast<int>(stop_err),
                      esp_err_to_name(stop_err));
    }
    initialized_ = false;
    leader_mac_valid_ = false;
    rx_pending_ = false;
}

void TeamPairingService::copyTeamName(const char* name)
{
    if (!name || name[0] == '\0')
    {
        team_name_[0] = '\0';
        has_team_name_ = false;
        return;
    }
    strncpy(team_name_, name, sizeof(team_name_) - 1);
    team_name_[sizeof(team_name_) - 1] = '\0';
    has_team_name_ = true;
}

void TeamPairingService::setState(TeamPairingState state, uint32_t peer_id)
{
    state_ = state;
    state_since_ms_ = millis();
    Serial.printf("[Pairing] state=%u role=%u peer=%08lX\n",
                  static_cast<unsigned>(state_),
                  static_cast<unsigned>(role_),
                  static_cast<unsigned long>(peer_id));
    publishState(peer_id);
}

void TeamPairingService::publishState(uint32_t peer_id) const
{
    team::TeamPairingEvent ev{};
    ev.role = role_;
    ev.state = state_;
    ev.peer_id = peer_id;
    ev.key_id = key_id_;
    if (has_team_id_)
    {
        ev.team_id = team_id_;
        ev.has_team_id = true;
    }
    if (has_team_name_)
    {
        strncpy(ev.team_name, team_name_, sizeof(ev.team_name) - 1);
        ev.team_name[sizeof(ev.team_name) - 1] = '\0';
        ev.has_team_name = true;
    }
    sys::EventBus::publish(new sys::TeamPairingEvent(ev), 0);
}

bool TeamPairingService::ensurePeer(const uint8_t* mac)
{
    if (!mac)
    {
        return false;
    }
    if (esp_now_is_peer_exist(mac))
    {
        return true;
    }
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = kPairingChannel;
    peer.encrypt = false;
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST)
    {
        Serial.printf("[TeamPairing] add peer failed err=%d\n", static_cast<int>(err));
        return false;
    }
    return true;
}

bool TeamPairingService::startLeader(const TeamId& team_id,
                                     uint32_t key_id,
                                     const uint8_t* psk,
                                     size_t psk_len,
                                     uint32_t leader_id,
                                     const char* team_name)
{
    if (!ensureInit())
    {
        return false;
    }
    if (!psk || psk_len == 0)
    {
        return false;
    }
    log_local_mac();
    Serial.printf("[Pairing] start leader id=%08lX key_id=%lu\n",
                  static_cast<unsigned long>(leader_id),
                  static_cast<unsigned long>(key_id));
    role_ = TeamPairingRole::Leader;
    team_id_ = team_id;
    has_team_id_ = true;
    key_id_ = key_id;
    leader_id_ = leader_id;
    copyTeamName(team_name);

    team_psk_.fill(0);
    team_psk_len_ = static_cast<uint8_t>(psk_len > team_psk_.size() ? team_psk_.size() : psk_len);
    if (psk && team_psk_len_ > 0)
    {
        memcpy(team_psk_.data(), psk, team_psk_len_);
    }

    active_until_ms_ = millis() + kLeaderWindowMs;
    last_beacon_ms_ = 0;
    join_attempts_ = 0;
    join_sent_ms_ = 0;
    setState(TeamPairingState::LeaderBeacon, 0);
    return true;
}

bool TeamPairingService::startMember(uint32_t self_id)
{
    if (!ensureInit())
    {
        return false;
    }
    log_local_mac();
    Serial.printf("[Pairing] start member self=%08lX\n",
                  static_cast<unsigned long>(self_id));
    role_ = TeamPairingRole::Member;
    state_ = TeamPairingState::MemberScanning;
    state_since_ms_ = millis();
    member_id_ = self_id;
    leader_id_ = 0;
    leader_mac_valid_ = false;
    has_team_id_ = false;
    key_id_ = 0;
    copyTeamName(nullptr);
    join_nonce_ = static_cast<uint32_t>(random(0, 0x7fffffff));
    active_until_ms_ = millis() + kMemberTimeoutMs;
    last_join_ms_ = 0;
    join_attempts_ = 0;
    join_sent_ms_ = 0;
    publishState(0);
    return true;
}

void TeamPairingService::stop()
{
    role_ = TeamPairingRole::None;
    state_ = TeamPairingState::Idle;
    state_since_ms_ = millis();
    Serial.printf("[Pairing] stop\n");
    publishState(0);
    shutdown();
}

TeamPairingStatus TeamPairingService::getStatus() const
{
    TeamPairingStatus status{};
    status.role = role_;
    status.state = state_;
    status.team_id = team_id_;
    status.has_team_id = has_team_id_;
    status.key_id = key_id_;
    status.peer_id = 0;
    if (has_team_name_)
    {
        strncpy(status.team_name, team_name_, sizeof(status.team_name) - 1);
        status.team_name[sizeof(status.team_name) - 1] = '\0';
        status.has_team_name = true;
    }
    return status;
}

void TeamPairingService::handleRx(const uint8_t* mac, const uint8_t* data, size_t len)
{
    if (!mac || !data || len == 0 || len > sizeof(rx_packet_.data))
    {
        return;
    }
    portENTER_CRITICAL(&rx_mux_);
    if (!rx_pending_)
    {
        memcpy(rx_packet_.mac, mac, 6);
        memcpy(rx_packet_.data, data, len);
        rx_packet_.len = len;
        rx_pending_ = true;
    }
    portEXIT_CRITICAL(&rx_mux_);
}

void TeamPairingService::handleBeacon(const uint8_t* mac, const uint8_t* data, size_t len)
{
    if (role_ != TeamPairingRole::Member ||
        (state_ != TeamPairingState::MemberScanning && state_ != TeamPairingState::JoinSent &&
         state_ != TeamPairingState::WaitingKey))
    {
        return;
    }
    if (len < 4 + 8 + 4 + 4 + 4 + 1)
    {
        return;
    }
    size_t off = 4;
    TeamId rx_id{};
    fill_team_id(rx_id, data + off);
    off += rx_id.size();
    uint32_t key_id = read_u32_le(data + off);
    off += 4;
    uint32_t leader_id = read_u32_le(data + off);
    off += 4;
    uint32_t expires_at = read_u32_le(data + off);
    off += 4;
    uint8_t name_len = data[off++];
    if (name_len > kMaxTeamNameLen)
    {
        name_len = kMaxTeamNameLen;
    }
    if (off + name_len > len)
    {
        return;
    }

    (void)expires_at;
    uint32_t now = millis();
    Serial.printf("[Pairing] beacon rx leader=%08lX key_id=%lu len=%u\n",
                  static_cast<unsigned long>(leader_id),
                  static_cast<unsigned long>(key_id),
                  static_cast<unsigned>(len));
    team_id_ = rx_id;
    has_team_id_ = true;
    key_id_ = key_id;
    leader_id_ = leader_id;
    if (name_len > 0)
    {
        char name_buf[16] = {0};
        memcpy(name_buf, data + off, name_len);
        name_buf[name_len] = '\0';
        copyTeamName(name_buf);
    }

    memcpy(leader_mac_, mac, 6);
    leader_mac_valid_ = true;
    join_nonce_ = static_cast<uint32_t>(random(0, 0x7fffffff));
    join_attempts_ = 0;
    sendJoin();
    join_sent_ms_ = now;
    setState(TeamPairingState::JoinSent, 0);
}

void TeamPairingService::handleJoin(const uint8_t* mac, const uint8_t* data, size_t len)
{
    if (role_ != TeamPairingRole::Leader || state_ != TeamPairingState::LeaderBeacon)
    {
        return;
    }
    if (len < 4 + 8 + 4 + 4)
    {
        return;
    }
    size_t off = 4;
    TeamId rx_id{};
    fill_team_id(rx_id, data + off);
    off += rx_id.size();
    uint32_t member_id = read_u32_le(data + off);
    off += 4;
    uint32_t nonce = read_u32_le(data + off);

    if (!has_team_id_ || rx_id != team_id_)
    {
        return;
    }
    Serial.printf("[Pairing] join rx member=%08lX mac_id=%08lX nonce=%lu\n",
                  static_cast<unsigned long>(member_id),
                  static_cast<unsigned long>(node_id_from_mac(mac)),
                  static_cast<unsigned long>(nonce));
    bool ok = sendKey(mac, member_id, nonce);
    if (ok)
    {
        publishState(member_id);
    }
    else
    {
        Serial.printf("[Pairing] key tx failed, skip member\n");
    }
}

void TeamPairingService::handleKey(const uint8_t* mac, const uint8_t* data, size_t len)
{
    (void)mac;
    if (role_ != TeamPairingRole::Member ||
        (state_ != TeamPairingState::WaitingKey && state_ != TeamPairingState::JoinSent))
    {
        return;
    }
    if (len < 4 + 8 + 4 + 4 + 1)
    {
        return;
    }
    size_t off = 4;
    TeamId rx_id{};
    fill_team_id(rx_id, data + off);
    off += rx_id.size();
    uint32_t key_id = read_u32_le(data + off);
    off += 4;
    uint32_t nonce = read_u32_le(data + off);
    off += 4;
    uint8_t psk_len = data[off++];
    if (psk_len > team_psk_.size())
    {
        return;
    }
    if (off + psk_len > len)
    {
        return;
    }

    if (!has_team_id_ || rx_id != team_id_ || nonce != join_nonce_)
    {
        return;
    }
    Serial.printf("[Pairing] key rx leader=%08lX key_id=%lu psk_len=%u\n",
                  static_cast<unsigned long>(leader_id_),
                  static_cast<unsigned long>(key_id),
                  static_cast<unsigned>(psk_len));
    team_psk_.fill(0);
    if (psk_len > 0)
    {
        memcpy(team_psk_.data(), data + off, psk_len);
    }
    team_psk_len_ = psk_len;
    key_id_ = key_id;

    team::proto::TeamKeyDist msg{};
    msg.team_id = team_id_;
    msg.key_id = key_id_;
    msg.channel_psk_len = team_psk_len_;
    msg.channel_psk = team_psk_;

    team::TeamKeyDistEvent ev{};
    ev.ctx.team_id = team_id_;
    ev.ctx.key_id = key_id_;
    ev.ctx.from = leader_id_;
    ev.ctx.timestamp = millis() / 1000;
    ev.msg = msg;
    sys::EventBus::publish(new sys::TeamKeyDistEvent(ev), 0);

    setState(TeamPairingState::Completed, leader_id_);
    stop();
}

void TeamPairingService::sendBeacon()
{
    uint8_t buf[64];
    size_t off = 0;
    buf[off++] = kPairingMagic0;
    buf[off++] = kPairingMagic1;
    buf[off++] = kPairingVersion;
    buf[off++] = static_cast<uint8_t>(PairingMsgType::Beacon);
    memcpy(buf + off, team_id_.data(), team_id_.size());
    off += team_id_.size();
    write_u32_le(buf + off, key_id_);
    off += 4;
    write_u32_le(buf + off, leader_id_);
    off += 4;
    write_u32_le(buf + off, kLeaderWindowMs);
    off += 4;
    uint8_t name_len = 0;
    if (has_team_name_)
    {
        name_len = static_cast<uint8_t>(strnlen(team_name_, kMaxTeamNameLen));
    }
    buf[off++] = name_len;
    if (name_len > 0)
    {
        memcpy(buf + off, team_name_, name_len);
        off += name_len;
    }

    static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ensurePeer(broadcast_mac);
    esp_err_t err = esp_now_send(broadcast_mac, buf, off);
    last_beacon_ms_ = millis();
    Serial.printf("[Pairing] beacon tx len=%u key_id=%lu err=%d\n",
                  static_cast<unsigned>(off),
                  static_cast<unsigned long>(key_id_),
                  static_cast<int>(err));
}

void TeamPairingService::sendJoin()
{
    if (!leader_mac_valid_)
    {
        return;
    }
    uint8_t buf[32];
    size_t off = 0;
    buf[off++] = kPairingMagic0;
    buf[off++] = kPairingMagic1;
    buf[off++] = kPairingVersion;
    buf[off++] = static_cast<uint8_t>(PairingMsgType::Join);
    memcpy(buf + off, team_id_.data(), team_id_.size());
    off += team_id_.size();
    write_u32_le(buf + off, member_id_);
    off += 4;
    write_u32_le(buf + off, join_nonce_);
    off += 4;

    ensurePeer(leader_mac_);
    esp_err_t err = esp_now_send(leader_mac_, buf, off);
    last_join_ms_ = millis();
    Serial.printf("[Pairing] join tx to=%02X:%02X:%02X:%02X:%02X:%02X len=%u attempt=%u err=%d\n",
                  leader_mac_[0], leader_mac_[1], leader_mac_[2],
                  leader_mac_[3], leader_mac_[4], leader_mac_[5],
                  static_cast<unsigned>(off),
                  static_cast<unsigned>(join_attempts_ + 1),
                  static_cast<int>(err));
    if (join_attempts_ < 255)
    {
        join_attempts_++;
    }
}

bool TeamPairingService::sendKey(const uint8_t* mac, uint32_t member_id, uint32_t nonce)
{
    (void)member_id;
    if (!mac || team_psk_len_ == 0)
    {
        return false;
    }
    uint8_t buf[64];
    size_t off = 0;
    buf[off++] = kPairingMagic0;
    buf[off++] = kPairingMagic1;
    buf[off++] = kPairingVersion;
    buf[off++] = static_cast<uint8_t>(PairingMsgType::Key);
    memcpy(buf + off, team_id_.data(), team_id_.size());
    off += team_id_.size();
    write_u32_le(buf + off, key_id_);
    off += 4;
    write_u32_le(buf + off, nonce);
    off += 4;
    buf[off++] = team_psk_len_;
    memcpy(buf + off, team_psk_.data(), team_psk_len_);
    off += team_psk_len_;

    ensurePeer(mac);
    esp_err_t err = esp_now_send(mac, buf, off);
    Serial.printf("[Pairing] key tx to=%02X:%02X:%02X:%02X:%02X:%02X len=%u err=%d\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  static_cast<unsigned>(off),
                  static_cast<int>(err));
    return err == ESP_OK;
}

void TeamPairingService::update()
{
    if (!initialized_)
    {
        return;
    }
    RxPacket rx{};
    bool has_rx = false;
    portENTER_CRITICAL(&rx_mux_);
    if (rx_pending_)
    {
        rx = rx_packet_;
        rx_pending_ = false;
        has_rx = true;
    }
    portEXIT_CRITICAL(&rx_mux_);
    if (has_rx)
    {
        PairingMsgType type = PairingMsgType::Beacon;
        size_t off = 0;
        if (decode_header(rx.data, rx.len, &type, &off))
        {
            char mac_str[20];
            format_mac(rx.mac, mac_str, sizeof(mac_str));
            Serial.printf("[Pairing] rx type=%u len=%u from=%s role=%u state=%u\n",
                          static_cast<unsigned>(type),
                          static_cast<unsigned>(rx.len),
                          mac_str,
                          static_cast<unsigned>(role_),
                          static_cast<unsigned>(state_));
            switch (type)
            {
            case PairingMsgType::Beacon:
                handleBeacon(rx.mac, rx.data, rx.len);
                break;
            case PairingMsgType::Join:
                handleJoin(rx.mac, rx.data, rx.len);
                break;
            case PairingMsgType::Key:
                handleKey(rx.mac, rx.data, rx.len);
                break;
            default:
                break;
            }
        }
    }

    uint32_t now = millis();
    if (state_ == TeamPairingState::LeaderBeacon)
    {
        if (active_until_ms_ != 0 && now >= active_until_ms_)
        {
            Serial.printf("[Pairing] leader timeout\n");
            setState(TeamPairingState::Failed, 0);
            stop();
            return;
        }
        if (now - last_beacon_ms_ >= kBeaconIntervalMs)
        {
            sendBeacon();
        }
    }
    else if (state_ == TeamPairingState::MemberScanning)
    {
        if (active_until_ms_ != 0 && now >= active_until_ms_)
        {
            Serial.printf("[Pairing] member timeout\n");
            setState(TeamPairingState::Failed, 0);
            stop();
            return;
        }
    }
    else if (state_ == TeamPairingState::JoinSent)
    {
        if ((now - join_sent_ms_) >= kJoinSentHoldMs)
        {
            setState(TeamPairingState::WaitingKey, 0);
        }
        if (active_until_ms_ != 0 && now >= active_until_ms_)
        {
            Serial.printf("[Pairing] join timeout\n");
            setState(TeamPairingState::Failed, 0);
            stop();
            return;
        }
    }
    else if (state_ == TeamPairingState::WaitingKey)
    {
        if (active_until_ms_ != 0 && now >= active_until_ms_)
        {
            Serial.printf("[Pairing] key timeout\n");
            setState(TeamPairingState::Failed, 0);
            stop();
            return;
        }
        if (leader_mac_valid_ &&
            (now - last_join_ms_) >= kJoinRetryMs &&
            join_attempts_ < kJoinRetryMax)
        {
            sendJoin();
        }
    }
}

} // namespace team
