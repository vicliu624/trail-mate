#include "team_service.h"

#include "../../sys/event_bus.h"
#include "../domain/team_events.h"
#include "../protocol/team_mgmt.h"
#include "../protocol/team_portnum.h"
#include "../protocol/team_wire.h"
#include <Arduino.h>
#include <string>

namespace team
{
namespace
{

#define TEAM_LOG_ENABLE 1
#if TEAM_LOG_ENABLE
#define TEAM_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define TEAM_LOG(...)
#endif

std::string toHex(const uint8_t* data, size_t len, size_t max_len = 64)
{
    if (!data || len == 0)
    {
        return {};
    }
    size_t capped = (len > max_len) ? max_len : len;
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(capped * 2);
    for (size_t i = 0; i < capped; ++i)
    {
        uint8_t b = data[i];
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    if (capped < len)
    {
        out.append("..");
    }
    return out;
}

template <size_t N>
std::string hexFromArray(const std::array<uint8_t, N>& data)
{
    return toHex(data.data(), data.size(), data.size());
}

const char* mgmtTypeName(team::proto::TeamMgmtType type)
{
    switch (type)
    {
    case team::proto::TeamMgmtType::Advertise:
        return "Advertise";
    case team::proto::TeamMgmtType::JoinRequest:
        return "JoinRequest";
    case team::proto::TeamMgmtType::JoinAccept:
        return "JoinAccept";
    case team::proto::TeamMgmtType::JoinConfirm:
        return "JoinConfirm";
    case team::proto::TeamMgmtType::Status:
        return "Status";
    case team::proto::TeamMgmtType::Rotate:
        return "Rotate";
    case team::proto::TeamMgmtType::Leave:
        return "Leave";
    case team::proto::TeamMgmtType::Disband:
        return "Disband";
    case team::proto::TeamMgmtType::JoinDecision:
        return "JoinDecision";
    case team::proto::TeamMgmtType::Kick:
        return "Kick";
    case team::proto::TeamMgmtType::TransferLeader:
        return "TransferLeader";
    case team::proto::TeamMgmtType::KeyDist:
        return "KeyDist";
    default:
        return "Unknown";
    }
}

const char* teamPortName(uint32_t portnum)
{
    switch (portnum)
    {
    case team::proto::TEAM_MGMT_APP:
        return "TEAM_MGMT";
    case team::proto::TEAM_POSITION_APP:
        return "TEAM_POS";
    case team::proto::TEAM_WAYPOINT_APP:
        return "TEAM_WP";
    case team::proto::TEAM_TRACK_APP:
        return "TEAM_TRACK";
    case team::proto::TEAM_CHAT_APP:
        return "TEAM_CHAT";
    default:
        return "TEAM_OTHER";
    }
}

const char* teamErrorName(team::TeamProtocolError err)
{
    switch (err)
    {
    case team::TeamProtocolError::DecryptFail:
        return "DecryptFail";
    case team::TeamProtocolError::DecodeFail:
        return "DecodeFail";
    case team::TeamProtocolError::KeyMismatch:
        return "KeyMismatch";
    case team::TeamProtocolError::UnknownVersion:
        return "UnknownVersion";
    default:
        return "UnknownError";
    }
}

void logTeamEncrypted(const char* dir,
                      const chat::MeshIncomingData& data,
                      const team::proto::TeamEncrypted& envelope,
                      const std::vector<uint8_t>* plain,
                      const std::vector<uint8_t>* wire,
                      const char* result)
{
    std::string team_id_hex = hexFromArray(envelope.team_id);
    std::string nonce_hex = hexFromArray(envelope.nonce);
    std::string cipher_hex = toHex(envelope.ciphertext.data(),
                                   envelope.ciphertext.size(),
                                   envelope.ciphertext.size());
    TEAM_LOG("[TEAM] %s %s %s ver=%u flags=0x%02X key_id=%lu team_id=%s nonce=%s cipher_len=%u cipher_hex=%s\n",
             dir,
             teamPortName(data.portnum),
             result ? result : "result",
             envelope.version,
             envelope.aad_flags,
             static_cast<unsigned long>(envelope.key_id),
             team_id_hex.c_str(),
             nonce_hex.c_str(),
             static_cast<unsigned>(envelope.ciphertext.size()),
             cipher_hex.c_str());
    if (plain)
    {
        std::string plain_hex = toHex(plain->data(), plain->size(), plain->size());
        TEAM_LOG("[TEAM] %s %s plain_len=%u plain_hex=%s\n",
                 dir,
                 teamPortName(data.portnum),
                 static_cast<unsigned>(plain->size()),
                 plain_hex.c_str());
    }
    if (wire)
    {
        std::string wire_hex = toHex(wire->data(), wire->size(), wire->size());
        TEAM_LOG("[TEAM] %s %s wire_len=%u wire_hex=%s\n",
                 dir,
                 teamPortName(data.portnum),
                 static_cast<unsigned>(wire->size()),
                 wire_hex.c_str());
    }
}

void logTeamAdvertise(const team::proto::TeamAdvertise& msg, const char* dir)
{
    TEAM_LOG("[TEAM] %s Advertise team_id=%s has_join_hint=%u join_hint=0x%08lX has_channel_index=%u channel_index=%u has_expires_at=%u expires_at=%llu nonce=%llu\n",
             dir,
             hexFromArray(msg.team_id).c_str(),
             msg.has_join_hint ? 1 : 0,
             static_cast<unsigned long>(msg.join_hint),
             msg.has_channel_index ? 1 : 0,
             static_cast<unsigned>(msg.channel_index),
             msg.has_expires_at ? 1 : 0,
             static_cast<unsigned long long>(msg.expires_at),
             static_cast<unsigned long long>(msg.nonce));
}

void logTeamJoinRequest(const team::proto::TeamJoinRequest& msg, const char* dir)
{
    std::string pub_hex = toHex(msg.member_pub.data(), msg.member_pub_len, msg.member_pub_len);
    TEAM_LOG("[TEAM] %s JoinRequest team_id=%s has_pub=%u pub_len=%u pub_hex=%s has_cap=%u cap=0x%08lX nonce=%llu\n",
             dir,
             hexFromArray(msg.team_id).c_str(),
             msg.has_member_pub ? 1 : 0,
             static_cast<unsigned>(msg.member_pub_len),
             pub_hex.c_str(),
             msg.has_capabilities ? 1 : 0,
             static_cast<unsigned long>(msg.capabilities),
             static_cast<unsigned long long>(msg.nonce));
}

void logTeamJoinAccept(const team::proto::TeamJoinAccept& msg, const char* dir)
{
    std::string psk_hex = toHex(msg.channel_psk.data(), msg.channel_psk_len, msg.channel_psk_len);
    TEAM_LOG("[TEAM] %s JoinAccept has_team_id=%u team_id=%s channel_index=%u psk_len=%u psk_hex=%s key_id=%lu params_has=%u pos_ms=%lu precision=%u flags=0x%08lX\n",
             dir,
             msg.has_team_id ? 1 : 0,
             hexFromArray(msg.team_id).c_str(),
             static_cast<unsigned>(msg.channel_index),
             static_cast<unsigned>(msg.channel_psk_len),
             psk_hex.c_str(),
             static_cast<unsigned long>(msg.key_id),
             msg.params.has_params ? 1 : 0,
             static_cast<unsigned long>(msg.params.position_interval_ms),
             static_cast<unsigned>(msg.params.precision_level),
             static_cast<unsigned long>(msg.params.flags));
}

void logTeamJoinConfirm(const team::proto::TeamJoinConfirm& msg, const char* dir)
{
    TEAM_LOG("[TEAM] %s JoinConfirm ok=%u has_cap=%u cap=0x%08lX has_battery=%u battery=%u\n",
             dir,
             msg.ok ? 1 : 0,
             msg.has_capabilities ? 1 : 0,
             static_cast<unsigned long>(msg.capabilities),
             msg.has_battery ? 1 : 0,
             static_cast<unsigned>(msg.battery));
}

void logTeamJoinDecision(const team::proto::TeamJoinDecision& msg, const char* dir)
{
    TEAM_LOG("[TEAM] %s JoinDecision accept=%u has_reason=%u reason=%lu\n",
             dir,
             msg.accept ? 1 : 0,
             msg.has_reason ? 1 : 0,
             static_cast<unsigned long>(msg.reason));
}

void logTeamKick(const team::proto::TeamKick& msg, const char* dir)
{
    TEAM_LOG("[TEAM] %s Kick target=%08lX\n", dir, static_cast<unsigned long>(msg.target));
}

void logTeamTransferLeader(const team::proto::TeamTransferLeader& msg, const char* dir)
{
    TEAM_LOG("[TEAM] %s TransferLeader target=%08lX\n", dir, static_cast<unsigned long>(msg.target));
}

void logTeamKeyDist(const team::proto::TeamKeyDist& msg, const char* dir)
{
    std::string psk_hex = toHex(msg.channel_psk.data(), msg.channel_psk_len, msg.channel_psk_len);
    TEAM_LOG("[TEAM] %s KeyDist team_id=%s key_id=%lu psk_len=%u psk_hex=%s\n",
             dir,
             hexFromArray(msg.team_id).c_str(),
             static_cast<unsigned long>(msg.key_id),
             static_cast<unsigned>(msg.channel_psk_len),
             psk_hex.c_str());
}

void logTeamStatus(const team::proto::TeamStatus& msg, const char* dir)
{
    TEAM_LOG("[TEAM] %s Status key_id=%lu member_hash=%s params_has=%u pos_ms=%lu precision=%u flags=0x%08lX\n",
             dir,
             static_cast<unsigned long>(msg.key_id),
             hexFromArray(msg.member_list_hash).c_str(),
             msg.params.has_params ? 1 : 0,
             static_cast<unsigned long>(msg.params.position_interval_ms),
             static_cast<unsigned>(msg.params.precision_level),
             static_cast<unsigned long>(msg.params.flags));
}

std::vector<uint8_t> buildAad(const team::proto::TeamEncrypted& envelope)
{
    std::vector<uint8_t> aad;
    aad.reserve(1 + 1 + 4 + envelope.team_id.size());
    aad.push_back(envelope.version);
    aad.push_back(envelope.aad_flags);

    uint32_t key_id = envelope.key_id;
    for (int i = 0; i < 4; ++i)
    {
        aad.push_back(static_cast<uint8_t>((key_id >> (8 * i)) & 0xFF));
    }

    aad.insert(aad.end(), envelope.team_id.begin(), envelope.team_id.end());
    return aad;
}

team::TeamEventContext makeContext(const chat::MeshIncomingData& data,
                                   const team::proto::TeamEncrypted* envelope)
{
    team::TeamEventContext ctx;
    if (envelope)
    {
        ctx.team_id = envelope->team_id;
        ctx.key_id = envelope->key_id;
    }
    ctx.from = data.from;
    ctx.timestamp = millis() / 1000;
    return ctx;
}

team::TeamEventContext makeContext(const chat::MeshIncomingData& data,
                                   const team::TeamId& team_id)
{
    team::TeamEventContext ctx;
    ctx.team_id = team_id;
    ctx.key_id = 0;
    ctx.from = data.from;
    ctx.timestamp = millis() / 1000;
    return ctx;
}

void fillNonce(std::array<uint8_t, team::proto::kTeamNonceSize>& nonce)
{
    for (size_t i = 0; i < nonce.size(); ++i)
    {
        nonce[i] = static_cast<uint8_t>(random(0, 256));
    }
}

} // namespace

TeamService::TeamService(team::ITeamCrypto& crypto,
                         chat::IMeshAdapter& mesh,
                         team::ITeamEventSink& sink)
    : crypto_(crypto), mesh_(mesh), sink_(sink)
{
}

void TeamService::setKeys(const TeamKeys& keys)
{
    keys_ = keys;
    keys_.valid = true;
}

void TeamService::clearKeys()
{
    keys_ = TeamKeys{};
}

bool TeamService::setKeysFromPsk(const TeamId& team_id, uint32_t key_id,
                                 const uint8_t* psk, size_t psk_len)
{
    if (!psk || psk_len == 0)
    {
        return false;
    }

    TeamKeys keys{};
    keys.team_id = team_id;
    keys.key_id = key_id;

    if (!crypto_.deriveKey(psk, psk_len, "team_mgmt",
                           keys.mgmt_key.data(), keys.mgmt_key.size()))
    {
        return false;
    }
    if (!crypto_.deriveKey(psk, psk_len, "team_pos",
                           keys.pos_key.data(), keys.pos_key.size()))
    {
        return false;
    }
    if (!crypto_.deriveKey(psk, psk_len, "team_wp",
                           keys.wp_key.data(), keys.wp_key.size()))
    {
        return false;
    }
    if (!crypto_.deriveKey(psk, psk_len, "team_chat",
                           keys.chat_key.data(), keys.chat_key.size()))
    {
        return false;
    }
    keys.valid = true;
    keys_ = keys;
    return true;
}

void TeamService::processIncoming()
{
    chat::MeshIncomingData data;
    while (mesh_.pollIncomingData(&data))
    {
        if (data.portnum == team::proto::TEAM_MGMT_APP)
        {
            std::string rx_raw_hex = toHex(data.payload.data(), data.payload.size(), data.payload.size());
            TEAM_LOG("[TEAM] RX TEAM_MGMT raw from=%08lX len=%u hex=%s\n",
                     static_cast<unsigned long>(data.from),
                     static_cast<unsigned>(data.payload.size()),
                     rx_raw_hex.c_str());

            team::proto::TeamEncrypted envelope;
            std::vector<uint8_t> plain;
            bool decoded_encrypted =
                decodeEncryptedPayload(data, keys_.mgmt_key.data(), keys_.mgmt_key.size(),
                                       &envelope, plain, false);

            uint8_t version = 0;
            team::proto::TeamMgmtType type = team::proto::TeamMgmtType::Advertise;
            std::vector<uint8_t> payload;

            if (decoded_encrypted)
            {
                logTeamEncrypted("RX", data, envelope, &plain, nullptr, "decrypt-ok");
                if (!team::proto::decodeTeamMgmtMessage(
                        plain.data(), plain.size(),
                        &version, &type, payload))
                {
                    std::string plain_hex = toHex(plain.data(), plain.size(), plain.size());
                    TEAM_LOG("[TEAM] RX TEAM_MGMT decode fail (encrypted) len=%u hex=%s\n",
                             static_cast<unsigned>(plain.size()),
                             plain_hex.c_str());
                    emitError(data, TeamProtocolError::DecodeFail, &envelope);
                    continue;
                }
                if (version != team::proto::kTeamMgmtVersion)
                {
                    std::string plain_hex = toHex(plain.data(), plain.size(), plain.size());
                    TEAM_LOG("[TEAM] RX TEAM_MGMT bad version (encrypted) ver=%u len=%u hex=%s\n",
                             static_cast<unsigned>(version),
                             static_cast<unsigned>(plain.size()),
                             plain_hex.c_str());
                    emitError(data, TeamProtocolError::UnknownVersion, &envelope);
                    continue;
                }
                std::string payload_hex = toHex(payload.data(), payload.size(), payload.size());
                TEAM_LOG("[TEAM] RX TEAM_MGMT encrypted ver=%u type=%s payload_len=%u payload_hex=%s\n",
                         static_cast<unsigned>(version),
                         mgmtTypeName(type),
                         static_cast<unsigned>(payload.size()),
                         payload_hex.c_str());
            }
            else
            {
                if (!team::proto::decodeTeamMgmtMessage(
                        data.payload.data(), data.payload.size(),
                        &version, &type, payload))
                {
                    TEAM_LOG("[TEAM] RX TEAM_MGMT plain decode fail len=%u hex=%s\n",
                             static_cast<unsigned>(data.payload.size()),
                             rx_raw_hex.c_str());
                    continue;
                }
                if (version != team::proto::kTeamMgmtVersion)
                {
                    TEAM_LOG("[TEAM] RX TEAM_MGMT plain bad version ver=%u len=%u hex=%s\n",
                             static_cast<unsigned>(version),
                             static_cast<unsigned>(data.payload.size()),
                             rx_raw_hex.c_str());
                    continue;
                }
                std::string payload_hex = toHex(payload.data(), payload.size(), payload.size());
                TEAM_LOG("[TEAM] RX TEAM_MGMT plain ver=%u type=%s payload_len=%u payload_hex=%s\n",
                         static_cast<unsigned>(version),
                         mgmtTypeName(type),
                         static_cast<unsigned>(payload.size()),
                         payload_hex.c_str());
            }

            switch (type)
            {
            case team::proto::TeamMgmtType::Advertise:
            {
                team::proto::TeamAdvertise msg;
                if (!team::proto::decodeTeamAdvertise(payload.data(), payload.size(), &msg))
                {
                    emitError(data, TeamProtocolError::DecodeFail,
                              decoded_encrypted ? &envelope : nullptr);
                    break;
                }
                logTeamAdvertise(msg, "RX");
                TeamAdvertiseEvent event{makeContext(data, msg.team_id), msg};
                sink_.onTeamAdvertise(event);
                break;
            }
            case team::proto::TeamMgmtType::JoinRequest:
            {
                team::proto::TeamJoinRequest msg;
                if (!team::proto::decodeTeamJoinRequest(payload.data(), payload.size(), &msg))
                {
                    emitError(data, TeamProtocolError::DecodeFail,
                              decoded_encrypted ? &envelope : nullptr);
                    break;
                }
                logTeamJoinRequest(msg, "RX");
                TeamJoinRequestEvent event{makeContext(data, msg.team_id), msg};
                sink_.onTeamJoinRequest(event);
                break;
            }
            case team::proto::TeamMgmtType::JoinAccept:
            {
                team::proto::TeamJoinAccept msg;
                if (!team::proto::decodeTeamJoinAccept(payload.data(), payload.size(), &msg))
                {
                    emitError(data, TeamProtocolError::DecodeFail,
                              decoded_encrypted ? &envelope : nullptr);
                    break;
                }
                logTeamJoinAccept(msg, "RX");
                team::TeamEventContext ctx = makeContext(data, decoded_encrypted ? &envelope : nullptr);
                if (msg.has_team_id)
                {
                    ctx.team_id = msg.team_id;
                }
                TeamJoinAcceptEvent event{ctx, msg};
                sink_.onTeamJoinAccept(event);

                if (msg.channel_psk_len > 0 && msg.has_team_id && msg.key_id != 0)
                {
                    setKeysFromPsk(msg.team_id, msg.key_id,
                                   msg.channel_psk.data(), msg.channel_psk_len);
                }
                break;
            }
            case team::proto::TeamMgmtType::JoinConfirm:
            {
                if (!decoded_encrypted)
                {
                    break;
                }
                team::proto::TeamJoinConfirm msg;
                if (!team::proto::decodeTeamJoinConfirm(payload.data(), payload.size(), &msg))
                {
                    emitError(data, TeamProtocolError::DecodeFail,
                              decoded_encrypted ? &envelope : nullptr);
                    break;
                }
                TeamJoinConfirmEvent event{makeContext(data, decoded_encrypted ? &envelope : nullptr), msg};
                sink_.onTeamJoinConfirm(event);
                logTeamJoinConfirm(msg, "RX");
                break;
            }
            case team::proto::TeamMgmtType::JoinDecision:
            {
                team::proto::TeamJoinDecision msg;
                if (!team::proto::decodeTeamJoinDecision(payload.data(), payload.size(), &msg))
                {
                    emitError(data, TeamProtocolError::DecodeFail,
                              decoded_encrypted ? &envelope : nullptr);
                    break;
                }
                logTeamJoinDecision(msg, "RX");
                TeamJoinDecisionEvent event{makeContext(data, decoded_encrypted ? &envelope : nullptr), msg};
                sink_.onTeamJoinDecision(event);
                break;
            }
            case team::proto::TeamMgmtType::Kick:
            {
                if (!decoded_encrypted)
                {
                    break;
                }
                team::proto::TeamKick msg;
                if (!team::proto::decodeTeamKick(payload.data(), payload.size(), &msg))
                {
                    emitError(data, TeamProtocolError::DecodeFail,
                              decoded_encrypted ? &envelope : nullptr);
                    break;
                }
                logTeamKick(msg, "RX");
                TeamKickEvent event{makeContext(data, decoded_encrypted ? &envelope : nullptr), msg};
                sink_.onTeamKick(event);
                break;
            }
            case team::proto::TeamMgmtType::TransferLeader:
            {
                if (!decoded_encrypted)
                {
                    break;
                }
                team::proto::TeamTransferLeader msg;
                if (!team::proto::decodeTeamTransferLeader(payload.data(), payload.size(), &msg))
                {
                    emitError(data, TeamProtocolError::DecodeFail,
                              decoded_encrypted ? &envelope : nullptr);
                    break;
                }
                logTeamTransferLeader(msg, "RX");
                TeamTransferLeaderEvent event{makeContext(data, decoded_encrypted ? &envelope : nullptr), msg};
                sink_.onTeamTransferLeader(event);
                break;
            }
            case team::proto::TeamMgmtType::KeyDist:
            {
                team::proto::TeamKeyDist msg;
                if (!team::proto::decodeTeamKeyDist(payload.data(), payload.size(), &msg))
                {
                    emitError(data, TeamProtocolError::DecodeFail,
                              decoded_encrypted ? &envelope : nullptr);
                    break;
                }
                logTeamKeyDist(msg, "RX");
                TeamKeyDistEvent event{makeContext(data, decoded_encrypted ? &envelope : nullptr), msg};
                sink_.onTeamKeyDist(event);

                if (msg.channel_psk_len > 0 && msg.key_id != 0)
                {
                    setKeysFromPsk(msg.team_id, msg.key_id,
                                   msg.channel_psk.data(), msg.channel_psk_len);
                }
                break;
            }
            case team::proto::TeamMgmtType::Status:
            {
                team::proto::TeamStatus msg;
                if (!team::proto::decodeTeamStatus(payload.data(), payload.size(), &msg))
                {
                    emitError(data, TeamProtocolError::DecodeFail,
                              decoded_encrypted ? &envelope : nullptr);
                    break;
                }
                logTeamStatus(msg, "RX");
                TeamStatusEvent event{makeContext(data, decoded_encrypted ? &envelope : nullptr), msg};
                sink_.onTeamStatus(event);
                break;
            }
            default:
                break;
            }
        }
        else if (data.portnum == team::proto::TEAM_POSITION_APP)
        {
            std::string rx_raw_hex = toHex(data.payload.data(), data.payload.size(), data.payload.size());
            TEAM_LOG("[TEAM] RX TEAM_POS raw from=%08lX len=%u hex=%s\n",
                     static_cast<unsigned long>(data.from),
                     static_cast<unsigned>(data.payload.size()),
                     rx_raw_hex.c_str());
            team::proto::TeamEncrypted envelope;
            std::vector<uint8_t> plain;
            if (!decodeEncryptedPayload(data, keys_.pos_key.data(), keys_.pos_key.size(),
                                        &envelope, plain, true))
            {
                continue;
            }

            logTeamEncrypted("RX", data, envelope, &plain, nullptr, "decrypt-ok");
            TeamPositionEvent event{makeContext(data, &envelope), plain};
            sink_.onTeamPosition(event);
        }
        else if (data.portnum == team::proto::TEAM_WAYPOINT_APP)
        {
            std::string rx_raw_hex = toHex(data.payload.data(), data.payload.size(), data.payload.size());
            TEAM_LOG("[TEAM] RX TEAM_WP raw from=%08lX len=%u hex=%s\n",
                     static_cast<unsigned long>(data.from),
                     static_cast<unsigned>(data.payload.size()),
                     rx_raw_hex.c_str());
            team::proto::TeamEncrypted envelope;
            std::vector<uint8_t> plain;
            if (!decodeEncryptedPayload(data, keys_.wp_key.data(), keys_.wp_key.size(),
                                        &envelope, plain, true))
            {
                continue;
            }

            logTeamEncrypted("RX", data, envelope, &plain, nullptr, "decrypt-ok");
            TeamWaypointEvent event{makeContext(data, &envelope), plain};
            sink_.onTeamWaypoint(event);
        }
        else if (data.portnum == team::proto::TEAM_TRACK_APP)
        {
            std::string rx_raw_hex = toHex(data.payload.data(), data.payload.size(), data.payload.size());
            TEAM_LOG("[TEAM] RX TEAM_TRACK raw from=%08lX len=%u hex=%s\n",
                     static_cast<unsigned long>(data.from),
                     static_cast<unsigned>(data.payload.size()),
                     rx_raw_hex.c_str());
            team::proto::TeamEncrypted envelope;
            std::vector<uint8_t> plain;
            if (!decodeEncryptedPayload(data, keys_.pos_key.data(), keys_.pos_key.size(),
                                        &envelope, plain, true))
            {
                continue;
            }

            logTeamEncrypted("RX", data, envelope, &plain, nullptr, "decrypt-ok");
            TeamTrackEvent event{makeContext(data, &envelope), plain};
            sink_.onTeamTrack(event);
        }
        else if (data.portnum == team::proto::TEAM_CHAT_APP)
        {
            std::string rx_raw_hex = toHex(data.payload.data(), data.payload.size(), data.payload.size());
            TEAM_LOG("[TEAM] RX TEAM_CHAT raw from=%08lX len=%u hex=%s\n",
                     static_cast<unsigned long>(data.from),
                     static_cast<unsigned>(data.payload.size()),
                     rx_raw_hex.c_str());
            team::proto::TeamEncrypted envelope;
            std::vector<uint8_t> plain;
            if (!decodeEncryptedPayload(data, keys_.chat_key.data(), keys_.chat_key.size(),
                                        &envelope, plain, true))
            {
                continue;
            }

            logTeamEncrypted("RX", data, envelope, &plain, nullptr, "decrypt-ok");
            team::proto::TeamChatMessage msg;
            if (!team::proto::decodeTeamChatMessage(plain.data(), plain.size(), &msg))
            {
                emitError(data, TeamProtocolError::DecodeFail, &envelope);
                continue;
            }
            if (msg.header.version != team::proto::kTeamChatVersion)
            {
                emitError(data, TeamProtocolError::UnknownVersion, &envelope);
                continue;
            }
            TeamChatEvent event{makeContext(data, &envelope), msg};
            sink_.onTeamChat(event);
        }
        else
        {
            sys::EventBus::publish(
                new sys::AppDataEvent(
                    data.portnum,
                    data.from,
                    data.to,
                    data.packet_id,
                    static_cast<uint8_t>(data.channel),
                    data.channel_hash,
                    data.want_response,
                    data.payload),
                0);
        }
    }
}

bool TeamService::sendAdvertise(const team::proto::TeamAdvertise& msg,
                                chat::ChannelId channel)
{
    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamAdvertise(msg, payload))
    {
        return false;
    }
    logTeamAdvertise(msg, "TX");
    std::string payload_hex = toHex(payload.data(), payload.size(), payload.size());
    TEAM_LOG("[TEAM] TX TEAM_MGMT Advertise payload_len=%u payload_hex=%s\n",
             static_cast<unsigned>(payload.size()),
             payload_hex.c_str());
    return sendMgmtPlain(team::proto::TeamMgmtType::Advertise, payload, channel, 0);
}

bool TeamService::sendJoinRequest(const team::proto::TeamJoinRequest& msg,
                                  chat::ChannelId channel, chat::NodeId dest)
{
    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamJoinRequest(msg, payload))
    {
        return false;
    }
    logTeamJoinRequest(msg, "TX");
    std::string payload_hex = toHex(payload.data(), payload.size(), payload.size());
    TEAM_LOG("[TEAM] TX TEAM_MGMT JoinRequest payload_len=%u payload_hex=%s\n",
             static_cast<unsigned>(payload.size()),
             payload_hex.c_str());
    return sendMgmtPlain(team::proto::TeamMgmtType::JoinRequest, payload, channel, dest);
}

