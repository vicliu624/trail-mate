#include "team_service.h"

#include "../domain/team_events.h"
#include "../protocol/team_mgmt.h"
#include "../protocol/team_portnum.h"
#include "../protocol/team_wire.h"
#include <Arduino.h>

namespace team
{
namespace
{

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

void TeamService::processIncoming()
{
    chat::MeshIncomingData data;
    while (mesh_.pollIncomingData(&data))
    {
        if (data.portnum == team::proto::TEAM_MGMT_APP)
        {
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
                if (!team::proto::decodeTeamMgmtMessage(
                        plain.data(), plain.size(),
                        &version, &type, payload))
                {
                    emitError(data, TeamProtocolError::DecodeFail, &envelope);
                    continue;
                }
                if (version != team::proto::kTeamMgmtVersion)
                {
                    emitError(data, TeamProtocolError::UnknownVersion, &envelope);
                    continue;
                }
            }
            else
            {
                if (!team::proto::decodeTeamMgmtMessage(
                        data.payload.data(), data.payload.size(),
                        &version, &type, payload))
                {
                    continue;
                }
                if (version != team::proto::kTeamMgmtVersion)
                {
                    continue;
                }
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
                TeamJoinRequestEvent event{makeContext(data, msg.team_id), msg};
                sink_.onTeamJoinRequest(event);
                break;
            }
            case team::proto::TeamMgmtType::JoinAccept:
            {
                if (!decoded_encrypted)
                {
                    break;
                }
                team::proto::TeamJoinAccept msg;
                if (!team::proto::decodeTeamJoinAccept(payload.data(), payload.size(), &msg))
                {
                    emitError(data, TeamProtocolError::DecodeFail,
                              decoded_encrypted ? &envelope : nullptr);
                    break;
                }
                TeamJoinAcceptEvent event{makeContext(data, decoded_encrypted ? &envelope : nullptr), msg};
                sink_.onTeamJoinAccept(event);
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
                break;
            }
            case team::proto::TeamMgmtType::Status:
            {
                if (!decoded_encrypted)
                {
                    break;
                }
                team::proto::TeamStatus msg;
                if (!team::proto::decodeTeamStatus(payload.data(), payload.size(), &msg))
                {
                    emitError(data, TeamProtocolError::DecodeFail,
                              decoded_encrypted ? &envelope : nullptr);
                    break;
                }
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
            team::proto::TeamEncrypted envelope;
            std::vector<uint8_t> plain;
            if (!decodeEncryptedPayload(data, keys_.pos_key.data(), keys_.pos_key.size(),
                                        &envelope, plain, true))
            {
                continue;
            }

            TeamPositionEvent event{makeContext(data, &envelope), plain};
            sink_.onTeamPosition(event);
        }
        else if (data.portnum == team::proto::TEAM_WAYPOINT_APP)
        {
            team::proto::TeamEncrypted envelope;
            std::vector<uint8_t> plain;
            if (!decodeEncryptedPayload(data, keys_.wp_key.data(), keys_.wp_key.size(),
                                        &envelope, plain, true))
            {
                continue;
            }

            TeamWaypointEvent event{makeContext(data, &envelope), plain};
            sink_.onTeamWaypoint(event);
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
    return sendMgmtEncrypted(team::proto::TeamMgmtType::JoinAccept, payload, channel, dest);
}

bool TeamService::sendJoinConfirm(const team::proto::TeamJoinConfirm& msg,
                                  chat::ChannelId channel, chat::NodeId dest)
{
    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamJoinConfirm(msg, payload))
    {
        return false;
    }
    return sendMgmtEncrypted(team::proto::TeamMgmtType::JoinConfirm, payload, channel, dest);
}

bool TeamService::sendStatus(const team::proto::TeamStatus& msg,
                             chat::ChannelId channel, chat::NodeId dest)
{
    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamStatus(msg, payload))
    {
        return false;
    }
    return sendMgmtEncrypted(team::proto::TeamMgmtType::Status, payload, channel, dest);
}

bool TeamService::sendPosition(const std::vector<uint8_t>& payload,
                               chat::ChannelId channel)
{
    if (!keys_.valid)
    {
        return false;
    }

    team::proto::TeamEncrypted envelope;
    std::vector<uint8_t> wire;
    if (!encodeEncryptedPayload(payload, keys_.pos_key.data(), keys_.pos_key.size(),
                                &envelope, wire))
    {
        return false;
    }
    return mesh_.sendAppData(channel, team::proto::TEAM_POSITION_APP,
                             wire.data(), wire.size(), 0, false);
}

bool TeamService::sendWaypoint(const std::vector<uint8_t>& payload,
                               chat::ChannelId channel)
{
    if (!keys_.valid)
    {
        return false;
    }

    team::proto::TeamEncrypted envelope;
    std::vector<uint8_t> wire;
    if (!encodeEncryptedPayload(payload, keys_.wp_key.data(), keys_.wp_key.size(),
                                &envelope, wire))
    {
        return false;
    }
    return mesh_.sendAppData(channel, team::proto::TEAM_WAYPOINT_APP,
                             wire.data(), wire.size(), 0, false);
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
            emitError(data, TeamProtocolError::DecodeFail, nullptr);
        }
        return false;
    }
    if (envelope->version != team::proto::kTeamEnvelopeVersion)
    {
        if (emit_errors)
        {
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
        return false;
    }
    return mesh_.sendAppData(channel, team::proto::TEAM_MGMT_APP,
                             wire.data(), wire.size(), dest, false);
}

bool TeamService::sendMgmtEncrypted(const team::proto::TeamMgmtType type,
                                    const std::vector<uint8_t>& payload,
                                    chat::ChannelId channel, chat::NodeId dest)
{
    if (!keys_.valid)
    {
        return false;
    }

    std::vector<uint8_t> mgmt_wire;
    if (!team::proto::encodeTeamMgmtMessage(type, payload, mgmt_wire))
    {
        return false;
    }

    team::proto::TeamEncrypted envelope;
    std::vector<uint8_t> wire;
    if (!encodeEncryptedPayload(mgmt_wire, keys_.mgmt_key.data(), keys_.mgmt_key.size(),
                                &envelope, wire))
    {
        return false;
    }

    return mesh_.sendAppData(channel, team::proto::TEAM_MGMT_APP,
                             wire.data(), wire.size(), dest, false);
}

void TeamService::emitError(const chat::MeshIncomingData& data,
                            team::TeamProtocolError error,
                            const team::proto::TeamEncrypted* envelope)
{
    TeamErrorEvent event;
    event.ctx = makeContext(data, envelope);
    event.error = error;
    event.portnum = data.portnum;
    sink_.onTeamError(event);
}

} // namespace team
