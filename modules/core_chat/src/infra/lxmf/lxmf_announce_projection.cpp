#include "chat/infra/lxmf/lxmf_announce_projection.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace chat::lxmf::runtime
{
namespace
{
constexpr const char* kAnonymousPeerDisplayName = "Anonymous Peer";
constexpr const char* kAnonymousNodeDisplayName = "Anonymous Node";

bool hashesEqual(const uint8_t* a, const uint8_t* b, std::size_t len)
{
    if ((!a || !b) && len != 0)
    {
        return false;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

void copyHash(uint8_t* out, const uint8_t* in, std::size_t len)
{
    if (!out || !in || len == 0)
    {
        return;
    }
    std::memcpy(out, in, len);
}

void copyCString(char* out, std::size_t out_len, const char* in)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!in)
    {
        return;
    }
    const std::size_t max_copy_len = out_len - 1U;
    const auto* terminator =
        static_cast<const char*>(std::memchr(in, '\0', max_copy_len));
    const std::size_t copy_len =
        terminator ? static_cast<std::size_t>(terminator - in) : max_copy_len;
    std::memcpy(out, in, copy_len);
    out[copy_len] = '\0';
}

bool isZeroBytes(const uint8_t* data, std::size_t len)
{
    if (!data)
    {
        return true;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (data[i] != 0)
        {
            return false;
        }
    }
    return true;
}

bool peerHasUsableRatchet(const PeerInfo& peer)
{
    return peer.has_ratchet &&
           !isZeroBytes(peer.ratchet_pub, sizeof(peer.ratchet_pub));
}

bool copyTextAppDataDisplayName(const uint8_t* data,
                                std::size_t len,
                                char* out,
                                std::size_t out_len)
{
    if (!data || len == 0 || len > 96 || !out || out_len == 0)
    {
        return false;
    }

    std::size_t used = 0;
    bool has_visible = false;
    for (std::size_t index = 0; index < len; ++index)
    {
        uint8_t byte = data[index];
        if (byte == '\t' || byte == '\r' || byte == '\n')
        {
            byte = ' ';
        }
        else if (byte == 0 || byte < 0x20 || byte == 0x7F)
        {
            out[0] = '\0';
            return false;
        }

        if (used + 1U < out_len)
        {
            out[used++] = static_cast<char>(byte);
        }
        if (byte != ' ')
        {
            has_visible = true;
        }
    }
    while (used != 0 && out[used - 1U] == ' ')
    {
        --used;
    }
    out[used] = '\0';
    return has_visible && used != 0;
}

bool isNameHash(const reticulum::ParsedAnnounce& announce,
                const char* app_name,
                const char* aspect)
{
    if (!announce.valid || !announce.name_hash || !app_name || !aspect)
    {
        return false;
    }
    uint8_t expected_name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash(app_name, aspect, expected_name_hash);
    return hashesEqual(expected_name_hash,
                       announce.name_hash,
                       sizeof(expected_name_hash));
}

bool isLxmfDeliveryAnnounce(const reticulum::ParsedAnnounce& announce)
{
    return isNameHash(announce, "lxmf", "delivery");
}

bool isLxmfPropagationAnnounce(const reticulum::ParsedAnnounce& announce)
{
    return isNameHash(announce, "lxmf", "propagation");
}

bool isLxstTelephonyAnnounce(const reticulum::ParsedAnnounce& announce)
{
    return isNameHash(announce, "lxst", "telephony");
}

bool isCallAudioAnnounce(const reticulum::ParsedAnnounce& announce)
{
    return isNameHash(announce, "call", "audio") ||
           isLxstTelephonyAnnounce(announce);
}

bool isNomadNetworkNodeAnnounce(const reticulum::ParsedAnnounce& announce)
{
    return isNameHash(announce, "nomadnetwork", "node");
}

void destinationHashForAspect(
    const uint8_t identity_hash[reticulum::kTruncatedHashSize],
    const char* aspect,
    uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    if (!identity_hash || !aspect || !out_hash)
    {
        return;
    }
    uint8_t name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf", aspect, name_hash);
    reticulum::computeDestinationHash(name_hash, identity_hash, out_hash);
}

} // namespace
void detail::projectVerifiedAnnounce(const reticulum::ParsedPacket& packet, AnnounceIngestResult& result, DestinationRegistry& destination_registry, const VerifiedAnnounceProjectionOptions& options)
{
    if (!result.announce.valid || !packet.destination_hash) return;
    const uint8_t* sig_pub = result.announce.public_key + reticulum::kEncryptionPublicKeySize;
    result.delivery_announce = isLxmfDeliveryAnnounce(result.announce);
    result.propagation_announce = isLxmfPropagationAnnounce(result.announce);
    result.call_audio_announce = isCallAudioAnnounce(result.announce);
    result.lxst_telephony_announce = isLxstTelephonyAnnounce(result.announce);
    result.nomad_node_announce = isNomadNetworkNodeAnnounce(result.announce);
    result.contact_announce =
        result.delivery_announce || result.lxst_telephony_announce;
    result.packet_has_ratchet =
        result.announce.has_ratchet &&
        result.announce.ratchet &&
        result.announce.ratchet_len == reticulum::kRatchetSize &&
        !isZeroBytes(result.announce.ratchet, result.announce.ratchet_len);

    bool has_stamp_cost = false;
    uint8_t stamp_cost = 0;
    if (result.call_audio_announce && result.announce.app_data &&
        result.announce.app_data_len != 0)
    {
        (void)copyTextAppDataDisplayName(result.announce.app_data,
                                         result.announce.app_data_len,
                                         result.display_name,
                                         sizeof(result.display_name));
    }
    else if (result.delivery_announce && result.announce.app_data &&
             result.announce.app_data_len != 0 &&
             unpackPeerAnnounceAppData(result.announce.app_data,
                                       result.announce.app_data_len,
                                       result.display_name,
                                       sizeof(result.display_name),
                                       &has_stamp_cost,
                                       &stamp_cost))
    {
        (void)has_stamp_cost;
        (void)stamp_cost;
    }
    else if (result.nomad_node_announce && result.announce.app_data &&
             result.announce.app_data_len != 0)
    {
        (void)copyTextAppDataDisplayName(result.announce.app_data,
                                         result.announce.app_data_len,
                                         result.display_name,
                                         sizeof(result.display_name));
    }
    else if (!(result.delivery_announce || result.propagation_announce ||
               result.call_audio_announce || result.nomad_node_announce) &&
             result.announce.app_data && result.announce.app_data_len != 0)
    {
        (void)copyTextAppDataDisplayName(result.announce.app_data,
                                         result.announce.app_data_len,
                                         result.display_name,
                                         sizeof(result.display_name));
    }
    if ((result.delivery_announce ||
         (result.call_audio_announce && !result.lxst_telephony_announce)) &&
        result.display_name[0] == '\0')
    {
        copyCString(result.display_name,
                    sizeof(result.display_name),
                    kAnonymousPeerDisplayName);
    }
    else if (result.nomad_node_announce && result.display_name[0] == '\0')
    {
        copyCString(result.display_name,
                    sizeof(result.display_name),
                    kAnonymousNodeDisplayName);
    }

    if (result.contact_announce)
    {
        uint8_t peer_destination_hash[reticulum::kTruncatedHashSize] = {};
        if (result.delivery_announce)
        {
            copyHash(peer_destination_hash,
                     packet.destination_hash,
                     sizeof(peer_destination_hash));
        }
        else
        {
            destinationHashForAspect(result.identity_hash,
                                     "delivery",
                                     peer_destination_hash);
        }

        PeerInfo& peer = destination_registry.upsertDestinationBounded(peer_destination_hash, options.max_destinations);
        const uint32_t previous_seen_s = peer.last_seen_s;
        const bool delivery_ratchet_available =
            result.delivery_announce && result.packet_has_ratchet;
        result.ratchet_changed =
            result.delivery_announce &&
            (peerHasUsableRatchet(peer) != delivery_ratchet_available ||
             (delivery_ratchet_available &&
              std::memcmp(peer.ratchet_pub,
                          result.announce.ratchet,
                          sizeof(peer.ratchet_pub)) != 0));
        result.identity_changed =
            isZeroBytes(peer.identity_hash, sizeof(peer.identity_hash)) ||
            !hashesEqual(peer.identity_hash,
                         result.identity_hash,
                         sizeof(peer.identity_hash)) ||
            std::memcmp(peer.enc_pub,
                        result.announce.public_key,
                        sizeof(peer.enc_pub)) != 0 ||
            std::memcmp(peer.sig_pub, sig_pub, sizeof(peer.sig_pub)) != 0;
        result.display_changed =
            result.display_name[0] != '\0' &&
            std::strncmp(peer.display_name,
                         result.display_name,
                         sizeof(peer.display_name)) != 0;

        copyHash(peer.identity_hash,
                 result.identity_hash,
                 sizeof(peer.identity_hash));
        std::memcpy(peer.enc_pub, result.announce.public_key, sizeof(peer.enc_pub));
        std::memcpy(peer.sig_pub, sig_pub, sizeof(peer.sig_pub));
        peer.last_seen_s = options.now_s;
        if (result.delivery_announce)
        {
            if (delivery_ratchet_available)
            {
                std::memcpy(peer.ratchet_pub,
                            result.announce.ratchet,
                            sizeof(peer.ratchet_pub));
                peer.has_ratchet = true;
                peer.ratchet_seen_s = options.now_s;
            }
            else
            {
                std::memset(peer.ratchet_pub, 0, sizeof(peer.ratchet_pub));
                peer.has_ratchet = false;
                peer.ratchet_seen_s = 0;
            }
        }
        if (result.display_name[0] != '\0')
        {
            copyCString(peer.display_name,
                        sizeof(peer.display_name),
                        result.display_name);
        }
        else if (peer.display_name[0] == '\0')
        {
            copyCString(peer.display_name,
                        sizeof(peer.display_name),
                        kAnonymousPeerDisplayName);
        }

        result.address_refresh_due =
            previous_seen_s == 0 ||
            (options.now_s >= previous_seen_s &&
             (options.now_s - previous_seen_s) >=
                 options.directory_address_refresh_interval_s);
        result.should_store_address =
            !options.wifi_gateway ||
            result.identity_changed || result.ratchet_changed ||
            result.display_changed || result.address_refresh_due;
        result.learned_peer = &peer;
    }
}
} // namespace chat::lxmf::runtime