bool TeamService::sendJoinAccept(const team::proto::TeamJoinAccept& msg,
                                 chat::ChannelId channel, chat::NodeId dest)
{
    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamJoinAccept(msg, payload))
    {
        return false;
    }
    logTeamJoinAccept(msg, "TX");
    std::string payload_hex = toHex(payload.data(), payload.size(), payload.size());
    TEAM_LOG("[TEAM] TX TEAM_MGMT JoinAccept payload_len=%u payload_hex=%s\n",
             static_cast<unsigned>(payload.size()),
             payload_hex.c_str());
    return sendMgmtPlain(team::proto::TeamMgmtType::JoinAccept, payload, channel, dest);
}

bool TeamService::sendJoinConfirm(const team::proto::TeamJoinConfirm& msg,
                                  chat::ChannelId channel, chat::NodeId dest)
{
    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamJoinConfirm(msg, payload))
    {
        return false;
    }
    logTeamJoinConfirm(msg, "TX");
    std::string payload_hex = toHex(payload.data(), payload.size(), payload.size());
    TEAM_LOG("[TEAM] TX TEAM_MGMT JoinConfirm payload_len=%u payload_hex=%s\n",
             static_cast<unsigned>(payload.size()),
             payload_hex.c_str());
    return sendMgmtEncrypted(team::proto::TeamMgmtType::JoinConfirm, payload, channel, dest);
}

bool TeamService::sendJoinDecision(const team::proto::TeamJoinDecision& msg,
                                   chat::ChannelId channel, chat::NodeId dest)
{
    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamJoinDecision(msg, payload))
    {
        return false;
    }
    logTeamJoinDecision(msg, "TX");
    std::string payload_hex = toHex(payload.data(), payload.size(), payload.size());
    TEAM_LOG("[TEAM] TX TEAM_MGMT JoinDecision payload_len=%u payload_hex=%s\n",
             static_cast<unsigned>(payload.size()),
             payload_hex.c_str());
    return sendMgmtPlain(team::proto::TeamMgmtType::JoinDecision, payload, channel, dest);
}

bool TeamService::sendKick(const team::proto::TeamKick& msg,
                           chat::ChannelId channel, chat::NodeId dest)
{
    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamKick(msg, payload))
    {
        return false;
    }
    logTeamKick(msg, "TX");
    std::string payload_hex = toHex(payload.data(), payload.size(), payload.size());
    TEAM_LOG("[TEAM] TX TEAM_MGMT Kick payload_len=%u payload_hex=%s\n",
             static_cast<unsigned>(payload.size()),
             payload_hex.c_str());
    return sendMgmtEncrypted(team::proto::TeamMgmtType::Kick, payload, channel, dest);
}

bool TeamService::sendTransferLeader(const team::proto::TeamTransferLeader& msg,
                                     chat::ChannelId channel, chat::NodeId dest)
{
    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamTransferLeader(msg, payload))
    {
        return false;
    }
    logTeamTransferLeader(msg, "TX");
    std::string payload_hex = toHex(payload.data(), payload.size(), payload.size());
    TEAM_LOG("[TEAM] TX TEAM_MGMT TransferLeader payload_len=%u payload_hex=%s\n",
             static_cast<unsigned>(payload.size()),
             payload_hex.c_str());
    return sendMgmtEncrypted(team::proto::TeamMgmtType::TransferLeader, payload, channel, dest);
}

bool TeamService::sendKeyDist(const team::proto::TeamKeyDist& msg,
                              chat::ChannelId channel, chat::NodeId dest)
{
    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamKeyDist(msg, payload))
    {
        return false;
    }
    logTeamKeyDist(msg, "TX");
    std::string payload_hex = toHex(payload.data(), payload.size(), payload.size());
    TEAM_LOG("[TEAM] TX TEAM_MGMT KeyDist payload_len=%u payload_hex=%s\n",
             static_cast<unsigned>(payload.size()),
             payload_hex.c_str());
    return sendMgmtEncrypted(team::proto::TeamMgmtType::KeyDist, payload, channel, dest);
}

bool TeamService::sendKeyDistPlain(const team::proto::TeamKeyDist& msg,
                                   chat::ChannelId channel, chat::NodeId dest)
{
    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamKeyDist(msg, payload))
    {
        return false;
    }
    logTeamKeyDist(msg, "TX");
    std::string payload_hex = toHex(payload.data(), payload.size(), payload.size());
    TEAM_LOG("[TEAM] TX TEAM_MGMT KeyDist (plain) payload_len=%u payload_hex=%s\n",
             static_cast<unsigned>(payload.size()),
             payload_hex.c_str());
    return sendMgmtPlain(team::proto::TeamMgmtType::KeyDist, payload, channel, dest);
}

bool TeamService::sendStatus(const team::proto::TeamStatus& msg,
                             chat::ChannelId channel, chat::NodeId dest)
{
    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamStatus(msg, payload))
    {
        return false;
    }
    logTeamStatus(msg, "TX");
    std::string payload_hex = toHex(payload.data(), payload.size(), payload.size());
    TEAM_LOG("[TEAM] TX TEAM_MGMT Status payload_len=%u payload_hex=%s\n",
             static_cast<unsigned>(payload.size()),
             payload_hex.c_str());
    return sendMgmtEncrypted(team::proto::TeamMgmtType::Status, payload, channel, dest);
}

bool TeamService::sendStatusPlain(const team::proto::TeamStatus& msg,
                                  chat::ChannelId channel, chat::NodeId dest)
{
    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamStatus(msg, payload))
    {
        return false;
    }
    logTeamStatus(msg, "TX");
    std::string payload_hex = toHex(payload.data(), payload.size(), payload.size());
    TEAM_LOG("[TEAM] TX TEAM_MGMT Status (plain) payload_len=%u payload_hex=%s\n",
             static_cast<unsigned>(payload.size()),
             payload_hex.c_str());
    return sendMgmtPlain(team::proto::TeamMgmtType::Status, payload, channel, dest);
}

bool TeamService::sendPosition(const std::vector<uint8_t>& payload,
                               chat::ChannelId channel)
{
    if (!keys_.valid)
    {
        TEAM_LOG("[TEAM] TX TEAM_POS keys not ready\n");
        return false;
    }

    team::proto::TeamEncrypted envelope;
    std::vector<uint8_t> wire;
    if (!encodeEncryptedPayload(payload, keys_.pos_key.data(), keys_.pos_key.size(),
                                &envelope, wire))
    {
        TEAM_LOG("[TEAM] TX TEAM_POS encrypt fail plain_len=%u\n",
                 static_cast<unsigned>(payload.size()));
        return false;
    }
    std::string plain_hex = toHex(payload.data(), payload.size(), payload.size());
    TEAM_LOG("[TEAM] TX TEAM_POS plain_len=%u plain_hex=%s\n",
             static_cast<unsigned>(payload.size()),
             plain_hex.c_str());
    std::string wire_hex = toHex(wire.data(), wire.size(), wire.size());
    TEAM_LOG("[TEAM] TX TEAM_POS wire_len=%u wire_hex=%s\n",
             static_cast<unsigned>(wire.size()),
             wire_hex.c_str());
    chat::MeshIncomingData dummy;
    dummy.portnum = team::proto::TEAM_POSITION_APP;
    logTeamEncrypted("TX", dummy, envelope, nullptr, nullptr, "encrypt-ok");
    if (!mesh_.sendAppData(channel, team::proto::TEAM_POSITION_APP,
                           wire.data(), wire.size(), 0, false))
    {
        TEAM_LOG("[TEAM] TX TEAM_POS send fail wire_len=%u\n",
                 static_cast<unsigned>(wire.size()));
        return false;
    }
    return true;
}

bool TeamService::sendWaypoint(const std::vector<uint8_t>& payload,
                               chat::ChannelId channel)
{
    if (!keys_.valid)
    {
        TEAM_LOG("[TEAM] TX TEAM_WP keys not ready\n");
        return false;
    }

    team::proto::TeamEncrypted envelope;
    std::vector<uint8_t> wire;
    if (!encodeEncryptedPayload(payload, keys_.wp_key.data(), keys_.wp_key.size(),
                                &envelope, wire))
    {
        TEAM_LOG("[TEAM] TX TEAM_WP encrypt fail plain_len=%u\n",
                 static_cast<unsigned>(payload.size()));
        return false;
    }
    std::string plain_hex = toHex(payload.data(), payload.size(), payload.size());
    TEAM_LOG("[TEAM] TX TEAM_WP plain_len=%u plain_hex=%s\n",
             static_cast<unsigned>(payload.size()),
             plain_hex.c_str());
    std::string wire_hex = toHex(wire.data(), wire.size(), wire.size());
    TEAM_LOG("[TEAM] TX TEAM_WP wire_len=%u wire_hex=%s\n",
             static_cast<unsigned>(wire.size()),
             wire_hex.c_str());
    chat::MeshIncomingData dummy;
    dummy.portnum = team::proto::TEAM_WAYPOINT_APP;
    logTeamEncrypted("TX", dummy, envelope, nullptr, nullptr, "encrypt-ok");
    if (!mesh_.sendAppData(channel, team::proto::TEAM_WAYPOINT_APP,
                           wire.data(), wire.size(), 0, false))
    {
        TEAM_LOG("[TEAM] TX TEAM_WP send fail wire_len=%u\n",
                 static_cast<unsigned>(wire.size()));
        return false;
    }
    return true;
}

bool TeamService::sendTrack(const std::vector<uint8_t>& payload,
                            chat::ChannelId channel)
{
    if (!keys_.valid)
    {
        TEAM_LOG("[TEAM] TX TEAM_TRACK keys not ready\n");
        return false;
    }

    team::proto::TeamEncrypted envelope;
    std::vector<uint8_t> wire;
    if (!encodeEncryptedPayload(payload, keys_.pos_key.data(), keys_.pos_key.size(),
                                &envelope, wire))
    {
        TEAM_LOG("[TEAM] TX TEAM_TRACK encrypt fail plain_len=%u\n",
                 static_cast<unsigned>(payload.size()));
        return false;
    }
    std::string plain_hex = toHex(payload.data(), payload.size(), payload.size());
    TEAM_LOG("[TEAM] TX TEAM_TRACK plain_len=%u plain_hex=%s\n",
             static_cast<unsigned>(payload.size()),
             plain_hex.c_str());
    std::string wire_hex = toHex(wire.data(), wire.size(), wire.size());
    TEAM_LOG("[TEAM] TX TEAM_TRACK wire_len=%u wire_hex=%s\n",
             static_cast<unsigned>(wire.size()),
             wire_hex.c_str());
    chat::MeshIncomingData dummy;
    dummy.portnum = team::proto::TEAM_TRACK_APP;
    logTeamEncrypted("TX", dummy, envelope, nullptr, nullptr, "encrypt-ok");
    if (!mesh_.sendAppData(channel, team::proto::TEAM_TRACK_APP,
                           wire.data(), wire.size(), 0, false))
    {
        TEAM_LOG("[TEAM] TX TEAM_TRACK send fail wire_len=%u\n",
                 static_cast<unsigned>(wire.size()));
        return false;
    }
    return true;
}

bool TeamService::sendChat(const team::proto::TeamChatMessage& msg,
                           chat::ChannelId channel)
{
    if (!keys_.valid)
    {
        last_send_error_ = SendError::KeysNotReady;
        TEAM_LOG("[TEAM] TX TEAM_CHAT keys not ready\n");
        return false;
    }

    std::vector<uint8_t> plain;
    if (!team::proto::encodeTeamChatMessage(msg, plain))
    {
        last_send_error_ = SendError::EncodeFail;
        TEAM_LOG("[TEAM] TX TEAM_CHAT encode fail\n");
        return false;
    }

    team::proto::TeamEncrypted envelope;
    std::vector<uint8_t> wire;
    if (!encodeEncryptedPayload(plain, keys_.chat_key.data(), keys_.chat_key.size(),
                                &envelope, wire))
    {
        last_send_error_ = SendError::EncryptFail;
        TEAM_LOG("[TEAM] TX TEAM_CHAT encrypt fail plain_len=%u\n",
                 static_cast<unsigned>(plain.size()));
        return false;
    }

    std::string plain_hex = toHex(plain.data(), plain.size(), plain.size());
    TEAM_LOG("[TEAM] TX TEAM_CHAT plain_len=%u plain_hex=%s\n",
             static_cast<unsigned>(plain.size()),
             plain_hex.c_str());
    std::string wire_hex = toHex(wire.data(), wire.size(), wire.size());
    TEAM_LOG("[TEAM] TX TEAM_CHAT wire_len=%u wire_hex=%s\n",
             static_cast<unsigned>(wire.size()),
             wire_hex.c_str());
    chat::MeshIncomingData dummy;
    dummy.portnum = team::proto::TEAM_CHAT_APP;
    logTeamEncrypted("TX", dummy, envelope, nullptr, nullptr, "encrypt-ok");
    if (!mesh_.sendAppData(channel, team::proto::TEAM_CHAT_APP,
                           wire.data(), wire.size(), 0, false))
    {
        last_send_error_ = SendError::MeshSendFail;
        TEAM_LOG("[TEAM] TX TEAM_CHAT send fail wire_len=%u\n",
                 static_cast<unsigned>(wire.size()));
        return false;
    }
    last_send_error_ = SendError::None;
    return true;
}

bool TeamService::requestNodeInfo(chat::NodeId dest, bool want_response)
{
    return mesh_.requestNodeInfo(dest, want_response);
}

bool TeamService::startPkiVerification(chat::NodeId dest)
{
    return mesh_.startKeyVerification(dest);
}

bool TeamService::submitPkiNumber(chat::NodeId dest, uint64_t nonce, uint32_t number)
{
    return mesh_.submitKeyVerificationNumber(dest, nonce, number);
}

bool TeamService::decodeEncryptedPayload(const chat::MeshIncomingData& data,
                                         const uint8_t* key, size_t key_len,
                                         team::proto::TeamEncrypted* envelope,
                                         std::vector<uint8_t>& out_plain,
                                         bool emit_errors)
{
    if (!envelope)
    {
        return false;
    }
    if (!team::proto::decodeTeamEncrypted(data.payload.data(),
                                          data.payload.size(),
                                          envelope))
    {
        if (emit_errors)
        {
            std::string raw_hex = toHex(data.payload.data(), data.payload.size(), data.payload.size());
            TEAM_LOG("[TEAM] RX %s decode fail from=%08lX len=%u hex=%s\n",
                     teamPortName(data.portnum),
                     static_cast<unsigned long>(data.from),
                     static_cast<unsigned>(data.payload.size()),
                     raw_hex.c_str());
            emitError(data, TeamProtocolError::DecodeFail, nullptr);
        }
        return false;
    }
    if (envelope->version != team::proto::kTeamEnvelopeVersion)
    {
        if (emit_errors)
        {
            TEAM_LOG("[TEAM] RX %s bad version=%u from=%08lX\n",
                     teamPortName(data.portnum),
                     static_cast<unsigned>(envelope->version),
                     static_cast<unsigned long>(data.from));
            emitError(data, TeamProtocolError::UnknownVersion, envelope);
        }
        return false;
    }
    if (!keys_.valid ||
        envelope->team_id != keys_.team_id ||
        envelope->key_id != keys_.key_id)
    {
        if (emit_errors)
        {
            TEAM_LOG("[TEAM] RX %s key mismatch from=%08lX env_key_id=%lu\n",
                     teamPortName(data.portnum),
                     static_cast<unsigned long>(data.from),
                     static_cast<unsigned long>(envelope->key_id));
            emitError(data, TeamProtocolError::KeyMismatch, envelope);
        }
        return false;
    }

    std::vector<uint8_t> aad = buildAad(*envelope);
    if (!crypto_.aeadDecrypt(key, key_len,
                             envelope->nonce.data(), envelope->nonce.size(),
                             aad.data(), aad.size(),
                             envelope->ciphertext.data(), envelope->ciphertext.size(),
                             out_plain))
    {
        if (emit_errors)
        {
            TEAM_LOG("[TEAM] RX %s decrypt fail from=%08lX key_id=%lu\n",
                     teamPortName(data.portnum),
                     static_cast<unsigned long>(data.from),
                     static_cast<unsigned long>(envelope->key_id));
            emitError(data, TeamProtocolError::DecryptFail, envelope);
        }
        return false;
    }
    return true;
}

bool TeamService::encodeEncryptedPayload(const std::vector<uint8_t>& plain,
                                         const uint8_t* key, size_t key_len,
                                         team::proto::TeamEncrypted* envelope,
                                         std::vector<uint8_t>& out_wire)
{
    if (!envelope || !keys_.valid)
    {
        return false;
    }

    envelope->version = team::proto::kTeamEnvelopeVersion;
    envelope->aad_flags = 0;
    envelope->team_id = keys_.team_id;
    envelope->key_id = keys_.key_id;
    fillNonce(envelope->nonce);

    std::vector<uint8_t> aad = buildAad(*envelope);
    if (!crypto_.aeadEncrypt(key, key_len,
                             envelope->nonce.data(), envelope->nonce.size(),
                             aad.data(), aad.size(),
                             plain.data(), plain.size(),
                             envelope->ciphertext))
    {
        return false;
    }

    return team::proto::encodeTeamEncrypted(*envelope, out_wire);
}

bool TeamService::sendMgmtPlain(const team::proto::TeamMgmtType type,
                                const std::vector<uint8_t>& payload,
                                chat::ChannelId channel, chat::NodeId dest)
{
    std::vector<uint8_t> wire;
    if (!team::proto::encodeTeamMgmtMessage(type, payload, wire))
    {
        last_send_error_ = SendError::EncodeFail;
        TEAM_LOG("[TEAM] TX TEAM_MGMT encode fail type=%s payload_len=%u\n",
                 mgmtTypeName(type),
                 static_cast<unsigned>(payload.size()));
        return false;
    }
    std::string wire_hex = toHex(wire.data(), wire.size(), wire.size());
    TEAM_LOG("[TEAM] TX TEAM_MGMT plain type=%s ch=%u dest=%08lX wire_len=%u wire_hex=%s\n",
             mgmtTypeName(type),
             static_cast<unsigned>(channel),
             static_cast<unsigned long>(dest),
             static_cast<unsigned>(wire.size()),
             wire_hex.c_str());
    if (!mesh_.sendAppData(channel, team::proto::TEAM_MGMT_APP,
                           wire.data(), wire.size(), dest, false))
    {
        last_send_error_ = SendError::MeshSendFail;
        TEAM_LOG("[TEAM] TX TEAM_MGMT send fail type=%s ch=%u dest=%08lX wire_len=%u\n",
                 mgmtTypeName(type),
                 static_cast<unsigned>(channel),
                 static_cast<unsigned long>(dest),
                 static_cast<unsigned>(wire.size()));
        return false;
    }
    last_send_error_ = SendError::None;
    return true;
}

bool TeamService::sendMgmtEncrypted(const team::proto::TeamMgmtType type,
                                    const std::vector<uint8_t>& payload,
                                    chat::ChannelId channel, chat::NodeId dest)
{
    if (!keys_.valid)
    {
        last_send_error_ = SendError::KeysNotReady;
        TEAM_LOG("[TEAM] TX TEAM_MGMT encrypt fail type=%s keys_not_ready\n",
                 mgmtTypeName(type));
        return false;
    }

    std::vector<uint8_t> mgmt_wire;
    if (!team::proto::encodeTeamMgmtMessage(type, payload, mgmt_wire))
    {
        last_send_error_ = SendError::EncodeFail;
        TEAM_LOG("[TEAM] TX TEAM_MGMT encode fail type=%s payload_len=%u\n",
                 mgmtTypeName(type),
                 static_cast<unsigned>(payload.size()));
        return false;
    }

    team::proto::TeamEncrypted envelope;
    std::vector<uint8_t> wire;
    if (!encodeEncryptedPayload(mgmt_wire, keys_.mgmt_key.data(), keys_.mgmt_key.size(),
                                &envelope, wire))
    {
        last_send_error_ = SendError::EncryptFail;
        TEAM_LOG("[TEAM] TX TEAM_MGMT encrypt fail type=%s payload_len=%u\n",
                 mgmtTypeName(type),
                 static_cast<unsigned>(payload.size()));
        return false;
    }

    chat::MeshIncomingData dummy;
    dummy.portnum = team::proto::TEAM_MGMT_APP;
    logTeamEncrypted("TX", dummy, envelope, &mgmt_wire, &wire, "encrypt-ok");

    if (!mesh_.sendAppData(channel, team::proto::TEAM_MGMT_APP,
                           wire.data(), wire.size(), dest, false))
    {
        last_send_error_ = SendError::MeshSendFail;
        TEAM_LOG("[TEAM] TX TEAM_MGMT send fail type=%s ch=%u dest=%08lX wire_len=%u\n",
                 mgmtTypeName(type),
                 static_cast<unsigned>(channel),
                 static_cast<unsigned long>(dest),
                 static_cast<unsigned>(wire.size()));
        return false;
    }
    last_send_error_ = SendError::None;
    return true;
}

void TeamService::emitError(const chat::MeshIncomingData& data,
                            team::TeamProtocolError error,
                            const team::proto::TeamEncrypted* envelope)
{
    TeamErrorEvent event;
    event.ctx = makeContext(data, envelope);
    event.error = error;
    event.portnum = data.portnum;
    TEAM_LOG("[TEAM] RX error port=%s from=%08lX err=%s team_id=%s key_id=%lu\n",
             teamPortName(data.portnum),
             static_cast<unsigned long>(data.from),
             teamErrorName(error),
             hexFromArray(event.ctx.team_id).c_str(),
             static_cast<unsigned long>(event.ctx.key_id));
    sink_.onTeamError(event);
}

} // namespace team
