/**
 * @file lxmf_adapter.cpp
 * @brief Device-side LXMF adapter over the existing RNode raw carrier
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_adapter.h"
#include "geocaching/protocol/sign_record.h"

#include "platform/esp/arduino_common/voice/vmp_pager_session.h"

#include "chat/domain/contact_types.h"
#include "chat/domain/reticulum_identity.h"
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "chat/infra/reticulum/audio_call_wire.h"
#include "chat/infra/reticulum/lxst_telephony_wire.h"
#include "chat/infra/voice/vmp_private_crypto.h"
#include "chat/time_utils.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_call_profile.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_delivery_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_link_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_service_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_resource_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_transport_runtime.h"
#include "platform/esp/common/mbedtls_sha256_compat.h"
#include "platform/esp/common/reticulum_crypto_compat.h"
#include "platform/esp/common/reticulum_runtime_compat.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/reticulum_call_runtime.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/reticulum_network_config_runtime.h"
#include "platform/ui/reticulum_page_runtime.h"
#include "platform/ui/reticulum_receive_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "sys/event_bus.h"

#include <bzlib.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#ifndef TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT
#define TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT 0
#endif

namespace chat::lxmf
{
namespace
{
namespace rtdir = ::platform::ui::reticulum_directory;
namespace rtnet = ::platform::ui::reticulum_network_config;
namespace rtpage = ::platform::ui::reticulum_page;
namespace screen_runtime = ::platform::ui::screen;
namespace sha_compat = ::platform::esp::common::crypto;
using PageFailureKind = rtpage::RequestProgress::FailureKind;

constexpr size_t kMaxPacketLen = reticulum::kReticulumMtu;
constexpr size_t kMaxLxmfMessageLen = reticulum::kReticulumMtu;
constexpr size_t kSignedPartMaxLen = reticulum::kReticulumMtu;
constexpr size_t kMaxTokenPlaintextLen = reticulum::kReticulumMtu;
constexpr size_t kPathRequestTagSize = reticulum::kTruncatedHashSize;
constexpr uint32_t kPathRequestMinIntervalMs = 20000;
constexpr uint32_t kPathRefreshAgeS = 300;
constexpr size_t kMaxPersistedPeers = 64;
constexpr size_t kMaxPaths = 96;
constexpr size_t kMaxPacketFilter = 128;
constexpr size_t kMaxReverseEntries = 64;
constexpr size_t kMaxLinkRelays = 24;
constexpr size_t kMaxLinkSessions = 12;
constexpr size_t kMaxPendingPathRequests = 32;
constexpr uint32_t kLinkSessionTtlMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kLinkHandshakeTimeoutMs = 30000;
constexpr uint32_t kLinkIdleTimeoutMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kLinkRequestTtlMs = 60000;
constexpr uint32_t kNomadPageLinkRetryMs = 10000;
constexpr uint32_t kPendingPathRequestTtlMs = 45000;
constexpr uint32_t kLinkKeepaliveMinMs = 5000;
constexpr uint32_t kLinkKeepaliveMaxMs = 360000;
constexpr float kLinkKeepaliveMaxRttS = 1.75f;
constexpr uint32_t kLinkKeepaliveTimeoutFactor = 4;
constexpr uint32_t kLinkStaleGraceMs = 5000;
constexpr uint32_t kResourceTransferTtlMs = 60000;
constexpr uint32_t kLinkPacketReceiptTtlMs = 60000;
constexpr uint32_t kResourceWindowSize = 4;
constexpr size_t kMaxPropagationEntries = 64;
constexpr size_t kMaxPropagationTransients = 192;
constexpr size_t kMaxPropagationPeers = 32;
constexpr size_t kMaxPendingPropagationUploads = 4;
constexpr uint32_t kPropagationEntryTtlS = 3UL * 24UL * 60UL * 60UL;
constexpr uint32_t kPropagationTransientTtlS = 3UL * 24UL * 60UL * 60UL;
constexpr uint32_t kPropagationUploadTtlMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kPropagationTransferLimitKb = 64;
constexpr uint32_t kPropagationSyncLimitKb = 64;
constexpr uint32_t kLoraDiscoveryForegroundDetailLogIntervalMs = 15000;
constexpr uint32_t kLoraDiscoverySleepDetailLogIntervalMs = 10000;
constexpr uint32_t kLoraAnnounceIgnoreDetailLogIntervalMs = 10000;
constexpr uint8_t kLinkModeAes256Cbc = 0x01;
constexpr size_t kLinkRequestBaseLen = 64;
constexpr size_t kLinkSignallingLen = 3;
constexpr size_t kResourceMapHashLen = 4;
constexpr size_t kResourceDataPrefixLen = 4;
constexpr size_t kResourceAdvertisementOverhead = 134;
constexpr uint8_t kResourceFlagEncrypted = 1U << 0U;
constexpr uint8_t kResourceFlagCompressed = 1U << 1U;
constexpr uint8_t kResourceFlagSplit = 1U << 2U;
constexpr uint8_t kResourceFlagRequest = 1U << 3U;
constexpr uint8_t kResourceFlagResponse = 1U << 4U;
constexpr uint8_t kResourceFlagHasMetadata = 1U << 5U;
constexpr uint32_t kPacketFilterTtlMs = 30000;
constexpr uint32_t kReverseEntryTtlMs = 60000;
constexpr uint32_t kLinkRelayTtlMs = 300000;
constexpr uint32_t kPathTtlMs = 7UL * 24UL * 60UL * 60UL * 1000UL;
constexpr size_t kMaxPendingPingReceipts = 4;
constexpr uint32_t kPendingPingReceiptTtlMs = 30000;
constexpr size_t kMaxPendingDeliveryReceipts = 8;
constexpr uint32_t kPendingDeliveryReceiptTtlMs = 60000;
constexpr uint32_t kPendingPingSendRetryMs = 1500;
constexpr uint32_t kDirectoryAddressRefreshIntervalS = 300;
constexpr uint8_t kMaxTransportHops = 128;
constexpr const char* kAnonymousPeerDisplayName = "Anonymous Peer";
constexpr const char* kAnonymousNodeDisplayName = "Anonymous Node";
constexpr uint8_t kPropagationMetaName = 0x01;

void formatHashPrefix(const uint8_t* hash, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!hash || out_len < 9)
    {
        snprintf(out, out_len, "-");
        return;
    }
    snprintf(out,
             out_len,
             "%02X%02X%02X%02X",
             static_cast<unsigned>(hash[0]),
             static_cast<unsigned>(hash[1]),
             static_cast<unsigned>(hash[2]),
             static_cast<unsigned>(hash[3]));
}

void formatHashHex(const uint8_t* hash, size_t hash_len, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!hash || hash_len == 0 || out_len < ((hash_len * 2U) + 1U))
    {
        snprintf(out, out_len, "-");
        return;
    }

    size_t used = 0;
    for (size_t index = 0; index < hash_len && used + 2U < out_len; ++index)
    {
        used += static_cast<size_t>(
            snprintf(out + used,
                     out_len - used,
                     "%02X",
                     static_cast<unsigned>(hash[index])));
    }
}

bool decodeMsgpackByteString(const uint8_t* packed,
                             size_t packed_len,
                             runtime::ResourcePayloadBuffer* out)
{
    if (!packed || packed_len == 0 || !out)
    {
        return false;
    }

    const uint8_t marker = packed[0];
    size_t offset = 1;
    size_t len = 0;
    if ((marker & 0xE0U) == 0xA0U)
    {
        len = marker & 0x1FU;
    }
    else if (marker == 0xC4 || marker == 0xD9)
    {
        if (packed_len < 2)
        {
            return false;
        }
        len = packed[1];
        offset = 2;
    }
    else if (marker == 0xC5 || marker == 0xDA)
    {
        if (packed_len < 3)
        {
            return false;
        }
        len = (static_cast<size_t>(packed[1]) << 8) |
              static_cast<size_t>(packed[2]);
        offset = 3;
    }
    else if (marker == 0xC6 || marker == 0xDB)
    {
        if (packed_len < 5)
        {
            return false;
        }
        len = (static_cast<size_t>(packed[1]) << 24) |
              (static_cast<size_t>(packed[2]) << 16) |
              (static_cast<size_t>(packed[3]) << 8) |
              static_cast<size_t>(packed[4]);
        offset = 5;
    }
    else
    {
        return false;
    }

    if (offset > packed_len || len > (packed_len - offset))
    {
        return false;
    }
    out->assign(packed + offset, packed + offset + len);
    return true;
}

void formatLogTextPreview(const std::string& text, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    const size_t max_copy = out_len - 1U;
    size_t used = 0;
    for (char value : text)
    {
        if (used >= max_copy)
        {
            break;
        }
        const unsigned char c = static_cast<unsigned char>(value);
        if (c == '\r' || c == '\n' || c == '\t')
        {
            out[used++] = ' ';
        }
        else if (c < 0x20U || c == 0x7FU)
        {
            out[used++] = '.';
        }
        else
        {
            out[used++] = value;
        }
    }
    out[used] = '\0';
}

const char* txBearerName(const reticulum::interfaces::TxResult& result)
{
    if (result.lora_ok && result.wifi_ok)
    {
        return "lora+wifi";
    }
    if (result.lora_ok)
    {
        return "lora";
    }
    if (result.wifi_ok)
    {
        return "wifi";
    }
    return "none";
}

const char* localDestinationKindLabel(runtime::LocalDestinationKind kind)
{
    switch (kind)
    {
    case runtime::LocalDestinationKind::Propagation:
        return "propagation";
    case runtime::LocalDestinationKind::CallAudio:
        return "call_audio";
    case runtime::LocalDestinationKind::NomadPage:
        return "nomad_page";
    case runtime::LocalDestinationKind::Delivery:
    default:
        return "delivery";
    }
}

const char* linkStateLabel(runtime::LinkState state)
{
    switch (state)
    {
    case runtime::LinkState::Pending:
        return "pending";
    case runtime::LinkState::Handshake:
        return "handshake";
    case runtime::LinkState::Active:
        return "active";
    case runtime::LinkState::Stale:
        return "stale";
    case runtime::LinkState::Closed:
        return "closed";
    }
    return "unknown";
}

void logNomadLinkProofEvent(const runtime::LinkSession& session,
                            const char* event,
                            const char* reason,
                            uint32_t first = 0,
                            uint32_t second = 0)
{
    if (session.destination != runtime::LocalDestinationKind::NomadPage)
    {
        return;
    }

    char destination_hash[12] = {};
    char link_hash[12] = {};
    formatHashPrefix(session.remote_destination_hash,
                     destination_hash,
                     sizeof(destination_hash));
    formatHashPrefix(session.link_id, link_hash, sizeof(link_hash));
    Serial.printf("[LXMF][LinkRX] proof_%s kind=nomad_page reason=%s dest=%s link=%s state=%s a=%lu b=%lu\n",
                  event ? event : "event",
                  reason ? reason : "-",
                  destination_hash,
                  link_hash,
                  linkStateLabel(session.state),
                  static_cast<unsigned long>(first),
                  static_cast<unsigned long>(second));
}

#ifndef LXMF_NOMAD_PAGE_TRACE
#define LXMF_NOMAD_PAGE_TRACE 0
#endif
#ifndef LXMF_LINK_PROOF_TRACE
#define LXMF_LINK_PROOF_TRACE 0
#endif
constexpr bool kNomadPageTraceEnabled = LXMF_NOMAD_PAGE_TRACE != 0;

#if LXMF_NOMAD_PAGE_TRACE
#define LXMF_NOMAD_PAGE_LOG(...)                         \
    do                                                   \
    {                                                    \
        Serial.printf("[LXMF][NomadPage] " __VA_ARGS__); \
    } while (0)
#else
#define LXMF_NOMAD_PAGE_LOG(...) \
    do                           \
    {                            \
    } while (0)
#endif

#if LXMF_LINK_PROOF_TRACE
#define LXMF_LINK_PROOF_LOG(...)                         \
    do                                                   \
    {                                                    \
        Serial.printf("[LXMF][LinkProof] " __VA_ARGS__); \
    } while (0)
#else
#define LXMF_LINK_PROOF_LOG(...) \
    do                           \
    {                            \
    } while (0)
#endif

bool appendMsgpackByte(uint8_t value, uint8_t* out, size_t out_len, size_t& used)
{
    if (!out || used >= out_len)
    {
        return false;
    }

    out[used++] = value;
    return true;
}

bool appendMsgpackBytes(const uint8_t* data, size_t len, uint8_t* out, size_t out_len, size_t& used)
{
    if ((!data && len != 0) || !out || used > out_len || len > (out_len - used))
    {
        return false;
    }

    if (len != 0)
    {
        memcpy(out + used, data, len);
    }
    used += len;
    return true;
}

bool appendMsgpackArrayHeader(uint8_t count, uint8_t* out, size_t out_len, size_t& used)
{
    if (count <= 0x0FU)
    {
        return appendMsgpackByte(static_cast<uint8_t>(0x90U | count), out, out_len, used);
    }

    return appendMsgpackByte(0xDC, out, out_len, used) &&
           appendMsgpackByte(0x00, out, out_len, used) &&
           appendMsgpackByte(count, out, out_len, used);
}

bool appendMsgpackMapHeader(uint8_t count, uint8_t* out, size_t out_len, size_t& used)
{
    if (count <= 0x0FU)
    {
        return appendMsgpackByte(static_cast<uint8_t>(0x80U | count), out, out_len, used);
    }

    return appendMsgpackByte(0xDE, out, out_len, used) &&
           appendMsgpackByte(0x00, out, out_len, used) &&
           appendMsgpackByte(count, out, out_len, used);
}

bool appendMsgpackBool(bool value, uint8_t* out, size_t out_len, size_t& used)
{
    return appendMsgpackByte(value ? 0xC3 : 0xC2, out, out_len, used);
}

bool appendMsgpackUint(uint32_t value, uint8_t* out, size_t out_len, size_t& used)
{
    if (value <= 0x7FU)
    {
        return appendMsgpackByte(static_cast<uint8_t>(value), out, out_len, used);
    }
    if (value <= 0xFFU)
    {
        return appendMsgpackByte(0xCC, out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>(value), out, out_len, used);
    }
    if (value <= 0xFFFFU)
    {
        return appendMsgpackByte(0xCD, out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>((value >> 8) & 0xFFU), out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>(value & 0xFFU), out, out_len, used);
    }

    return appendMsgpackByte(0xCE, out, out_len, used) &&
           appendMsgpackByte(static_cast<uint8_t>((value >> 24) & 0xFFU), out, out_len, used) &&
           appendMsgpackByte(static_cast<uint8_t>((value >> 16) & 0xFFU), out, out_len, used) &&
           appendMsgpackByte(static_cast<uint8_t>((value >> 8) & 0xFFU), out, out_len, used) &&
           appendMsgpackByte(static_cast<uint8_t>(value & 0xFFU), out, out_len, used);
}

bool appendMsgpackBin(const uint8_t* data, size_t len, uint8_t* out, size_t out_len, size_t& used)
{
    if (len <= 0xFFU)
    {
        return appendMsgpackByte(0xC4, out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>(len), out, out_len, used) &&
               appendMsgpackBytes(data, len, out, out_len, used);
    }
    if (len <= 0xFFFFU)
    {
        return appendMsgpackByte(0xC5, out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>((len >> 8) & 0xFFU), out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>(len & 0xFFU), out, out_len, used) &&
               appendMsgpackBytes(data, len, out, out_len, used);
    }

    return false;
}

bool packPropagationAnnounceAppData(uint32_t timebase_s,
                                    bool node_state,
                                    uint32_t per_transfer_limit_kb,
                                    uint32_t per_sync_limit_kb,
                                    const char* display_name,
                                    uint8_t* out_data,
                                    size_t* inout_len)
{
    if (!out_data || !inout_len)
    {
        return false;
    }

    const uint8_t* name_bytes = reinterpret_cast<const uint8_t*>(display_name ? display_name : "");
    const size_t name_len = (display_name && display_name[0] != '\0') ? strlen(display_name) : 0;

    size_t used = 0;
    const uint8_t metadata_entries = (name_len != 0) ? 1 : 0;
    if (!appendMsgpackArrayHeader(7, out_data, *inout_len, used) ||
        !appendMsgpackBool(false, out_data, *inout_len, used) ||
        !appendMsgpackUint(timebase_s, out_data, *inout_len, used) ||
        !appendMsgpackBool(node_state, out_data, *inout_len, used) ||
        !appendMsgpackUint(per_transfer_limit_kb, out_data, *inout_len, used) ||
        !appendMsgpackUint(per_sync_limit_kb, out_data, *inout_len, used) ||
        !appendMsgpackArrayHeader(3, out_data, *inout_len, used) ||
        !appendMsgpackUint(0, out_data, *inout_len, used) ||
        !appendMsgpackUint(0, out_data, *inout_len, used) ||
        !appendMsgpackUint(0, out_data, *inout_len, used) ||
        !appendMsgpackMapHeader(metadata_entries, out_data, *inout_len, used))
    {
        return false;
    }

    if (metadata_entries != 0 &&
        (!appendMsgpackUint(kPropagationMetaName, out_data, *inout_len, used) ||
         !appendMsgpackBin(name_bytes, name_len, out_data, *inout_len, used)))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

void destinationHashForServiceAspect(
    const uint8_t identity_hash[reticulum::kTruncatedHashSize],
    const char* app_name,
    const char* aspect,
    uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    if (!identity_hash || !app_name || !aspect || !out_hash)
    {
        return;
    }

    uint8_t name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash(app_name, aspect, name_hash);
    reticulum::computeDestinationHash(name_hash, identity_hash, out_hash);
}

void destinationHashForAspect(const uint8_t identity_hash[reticulum::kTruncatedHashSize],
                              const char* aspect,
                              uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    destinationHashForServiceAspect(identity_hash, "lxmf", aspect, out_hash);
}

void callDestinationHashForIdentity(
    const uint8_t identity_hash[reticulum::kTruncatedHashSize],
    ReticulumCallWireProfile profile,
    uint8_t out_hash[reticulum::kTruncatedHashSize])
{
#if TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT
    if (profile == ReticulumCallWireProfile::MeshChatCallAudio)
    {
        destinationHashForServiceAspect(identity_hash, "call", "audio", out_hash);
    }
    else
#else
    (void)profile;
#endif
    {
        destinationHashForServiceAspect(identity_hash, "lxst", "telephony", out_hash);
    }
}

void fillRandomBytes(uint8_t* out, size_t len)
{
    if (!out || len == 0)
    {
        return;
    }

    size_t offset = 0;
    while (offset < len)
    {
        const uint32_t rnd = static_cast<uint32_t>(esp_random());
        const size_t chunk = (len - offset >= sizeof(rnd)) ? sizeof(rnd) : (len - offset);
        memcpy(out + offset, &rnd, chunk);
        offset += chunk;
    }
}

bool isZeroBytes(const uint8_t* data, size_t len)
{
    if (!data)
    {
        return true;
    }
    for (size_t i = 0; i < len; ++i)
    {
        if (data[i] != 0)
        {
            return false;
        }
    }
    return true;
}

bool peerHasUsableRatchet(const runtime::PeerInfo& peer)
{
    return peer.has_ratchet &&
           !isZeroBytes(peer.ratchet_pub, sizeof(peer.ratchet_pub));
}

void formatRatchetIdPrefix(const uint8_t* ratchet_pub, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!ratchet_pub || isZeroBytes(ratchet_pub, reticulum::kRatchetSize))
    {
        snprintf(out, out_len, "-");
        return;
    }

    uint8_t ratchet_hash[reticulum::kFullHashSize] = {};
    reticulum::fullHash(ratchet_pub, reticulum::kRatchetSize, ratchet_hash);
    formatHashPrefix(ratchet_hash, out, out_len);
}

bool hashesEqual(const uint8_t* a, const uint8_t* b, size_t len)
{
    if ((!a || !b) && len != 0)
    {
        return false;
    }
    for (size_t i = 0; i < len; ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

bool fullHashJoined(const uint8_t* first,
                    size_t first_len,
                    const uint8_t* second,
                    size_t second_len,
                    uint8_t out_hash[reticulum::kFullHashSize])
{
    if (!out_hash || (!first && first_len != 0) || (!second && second_len != 0))
    {
        return false;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool ok = sha_compat::sha256_starts(&sha, 0) == 0;
    if (ok && first_len != 0)
    {
        ok = sha_compat::sha256_update(&sha, first, first_len) == 0;
    }
    if (ok && second_len != 0)
    {
        ok = sha_compat::sha256_update(&sha, second, second_len) == 0;
    }
    if (ok)
    {
        ok = sha_compat::sha256_finish(&sha, out_hash) == 0;
    }
    mbedtls_sha256_free(&sha);
    if (!ok)
    {
        std::memset(out_hash, 0, reticulum::kFullHashSize);
    }
    return ok;
}

bool unpackRnsLxmfEnvelope(const uint8_t expected_destination_hash[reticulum::kTruncatedHashSize],
                           const uint8_t* payload, size_t payload_len,
                           DecodedEnvelope* out_envelope,
                           bool* out_embedded_destination_hash)
{
    if (!expected_destination_hash || !payload || payload_len == 0 || !out_envelope)
    {
        return false;
    }

    const size_t full_envelope_min_len =
        (reticulum::kTruncatedHashSize * 2U) + reticulum::kSignatureSize;
    if (payload_len >= full_envelope_min_len &&
        hashesEqual(payload, expected_destination_hash, reticulum::kTruncatedHashSize) &&
        unpackMessageEnvelope(payload, payload_len, out_envelope))
    {
        if (out_embedded_destination_hash)
        {
            *out_embedded_destination_hash = true;
        }
        return true;
    }

    const size_t opportunistic_tail_min_len =
        reticulum::kTruncatedHashSize + reticulum::kSignatureSize;
    if (payload_len < opportunistic_tail_min_len)
    {
        return false;
    }

    runtime::RuntimeByteBuffer full_payload(reticulum::kTruncatedHashSize + payload_len);
    memcpy(full_payload.data(), expected_destination_hash, reticulum::kTruncatedHashSize);
    memcpy(full_payload.data() + reticulum::kTruncatedHashSize, payload, payload_len);
    if (!unpackMessageEnvelope(full_payload.data(), full_payload.size(), out_envelope))
    {
        return false;
    }
    if (out_embedded_destination_hash)
    {
        *out_embedded_destination_hash = false;
    }
    return true;
}

void copyHash(uint8_t* out, const uint8_t* in, size_t len)
{
    if (!out || !in || len == 0)
    {
        return;
    }
    memcpy(out, in, len);
}

MeshPeerSource meshPeerSourceFromDirectorySource(rtdir::EntrySource source)
{
    switch (source)
    {
    case rtdir::EntrySource::RuntimeRx:
        return MeshPeerSource::RuntimeRx;
    case rtdir::EntrySource::PathResponse:
        return MeshPeerSource::DiscoveryResponse;
    case rtdir::EntrySource::Manual:
        return MeshPeerSource::Manual;
    case rtdir::EntrySource::Import:
        return MeshPeerSource::Import;
    case rtdir::EntrySource::Unknown:
    default:
        return MeshPeerSource::Unknown;
    }
}

void copyCString(char* out, size_t out_len, const char* in)
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

    const size_t max_copy_len = out_len - 1U;
    const auto* terminator = static_cast<const char*>(std::memchr(in, '\0', max_copy_len));
    const size_t copy_len =
        terminator ? static_cast<size_t>(terminator - in) : max_copy_len;
    memcpy(out, in, copy_len);
    out[copy_len] = '\0';
}

bool isNomadNetworkNodeAnnounce(const reticulum::ParsedAnnounce& announce)
{
    if (!announce.valid || !announce.name_hash)
    {
        return false;
    }

    uint8_t expected_name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("nomadnetwork", "node", expected_name_hash);
    return hashesEqual(expected_name_hash, announce.name_hash, sizeof(expected_name_hash));
}

bool isLxstTelephonyAnnounce(const reticulum::ParsedAnnounce& announce)
{
    if (!announce.valid || !announce.name_hash)
    {
        return false;
    }

    uint8_t expected_name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxst", "telephony", expected_name_hash);
    return hashesEqual(expected_name_hash, announce.name_hash, sizeof(expected_name_hash));
}

bool isLxstTelephonyAnnouncePacket(const reticulum::ParsedPacket& packet)
{
    if (!packet.valid || packet.packet_type != reticulum::PacketType::Announce)
    {
        return false;
    }

    reticulum::ParsedAnnounce announce{};
    return reticulum::parseAnnounce(packet, &announce) &&
           isLxstTelephonyAnnounce(announce);
}

bool isLoRaInterfaceId(reticulum::interfaces::InterfaceId interface_id)
{
    return interface_id == reticulum::interfaces::kLoRaInterfaceId;
}

bool isLoRaPath(const runtime::PathEntry* path)
{
    return path && isLoRaInterfaceId(path->interface_id);
}

bool packetContextUsesRawLinkPayload(uint8_t context)
{
    return context == static_cast<uint8_t>(reticulum::PacketContext::Keepalive) ||
           context == static_cast<uint8_t>(reticulum::PacketContext::Resource);
}

size_t resourceHashmapSegmentCapacity(uint16_t mdu)
{
    if (mdu <= kResourceAdvertisementOverhead)
    {
        return 1;
    }

    const size_t available = static_cast<size_t>(mdu) - kResourceAdvertisementOverhead;
    return std::max<size_t>(1, available / kResourceMapHashLen);
}

void* bzip2PsramAlloc(void*, int items, int size)
{
    if (items <= 0 || size <= 0)
    {
        return nullptr;
    }

    const size_t count = static_cast<size_t>(items);
    const size_t item_size = static_cast<size_t>(size);
    if (count > (std::numeric_limits<size_t>::max() / item_size))
    {
        return nullptr;
    }

    const size_t bytes = count * item_size;
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void bzip2PsramFree(void*, void* address)
{
    if (address)
    {
        heap_caps_free(address);
    }
}

bool decompressBzip2Payload(const uint8_t* compressed,
                            size_t compressed_len,
                            size_t expected_size,
                            runtime::ResourcePayloadBuffer* out_payload,
                            int* out_status)
{
    if (out_status)
    {
        *out_status = BZ_PARAM_ERROR;
    }
    if (!compressed || compressed_len == 0 || !out_payload ||
        compressed_len > std::numeric_limits<unsigned int>::max() ||
        expected_size > std::numeric_limits<unsigned int>::max())
    {
        return false;
    }

    runtime::ResourcePayloadBuffer output(expected_size, 0);
    bz_stream stream{};
    stream.bzalloc = bzip2PsramAlloc;
    stream.bzfree = bzip2PsramFree;
    stream.opaque = nullptr;
    stream.next_in = reinterpret_cast<char*>(const_cast<uint8_t*>(compressed));
    stream.avail_in = static_cast<unsigned int>(compressed_len);
    stream.next_out = reinterpret_cast<char*>(output.data());
    stream.avail_out = static_cast<unsigned int>(output.size());

    int status = BZ2_bzDecompressInit(&stream, 0, 1);
    if (status != BZ_OK)
    {
        if (out_status)
        {
            *out_status = status;
        }
        return false;
    }

    do
    {
        status = BZ2_bzDecompress(&stream);
        if (status == BZ_STREAM_END)
        {
            break;
        }
        if (status != BZ_OK)
        {
            (void)BZ2_bzDecompressEnd(&stream);
            if (out_status)
            {
                *out_status = status;
            }
            return false;
        }
        if (stream.avail_out == 0)
        {
            (void)BZ2_bzDecompressEnd(&stream);
            if (out_status)
            {
                *out_status = BZ_OUTBUFF_FULL;
            }
            return false;
        }
    } while (stream.avail_in > 0 || stream.avail_out > 0);

    const size_t produced = output.size() - stream.avail_out;
    const int end_status = BZ2_bzDecompressEnd(&stream);
    if (status != BZ_STREAM_END || end_status != BZ_OK || produced != expected_size)
    {
        if (out_status)
        {
            *out_status = status != BZ_STREAM_END ? status : end_status;
        }
        return false;
    }

    output.resize(produced);
    *out_payload = std::move(output);
    if (out_status)
    {
        *out_status = BZ_OK;
    }
    return true;
}

void buildLinkSignallingBytes(uint16_t mtu, uint8_t out_bytes[kLinkSignallingLen])
{
    if (!out_bytes)
    {
        return;
    }

    const uint32_t signalling_value =
        (static_cast<uint32_t>(mtu) & 0x1FFFFFU) |
        ((static_cast<uint32_t>((kLinkModeAes256Cbc << 5) & 0xE0U)) << 16);
    out_bytes[0] = static_cast<uint8_t>((signalling_value >> 16) & 0xFFU);
    out_bytes[1] = static_cast<uint8_t>((signalling_value >> 8) & 0xFFU);
    out_bytes[2] = static_cast<uint8_t>(signalling_value & 0xFFU);
}

uint16_t mtuFromLinkSignalling(const uint8_t* signalling_bytes, size_t len)
{
    if (!signalling_bytes || len < kLinkSignallingLen)
    {
        return reticulum::kReticulumMtu;
    }
    const uint32_t mtu =
        ((static_cast<uint32_t>(signalling_bytes[0]) << 16) |
         (static_cast<uint32_t>(signalling_bytes[1]) << 8) |
         static_cast<uint32_t>(signalling_bytes[2])) &
        0x1FFFFFU;
    return static_cast<uint16_t>(std::min<uint32_t>(mtu, reticulum::kReticulumMtu));
}

uint8_t linkModeFromSignalling(const uint8_t* signalling_bytes, size_t len)
{
    if (!signalling_bytes || len < kLinkSignallingLen)
    {
        return kLinkModeAes256Cbc;
    }
    return static_cast<uint8_t>((signalling_bytes[0] & 0xE0U) >> 5);
}

uint32_t keepaliveIntervalForRtt(float rtt_s)
{
    if (rtt_s <= 0.0f)
    {
        return kLinkKeepaliveMaxMs;
    }

    const float scaled_ms =
        rtt_s * (static_cast<float>(kLinkKeepaliveMaxMs) / kLinkKeepaliveMaxRttS);
    const uint32_t keepalive_ms = static_cast<uint32_t>(scaled_ms);
    return std::min<uint32_t>(kLinkKeepaliveMaxMs,
                              std::max<uint32_t>(kLinkKeepaliveMinMs, keepalive_ms));
}

bool packFloat64(double value, uint8_t* out_payload, size_t* inout_len)
{
    if (!out_payload || !inout_len || *inout_len < 9)
    {
        if (inout_len)
        {
            *inout_len = 9;
        }
        return false;
    }

    union
    {
        double d;
        uint8_t b[8];
    } bits{};
    bits.d = value;

    out_payload[0] = 0xCB;
    for (int i = 7; i >= 0; --i)
    {
        out_payload[8 - i] = bits.b[i];
    }
    *inout_len = 9;
    return true;
}

bool unpackFloat64(const uint8_t* data, size_t len, double* out_value)
{
    if (!data || len != 9 || data[0] != 0xCB || !out_value)
    {
        return false;
    }

    union
    {
        double d;
        uint8_t b[8];
    } bits{};
    for (int i = 7; i >= 0; --i)
    {
        bits.b[i] = data[8 - i];
    }
    *out_value = bits.d;
    return true;
}

bool computeLinkIdFromLinkRequest(const uint8_t* raw_packet, size_t raw_len,
                                  const reticulum::ParsedPacket& packet,
                                  uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    if (!raw_packet || raw_len == 0 || !out_hash ||
        packet.packet_type != reticulum::PacketType::LinkRequest ||
        !packet.payload)
    {
        return false;
    }

    if (raw_len > kMaxPacketLen)
    {
        return false;
    }

    size_t working_len = raw_len;
    runtime::RuntimeByteBuffer scratch(raw_len, 0);
    memcpy(scratch.data(), raw_packet, raw_len);

    if (packet.payload_len > 64)
    {
        const size_t trim = packet.payload_len - 64;
        if (trim >= working_len)
        {
            return false;
        }
        working_len -= trim;
    }

    reticulum::computeTruncatedPacketHash(scratch.data(), working_len, out_hash);
    return true;
}

} // namespace

LxmfAdapter::LxmfAdapter(LoraBoard& board,
                         IMeshPeerDirectory* peer_directory,
                         bool owns_integrated_radio)
    : interfaces_(board, owns_integrated_radio),
      peer_directory_service_(peer_directory),
      geocaching_only_(!owns_integrated_radio)
{
    uint8_t seed[sizeof(next_app_packet_id_)] = {};
    fillRandomBytes(seed, sizeof(seed));
    memcpy(&next_app_packet_id_, seed, sizeof(next_app_packet_id_));
    if (next_app_packet_id_ == 0)
    {
        next_app_packet_id_ = 1;
    }
}

void* LxmfAdapter::operator new(std::size_t size)
{
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr)
    {
        Serial.printf("[LXMF] psram_alloc_failed adapter_size=%u\n",
                      static_cast<unsigned>(size));
        std::abort();
    }
    return ptr;
}

void LxmfAdapter::operator delete(void* ptr) noexcept
{
    heap_caps_free(ptr);
}

void LxmfAdapter::operator delete(void* ptr, std::size_t) noexcept
{
    operator delete(ptr);
}

bool LxmfAdapter::resolveLocalDestinationForAnnounce(
    void* context,
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    LocalDestinationKind* out_kind)
{
    auto* adapter = static_cast<LxmfAdapter*>(context);
    return adapter && adapter->isLocalDestinationHash(destination_hash, out_kind);
}

MeshCapabilities LxmfAdapter::getCapabilities() const
{
    MeshCapabilities caps;
    caps.supports_unicast_text = true;
    caps.supports_reticulum_destination_text = true;
    caps.supports_unicast_appdata = true;
    caps.supports_broadcast_appdata = true;
    caps.provides_appdata_sender = true;
    caps.supports_node_info = true;
    caps.supports_reticulum_destination_ping = true;
    caps.supports_reticulum_audio_call = true;
    return caps;
}

bool LxmfAdapter::sendText(ChannelId channel, const std::string& text,
                           MessageId* out_msg_id, NodeId peer)
{
    const MeshSendResult result = sendTextDetailed(channel, text, 0, peer);
    if (out_msg_id)
    {
        *out_msg_id = result.msg_id;
    }
    return result.ok;
}

bool LxmfAdapter::dispatchLxmfPayload(PeerInfo& peer,
                                      const uint8_t* packed_payload,
                                      size_t packed_payload_len,
                                      bool track_user_message,
                                      OutboundLxmfDispatch* out_dispatch)
{
    if (!packed_payload || packed_payload_len == 0 || packed_payload_len > 8448 || !out_dispatch)
    {
        return false;
    }
    *out_dispatch = OutboundLxmfDispatch{};

    if (shouldRequestPath(peer))
    {
        (void)sendPathRequest(peer);
    }

    runtime::RuntimeByteBuffer signed_part(packed_payload_len + 64, 0);
    size_t signed_part_len = signed_part.size();
    if (!buildSignedPart(peer.destination_hash,
                         identity_.destinationHash(),
                         packed_payload,
                         packed_payload_len,
                         signed_part.data(),
                         &signed_part_len,
                         out_dispatch->message_hash))
    {
        out_dispatch->failure = MeshOperationFailure::EncodeFailed;
        return false;
    }

    uint8_t signature[reticulum::kSignatureSize] = {};
    runtime::RuntimeByteBuffer lxmf_message(packed_payload_len + 96, 0);
    size_t lxmf_message_len = lxmf_message.size();
    if (!identity_.sign(signed_part.data(), signed_part_len, signature) ||
        !packMessage(peer.destination_hash,
                     identity_.destinationHash(),
                     signature,
                     packed_payload,
                     packed_payload_len,
                     lxmf_message.data(),
                     &lxmf_message_len))
    {
        out_dispatch->failure = MeshOperationFailure::CryptoFailed;
        return false;
    }

    out_dispatch->message_id = messageIdFromHash(out_dispatch->message_hash);
    LinkSession* active_link =
        findActiveLinkSessionByDestination(peer.destination_hash,
                                           LocalDestinationKind::Delivery);
    const bool use_opportunistic = !active_link && packed_payload_len <= 256 && peerHasUsableRatchet(peer);
    const auto& propagation_config = rtnet::active().propagation;
    bool propagation_peer_available = false;
    if (propagation_config.enabled &&
        propagation_config.delivery ==
            chat::reticulum::LxmfDeliveryPreference::Automatic &&
        !active_link && !use_opportunistic)
    {
        propagation_peer_available = selectActivePropagationPeer() != nullptr;
    }
    const runtime::OutboundDeliveryPlan plan =
        runtime::ReticulumDeliveryPlanner::plan(
            runtime::OutboundDeliveryPlanInput{
                active_link != nullptr,
                use_opportunistic,
                propagation_config.enabled,
                propagation_config.delivery,
                propagation_peer_available});
    if (plan.propagation_first)
    {
        if (queuePropagationUpload(peer,
                                   lxmf_message.data(),
                                   lxmf_message_len,
                                   out_dispatch->message_id,
                                   out_dispatch->message_hash,
                                   track_user_message,
                                   out_dispatch))
        {
            return true;
        }
        if (plan.propagation_only)
        {
            out_dispatch->failure = MeshOperationFailure::RadioTxFailed;
            return false;
        }
    }
    const runtime::OutboundDeliveryPath fallback_path =
        active_link ? runtime::OutboundDeliveryPath::Link
                    : (use_opportunistic
                           ? runtime::OutboundDeliveryPath::Opportunistic
                           : runtime::OutboundDeliveryPath::DeferredLink);
    out_dispatch->path = runtime::ReticulumDeliveryPlanner::pathName(
        plan.propagation_first ? fallback_path : plan.path);
    if (active_link)
    {
        runtime::DeferredLinkPayload deferred{};
        deferred.payload.assign(lxmf_message.data(),
                                lxmf_message.data() + lxmf_message_len);
        deferred.message_id = track_user_message
                                  ? out_dispatch->message_id
                                  : 0;
        link_manager_.appendDeferredPayload(*active_link, std::move(deferred));
        flushDeferredLinkPayloads(*active_link);
        out_dispatch->ok = true;
        out_dispatch->result_event_deferred = track_user_message;
    }
    else if (use_opportunistic)
    {
        runtime::RuntimeByteBuffer packet(kMaxPacketLen, 0);
        size_t packet_len = packet.size();
        out_dispatch->ok =
            buildSignedMessagePacket(peer,
                                     packed_payload,
                                     packed_payload_len,
                                     packet.data(),
                                     &packet_len,
                                     out_dispatch->message_hash) &&
            routeAndSendPacket(packet.data(), packet_len, true);
        if (out_dispatch->ok && track_user_message)
        {
            uint8_t packet_hash[reticulum::kFullHashSize] = {};
            reticulum::computePacketHash(packet.data(), packet_len, packet_hash);
            delivery_attempt_ledger_.noteDirectPacketReceipt(
                packet_hash,
                peer.destination_hash,
                peer.sig_pub,
                out_dispatch->message_id,
                millis(),
                kMaxPendingDeliveryReceipts);
        }
    }

    if (plan.may_fallback_to_link && !active_link && !out_dispatch->ok)
    {
        bool started = false;
        LinkSession* session =
            ensureOutboundLinkSession(peer, LocalDestinationKind::Delivery, &started);
        if (session)
        {
            runtime::DeferredLinkPayload deferred{};
            deferred.payload.assign(lxmf_message.data(),
                                    lxmf_message.data() + lxmf_message_len);
            deferred.message_id = track_user_message
                                      ? out_dispatch->message_id
                                      : 0;
            link_manager_.appendDeferredPayload(*session, std::move(deferred));
            if (session->state == LinkState::Active)
            {
                flushDeferredLinkPayloads(*session);
            }
            out_dispatch->ok = true;
            out_dispatch->result_event_deferred = track_user_message;
        }
    }

    if (!out_dispatch->ok)
    {
        out_dispatch->failure = MeshOperationFailure::RadioTxFailed;
    }
    return out_dispatch->ok;
}

const LxmfAdapter::PropagationPeerState*
LxmfAdapter::selectActivePropagationPeer()
{
    const auto& config = rtnet::active().propagation;
    if (!config.enabled)
    {
        propagation_client_.clearActivePeer();
        return nullptr;
    }

    const uint32_t now_s = currentTimestampSeconds();
    const runtime::PropagationActivePeerSelection selection =
        propagation_client_.selectActivePeer(config.automatic_node,
                                             config.node_hash,
                                             now_s,
                                             kPropagationEntryTtlS,
                                             config.sync_on_start);
    const PropagationPeerState* selected = selection.peer;
    if (!selected)
    {
        if (!config.automatic_node)
        {
            (void)sendPathRequestForDestination(config.node_hash);
        }
        return nullptr;
    }

    if (selection.changed)
    {
        char node_hash[12] = {};
        formatHashPrefix(selected->propagation_hash,
                         node_hash,
                         sizeof(node_hash));
        Serial.printf("[LXMF][Propagation] active_node dest=%s hops=%u cost=%u mode=%s\n",
                      node_hash,
                      static_cast<unsigned>(selected->hops),
                      static_cast<unsigned>(selected->stamp_cost),
                      config.automatic_node ? "auto" : "manual");
    }
    return selected;
}

bool LxmfAdapter::preparePropagationPeer(const PropagationPeerState& source,
                                         PeerInfo* out_peer) const
{
    if (!out_peer || !source.node_active ||
        isZeroBytes(source.propagation_hash,
                    sizeof(source.propagation_hash)) ||
        isZeroBytes(source.enc_pub, sizeof(source.enc_pub)) ||
        isZeroBytes(source.sig_pub, sizeof(source.sig_pub)))
    {
        return false;
    }

    *out_peer = PeerInfo{};
    copyHash(out_peer->destination_hash,
             source.propagation_hash,
             sizeof(out_peer->destination_hash));
    copyHash(out_peer->identity_hash,
             source.identity_hash,
             sizeof(out_peer->identity_hash));
    memcpy(out_peer->enc_pub, source.enc_pub, sizeof(out_peer->enc_pub));
    memcpy(out_peer->sig_pub, source.sig_pub, sizeof(out_peer->sig_pub));
    out_peer->node_id =
        reticulum::nodeIdFromDestinationHash(out_peer->destination_hash);
    out_peer->last_seen_s = source.last_seen_s;
    std::snprintf(out_peer->display_name,
                  sizeof(out_peer->display_name),
                  "%s",
                  source.display_name);
    return true;
}

bool LxmfAdapter::queuePropagationUpload(
    PeerInfo& recipient,
    const uint8_t* lxmf_message,
    size_t lxmf_message_len,
    MessageId message_id,
    const uint8_t message_hash[reticulum::kFullHashSize],
    bool track_user_message,
    OutboundLxmfDispatch* out_dispatch)
{
    if (!lxmf_message ||
        lxmf_message_len <= reticulum::kTruncatedHashSize ||
        !message_hash || !out_dispatch ||
        !propagation_client_.canQueueUpload(kMaxPendingPropagationUploads))
    {
        return false;
    }

    const size_t encrypted_capacity =
        LxmfIdentity::kEncPubKeySize +
        reticulum::tokenSizeForPlaintext(
            lxmf_message_len - reticulum::kTruncatedHashSize);
    runtime::RuntimeByteBuffer encrypted(encrypted_capacity, 0);
    size_t encrypted_len = encrypted.size();
    if (!encryptForPeer(recipient,
                        lxmf_message + reticulum::kTruncatedHashSize,
                        lxmf_message_len - reticulum::kTruncatedHashSize,
                        encrypted.data(),
                        &encrypted_len))
    {
        return false;
    }

    PendingPropagationUpload upload{};
    copyHash(upload.destination_hash,
             recipient.destination_hash,
             sizeof(upload.destination_hash));
    copyHash(upload.message_hash,
             message_hash,
             sizeof(upload.message_hash));
    upload.transient_data.resize(reticulum::kTruncatedHashSize + encrypted_len);
    memcpy(upload.transient_data.data(),
           recipient.destination_hash,
           reticulum::kTruncatedHashSize);
    memcpy(upload.transient_data.data() + reticulum::kTruncatedHashSize,
           encrypted.data(),
           encrypted_len);
    reticulum::fullHash(upload.transient_data.data(),
                        upload.transient_data.size(),
                        upload.transient_id);
    upload.created_ms = millis();

    if (const PropagationPeerState* node = selectActivePropagationPeer())
    {
        propagation_client_.bindUploadNode(upload, *node);
    }

    PendingPropagationUpload* queued_upload =
        propagation_client_.queueUpload(std::move(upload),
                                        kMaxPendingPropagationUploads);
    if (!queued_upload)
    {
        return false;
    }
    if (track_user_message && message_id != 0)
    {
        delivery_attempt_ledger_.notePropagationReceipt(
            queued_upload->transient_id,
            message_id,
            millis(),
            kMaxPendingDeliveryReceipts);
    }
    out_dispatch->ok = true;
    out_dispatch->result_event_deferred = track_user_message;
    out_dispatch->path = "propagation";
    char transient_hash[12] = {};
    formatHashPrefix(queued_upload->transient_id,
                     transient_hash,
                     sizeof(transient_hash));
    Serial.printf("[LXMF][PropagationTX] queued msg=%lu transient=%s bytes=%u node=%u\n",
                  static_cast<unsigned long>(message_id),
                  transient_hash,
                  static_cast<unsigned>(queued_upload->transient_data.size()),
                  queued_upload->state ==
                          runtime::PropagationUploadState::NeedsStamp
                      ? 1U
                      : 0U);
    return true;
}

MessageId LxmfAdapter::propagationUploadMessageId(
    const PendingPropagationUpload& upload)
{
    runtime::DeliveryAttemptReceipt* receipt =
        delivery_attempt_ledger_.findPropagationReceipt(upload.transient_id);
    return receipt ? receipt->message_id : 0;
}

MessageId LxmfAdapter::takePropagationUploadMessageId(
    const PendingPropagationUpload& upload)
{
    const MessageId message_id = propagationUploadMessageId(upload);
    delivery_attempt_ledger_.removePropagationReceipt(upload.transient_id);
    return message_id;
}

bool LxmfAdapter::queueReadyPropagationUpload(
    PendingPropagationUpload& upload,
    const PropagationPeerState& node)
{
    if (upload.state != runtime::PropagationUploadState::Ready ||
        upload.transient_data.empty() ||
        !preparePropagationPeer(node, &propagation_client_.peerScratch()))
    {
        return false;
    }

    bool started = false;
    LinkSession* session = ensureOutboundLinkSession(propagation_client_.peerScratch(),
                                                     LocalDestinationKind::Propagation,
                                                     &started);
    if (!session)
    {
        return false;
    }

    runtime::RuntimeByteSpanList messages;
    messages.push_back(ByteSpan{upload.transient_data.data(),
                                upload.transient_data.size()});
    runtime::ResourcePayloadBuffer batch(upload.transient_data.size() + 32U, 0);
    size_t batch_len = batch.size();
    if (!encodePropagationBatch(static_cast<double>(currentTimestampSeconds()),
                                runtime::viewRuntimeByteSpans(messages),
                                batch.data(),
                                &batch_len))
    {
        return false;
    }
    batch.resize(batch_len);

    runtime::DeferredLinkPayload deferred{};
    deferred.payload = std::move(batch);
    const MessageId message_id = propagationUploadMessageId(upload);
    deferred.message_id = message_id;
    link_manager_.appendDeferredPayload(*session, std::move(deferred));
    if (message_id != 0)
    {
        delivery_attempt_ledger_.removePropagationReceipt(upload.transient_id);
    }
    propagation_client_.markUploadQueuedToLink(upload);
    if (session->state == LinkState::Active)
    {
        (void)sendLinkIdentify(*session);
        flushDeferredLinkPayloads(*session);
    }

    char node_hash[12] = {};
    char transient_hash[12] = {};
    formatHashPrefix(node.propagation_hash, node_hash, sizeof(node_hash));
    formatHashPrefix(upload.transient_id,
                     transient_hash,
                     sizeof(transient_hash));
    Serial.printf("[LXMF][PropagationTX] link_queue msg=%lu transient=%s node=%s link_state=%u started=%u\n",
                  static_cast<unsigned long>(message_id),
                  transient_hash,
                  node_hash,
                  static_cast<unsigned>(session->state),
                  started ? 1U : 0U);
    return true;
}

void LxmfAdapter::processPropagationClient()
{
    const auto& config = rtnet::active().propagation;
    if (!config.enabled)
    {
        const auto disabled_uploads =
            propagation_client_.takeAllPendingUploads();
        for (const auto& upload : disabled_uploads)
        {
            const MessageId message_id = takePropagationUploadMessageId(upload);
            if (message_id != 0)
            {
                delivery_notifier_.failed(message_id);
            }
            Serial.printf("[LXMF][PropagationTX] disabled msg=%lu\n",
                          static_cast<unsigned long>(message_id));
        }
        propagation_client_.resetForDisabled();
        link_manager_.forEachSession(
            [this](LinkSession& session)
            {
                if (session.destination == LocalDestinationKind::Propagation &&
                    session.state != LinkState::Closed)
                {
                    closeLinkSession(session, LinkCloseReason::LocalClose);
                }
            });
        return;
    }

    const uint32_t now_ms = millis();
    propagation_client_.markExpiredUploads(now_ms, kPropagationUploadTtlMs);
    const auto failed_uploads = propagation_client_.takeFailedUploads();
    for (const auto& upload : failed_uploads)
    {
        const MessageId message_id = takePropagationUploadMessageId(upload);
        if (message_id != 0)
        {
            delivery_notifier_.failed(message_id);
        }
        Serial.printf("[LXMF][PropagationTX] failed msg=%lu\n",
                      static_cast<unsigned long>(message_id));
    }

    const PropagationPeerState* node = selectActivePropagationPeer();
    const PathEntry* node_path =
        node ? path_manager_.findPath(node->propagation_hash, millis(), kPathTtlMs)
             : nullptr;
    if (node && !node_path)
    {
        (void)sendPathRequestForDestination(node->propagation_hash);
        return;
    }
    if (node && isLoRaPath(node_path))
    {
        if (propagation_client_.hasPendingUploads())
        {
            if (PendingPropagationUpload* upload =
                    propagation_client_.firstPendingUpload())
            {
                propagation_client_.markUploadWaitingForNode(*upload);
            }
        }
        return;
    }

    if (propagation_client_.hasPendingUploads())
    {
        PendingPropagationUpload* pending_upload =
            propagation_client_.firstPendingUpload();
        if (!pending_upload)
        {
            return;
        }
        PendingPropagationUpload& upload = *pending_upload;
        const MessageId message_id = propagationUploadMessageId(upload);
        if (!node)
        {
            propagation_client_.markUploadWaitingForNode(upload);
        }
        else
        {
            propagation_client_.bindUploadNode(upload, *node);

            if (upload.state == runtime::PropagationUploadState::NeedsStamp)
            {
                if (propagation_client_.beginUploadStamp(upload))
                {
                    Serial.printf("[LXMF][PropagationTX] stamp_begin msg=%lu cost=%u\n",
                                  static_cast<unsigned long>(message_id),
                                  static_cast<unsigned>(upload.stamp_cost));
                }
            }

            if (upload.state == runtime::PropagationUploadState::Stamping)
            {
                const auto stamp_state = propagation_client_.stamp().poll();
                if (stamp_state ==
                    runtime::PropagationStampRuntime::State::Complete)
                {
                    uint8_t stamp[reticulum::kFullHashSize] = {};
                    const uint32_t rounds =
                        propagation_client_.stamp().searchRounds();
                    if (!propagation_client_.stamp().takeStamp(stamp))
                    {
                        propagation_client_.markUploadFailed(upload);
                    }
                    else if (propagation_client_.completeUploadStamp(upload,
                                                                     stamp))
                    {
                        Serial.printf("[LXMF][PropagationTX] stamp_ready msg=%lu rounds=%lu\n",
                                      static_cast<unsigned long>(message_id),
                                      static_cast<unsigned long>(rounds));
                    }
                }
                else if (stamp_state ==
                         runtime::PropagationStampRuntime::State::Failed)
                {
                    propagation_client_.markUploadFailed(upload);
                }
#if defined(TRAIL_MATE_VERBOSE_RUNTIME_LOGS) && TRAIL_MATE_VERBOSE_RUNTIME_LOGS
                else if (stamp_state ==
                             runtime::PropagationStampRuntime::State::Expanding &&
                         propagation_client_.stamp().expandedRounds() != 0U &&
                         (propagation_client_.stamp().expandedRounds() % 100U) == 0U)
                {
                    Serial.printf("[LXMF][PropagationTX] stamp_progress msg=%lu expand=%u/1000 search=%lu\n",
                                  static_cast<unsigned long>(message_id),
                                  static_cast<unsigned>(
                                      propagation_client_.stamp().expandedRounds()),
                                  static_cast<unsigned long>(
                                      propagation_client_.stamp().searchRounds()));
                }
                else if (stamp_state ==
                             runtime::PropagationStampRuntime::State::Searching &&
                         propagation_client_.stamp().searchRounds() != 0U &&
                         (propagation_client_.stamp().searchRounds() % 4096U) == 0U)
                {
                    Serial.printf("[LXMF][PropagationTX] stamp_progress msg=%lu expand=%u/1000 search=%lu\n",
                                  static_cast<unsigned long>(message_id),
                                  static_cast<unsigned>(
                                      propagation_client_.stamp().expandedRounds()),
                                  static_cast<unsigned long>(
                                      propagation_client_.stamp().searchRounds()));
                }
#endif
            }

            if (upload.state == runtime::PropagationUploadState::Ready &&
                queueReadyPropagationUpload(upload, *node))
            {
                propagation_client_.removeFirstPendingUpload();
            }
        }
    }

    if (node)
    {
        LinkSession* session =
            link_manager_.findOpenSessionByDestination(
                node->propagation_hash,
                LocalDestinationKind::Propagation);
        const auto& sync_config = rtnet::active().propagation;
        const uint32_t now_s = currentTimestampSeconds();
        const bool sync_due =
            propagation_client_.syncDue(now_s, sync_config.sync_interval_s);
        if (!session && sync_due &&
            preparePropagationPeer(*node, &propagation_client_.peerScratch()))
        {
            session = ensureOutboundLinkSession(propagation_client_.peerScratch(),
                                                LocalDestinationKind::Propagation,
                                                nullptr);
        }
        if (session)
        {
            processPropagationSyncResponse(*session);
        }
    }
}

bool LxmfAdapter::sendPropagationSyncRequest(
    LinkSession& session,
    PropagationSyncStage next_stage,
    const runtime::PropagationIdList* wants,
    const runtime::PropagationIdList* haves,
    bool include_transfer_limit)
{
    if (session.state != LinkState::Active ||
        session.destination != LocalDestinationKind::Propagation)
    {
        return false;
    }

    const size_t item_count = (wants ? wants->size() : 0U) +
                              (haves ? haves->size() : 0U);
    runtime::ResourcePayloadBuffer packed_data(32U + item_count * 35U, 0);
    size_t packed_data_len = packed_data.size();
    const runtime::RuntimeByteSpanList want_spans =
        wants ? runtime::makeRuntimeByteSpans(*wants)
              : runtime::RuntimeByteSpanList{};
    const runtime::RuntimeByteSpanList have_spans =
        haves ? runtime::makeRuntimeByteSpans(*haves)
              : runtime::RuntimeByteSpanList{};
    const ByteSpanList want_view = runtime::viewRuntimeByteSpans(want_spans);
    const ByteSpanList have_view = runtime::viewRuntimeByteSpans(have_spans);
    if (!encodePropagationGetRequestPayloadSpans(wants ? &want_view : nullptr,
                                                 haves ? &have_view : nullptr,
                                                 include_transfer_limit,
                                                 kPropagationTransferLimitKb,
                                                 packed_data.data(),
                                                 &packed_data_len))
    {
        return false;
    }
    packed_data.resize(packed_data_len);

    uint8_t path_hash[reticulum::kTruncatedHashSize] = {};
    runtime::propagationServicePathHash(
        runtime::PropagationServicePath::Get,
        path_hash);
    runtime::ResourcePayloadBuffer request_payload(packed_data.size() + 64U, 0);
    size_t request_payload_len = request_payload.size();
    if (!encodeLinkRequestPayload(
            static_cast<double>(currentTimestampSeconds()),
            path_hash,
            packed_data.data(),
            packed_data.size(),
            false,
            request_payload.data(),
            &request_payload_len))
    {
        return false;
    }
    request_payload.resize(request_payload_len);

    uint8_t request_id[reticulum::kTruncatedHashSize] = {};
    bool sent = false;
    if (request_payload.size() <= session.mdu)
    {
        runtime::ResourcePayloadBuffer wire_payload(
            reticulum::tokenSizeForPlaintext(request_payload.size()),
            0);
        size_t wire_payload_len = wire_payload.size();
        if (!encryptLinkPayload(session,
                                request_payload.data(),
                                request_payload.size(),
                                wire_payload.data(),
                                &wire_payload_len))
        {
            return false;
        }
        wire_payload.resize(wire_payload_len);

        std::memset(scratch_.lxmf_tx_packet, 0, sizeof(scratch_.lxmf_tx_packet));
        size_t packet_len = sizeof(scratch_.lxmf_tx_packet);
        if (!reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                           reticulum::DestinationType::Link,
                                           reticulum::PacketContext::Request,
                                           false,
                                           session.link_id,
                                           wire_payload.data(),
                                           wire_payload.size(),
                                           scratch_.lxmf_tx_packet,
                                           &packet_len))
        {
            return false;
        }
        reticulum::computeTruncatedPacketHash(scratch_.lxmf_tx_packet,
                                              packet_len,
                                              request_id);
        sent = session.interface_id !=
                       reticulum::interfaces::kInvalidInterfaceId
                   ? interfaces_.sendPacketOn(session.interface_id,
                                              scratch_.lxmf_tx_packet,
                                              packet_len)
                   : interfaces_.sendPacket(scratch_.lxmf_tx_packet, packet_len);
    }
    else
    {
        reticulum::truncatedHash(request_payload.data(),
                                 request_payload.size(),
                                 request_id);
        sent = queueOutgoingResource(session,
                                     request_payload.data(),
                                     request_payload.size(),
                                     kResourceFlagRequest,
                                     request_id,
                                     sizeof(request_id));
    }
    if (!sent)
    {
        return false;
    }

    link_manager_.queuePendingRequest(session,
                                      request_id,
                                      sizeof(request_id),
                                      millis(),
                                      request_payload.size() > session.mdu);
    propagation_client_.markSyncRequestSent(request_id, next_stage);
    link_manager_.touchOutbound(session, millis());
    Serial.printf("[LXMF][PropagationSync] request stage=%u wants=%u haves=%u resource=%u\n",
                  static_cast<unsigned>(next_stage),
                  static_cast<unsigned>(wants ? wants->size() : 0U),
                  static_cast<unsigned>(haves ? haves->size() : 0U),
                  request_payload.size() > session.mdu ? 1U : 0U);
    return true;
}

void LxmfAdapter::processPropagationSyncResponse(LinkSession& session)
{
    if (session.state != LinkState::Active ||
        session.destination != LocalDestinationKind::Propagation)
    {
        return;
    }

    const auto& config = rtnet::active().propagation;
    const uint32_t now_s = currentTimestampSeconds();
    propagation_client_.startSyncIfDue(now_s, millis(), config.sync_interval_s);

    if (propagation_client_.syncStage() == PropagationSyncStage::NeedList)
    {
        (void)sendLinkIdentify(session);
        if (!sendPropagationSyncRequest(session,
                                        PropagationSyncStage::Listing,
                                        nullptr,
                                        nullptr,
                                        false))
        {
            propagation_client_.markSyncFailed();
        }
        return;
    }

    LinkPendingRequest* pending = link_manager_.findPendingRequestIf(
        session,
        [this](const LinkPendingRequest& request)
        { return propagation_client_.syncRequestMatches(request); });
    if ((propagation_client_.syncStage() == PropagationSyncStage::Listing ||
         propagation_client_.syncStage() == PropagationSyncStage::Downloading ||
         propagation_client_.syncStage() == PropagationSyncStage::Acknowledging) &&
        pending && pending->created_ms != 0 &&
        (millis() - pending->created_ms) > kLinkRequestTtlMs)
    {
        link_manager_.erasePendingRequest(session, *pending);
        propagation_client_.markSyncFailed();
        pending = nullptr;
    }

    if (propagation_client_.syncStage() == PropagationSyncStage::Listing &&
        pending && pending->response_ready)
    {
        runtime::PropagationIdList remote_ids;
        const bool decoded =
            decodePropagationIdListPayload(pending->response.data(),
                                           pending->response.size(),
                                           runtime::appendRuntimeByteBufferCallback,
                                           &remote_ids);
        link_manager_.erasePendingRequest(session, *pending);
        pending = nullptr;
        if (!decoded)
        {
            propagation_client_.markSyncFailed();
        }
        else
        {
            propagation_client_.noteListingResult(remote_ids,
                                                  config.max_messages_per_sync);
        }
    }

    if (propagation_client_.syncStage() == PropagationSyncStage::NeedMessages)
    {
        if (!sendPropagationSyncRequest(session,
                                        PropagationSyncStage::Downloading,
                                        &propagation_client_.syncWants(),
                                        &propagation_client_.syncHaves(),
                                        true))
        {
            propagation_client_.markSyncFailed();
        }
        return;
    }

    if (propagation_client_.syncStage() == PropagationSyncStage::Downloading &&
        pending && pending->response_ready)
    {
        runtime::PropagationMessageList messages;
        const bool decoded =
            decodePropagationMessageListPayload(pending->response.data(),
                                                pending->response.size(),
                                                runtime::appendRuntimeByteBufferCallback,
                                                &messages);
        link_manager_.erasePendingRequest(session, *pending);
        pending = nullptr;
        if (!decoded)
        {
            propagation_client_.markSyncFailed();
        }
        else
        {
            bool delivery_commit_registration_failed = false;
            uint8_t local_delivery[reticulum::kTruncatedHashSize] = {};
            localDestinationHash(LocalDestinationKind::Delivery,
                                 local_delivery);
            for (const auto& message : messages)
            {
                if (message.size() <= reticulum::kTruncatedHashSize ||
                    !hashesEqual(message.data(),
                                 local_delivery,
                                 sizeof(local_delivery)))
                {
                    continue;
                }
                uint8_t transient_id[reticulum::kFullHashSize] = {};
                reticulum::fullHash(message.data(),
                                    message.size(),
                                    transient_id);
                uint8_t message_hash[reticulum::kFullHashSize] = {};
                bool awaiting_commit = false;
                if (acceptPropagatedDelivery(
                        message.data() + reticulum::kTruncatedHashSize,
                        message.size() - reticulum::kTruncatedHashSize,
                        message_hash,
                        &awaiting_commit))
                {
                    if (awaiting_commit)
                    {
                        if (!propagation_client_.registerDeliveryCommit(
                                transient_id,
                                message_hash,
                                config.max_messages_per_sync))
                        {
                            delivery_commit_registration_failed = true;
                            break;
                        }
                    }
                    else
                    {
                        propagation_client_.rememberDeliveredTransient(
                            transient_id,
                            currentTimestampSeconds(),
                            kMaxPropagationTransients);
                    }
                }
            }
            propagation_client_.noteDownloadResult(
                delivery_commit_registration_failed,
                millis());
        }
    }

    if (propagation_client_.syncStage() == PropagationSyncStage::AwaitingPersistence)
    {
        if (!propagation_client_.pollPersistence(millis(), kLinkRequestTtlMs))
        {
            return;
        }
    }

    if (propagation_client_.syncStage() == PropagationSyncStage::NeedAcknowledge)
    {
        const bool acknowledged =
            propagation_client_.syncHavesEmpty() ||
            sendPropagationSyncRequest(session,
                                       PropagationSyncStage::Acknowledging,
                                       nullptr,
                                       &propagation_client_.syncHaves(),
                                       false);
        if (acknowledged)
        {
            if (propagation_client_.syncHavesEmpty())
            {
                propagation_client_.markAcknowledged();
            }
        }
        else
        {
            propagation_client_.markSyncFailed();
        }
        if (propagation_client_.syncStage() == PropagationSyncStage::Acknowledging)
        {
            return;
        }
    }

    if (propagation_client_.syncStage() == PropagationSyncStage::Acknowledging &&
        pending && pending->response_ready)
    {
        link_manager_.erasePendingRequest(session, *pending);
        pending = nullptr;
        propagation_client_.markAcknowledged();
    }

    if (propagation_client_.syncStage() == PropagationSyncStage::Complete)
    {
        const std::size_t acknowledged_count = propagation_client_.syncHaveCount();
        Serial.printf("[LXMF][PropagationSync] complete received=%u acknowledged=%u\n",
                      static_cast<unsigned>(acknowledged_count),
                      static_cast<unsigned>(acknowledged_count));
        propagation_client_.finishSyncComplete(now_s);
    }
    else if (propagation_client_.syncStage() == PropagationSyncStage::Failed)
    {
        Serial.println("[LXMF][PropagationSync] failed");
        propagation_client_.finishSyncFailed(now_s);
    }
}

bool LxmfAdapter::respondToSidebandTelemetryRequest(
    PeerInfo& peer,
    const SidebandTelemetryRequest& request)
{
    if (!request.valid || !config_.reticulum_allow_location_requests)
    {
        return false;
    }

    const auto fix = ::platform::ui::gps::get_data();
    if (!fix.valid)
    {
        Serial.printf("[LXMF][Sideband] telemetry response skipped peer=%08lX reason=gps_no_fix\n",
                      static_cast<unsigned long>(peer.node_id));
        return false;
    }

    SidebandTelemetryLocation location{};
    location.valid = true;
    location.latitude_e6 = static_cast<int32_t>(std::llround(fix.lat * 1000000.0));
    location.longitude_e6 = static_cast<int32_t>(std::llround(fix.lng * 1000000.0));
    location.altitude_cm = fix.has_alt
                               ? static_cast<int32_t>(std::llround(fix.alt_m * 100.0))
                               : 0;
    location.accuracy_cm = 0;
    location.timestamp = currentTimestampSeconds();

    runtime::RuntimeByteBuffer packed_payload(kMaxLxmfMessageLen, 0);
    size_t packed_payload_len = packed_payload.size();
    if (!encodeSidebandTelemetryLocationPayload(
            static_cast<double>(location.timestamp),
            location,
            packed_payload.data(),
            &packed_payload_len))
    {
        return false;
    }

    OutboundLxmfDispatch dispatch{};
    const bool sent = dispatchLxmfPayload(peer,
                                          packed_payload.data(),
                                          packed_payload_len,
                                          false,
                                          &dispatch);
    Serial.printf("[LXMF][Sideband] telemetry response peer=%08lX sent=%u path=%s timebase=%lu collector=%u\n",
                  static_cast<unsigned long>(peer.node_id),
                  sent ? 1U : 0U,
                  dispatch.path,
                  static_cast<unsigned long>(request.timebase),
                  request.collector_request ? 1U : 0U);
    return sent;
}

bool LxmfAdapter::getGeocachingAuthorKey(uint8_t out[64]) const
{
    if (!out) return false;
    std::memset(out, 0, 64);
    if (!identity_.isReady()) return false;
    identity_.combinedPublicKey(out);
    return true;
}

bool LxmfAdapter::signGeocachingRecord(ByteSpan record, uint8_t* workspace, size_t workspace_capacity,
                                       uint8_t* output, size_t output_capacity, size_t& written)
{
    return ::geocaching::protocol::signGeocacheRecord({record.data, record.size}, identity_,
                                                      workspace, workspace_capacity, output, output_capacity, written);
}

MeshSendResult LxmfAdapter::sendCustomDataToDestination(const uint8_t destination_hash[16],
                                                        const char* custom_type, ByteSpan data,
                                                        bool response, std::array<uint8_t, 32>* accepted_lxmf_hash)
{
    if (accepted_lxmf_hash) accepted_lxmf_hash->fill(0);
    if (!destination_hash || !custom_type || !custom_type[0] || strlen(custom_type) > 96 ||
        !data.data || data.size == 0 || data.size > 8192)
        return MeshSendResult::fail(MeshOperationFailure::InvalidInput);
    if (!isReady()) return MeshSendResult::fail(MeshOperationFailure::NotReady);
    PeerInfo* peer = findOrLoadPeerByDestinationHash(destination_hash);
    if (!peer) return MeshSendResult::fail(MeshOperationFailure::PeerKeyMissing);
    runtime::RuntimeByteBuffer payload(data.size + 256, 0);
    size_t size = payload.size();
    if (!encodeCustomDataPayload(static_cast<double>(currentTimestampSeconds()),
                                 "Trail Mate Geocache v1",
                                 response ? "Geocache response" : "Geocache request",
                                 custom_type, data, payload.data(), &size))
        return MeshSendResult::fail(MeshOperationFailure::EncodeFailed);
    OutboundLxmfDispatch dispatch{};
    const bool ok = dispatchLxmfPayload(*peer, payload.data(), size, false, &dispatch);
    if (ok && accepted_lxmf_hash) std::memcpy(accepted_lxmf_hash->data(), dispatch.message_hash, accepted_lxmf_hash->size());
    MeshSendResult result = ok ? MeshSendResult::success(dispatch.message_id)
                               : MeshSendResult::fail(dispatch.failure, dispatch.message_id);
    result.reticulum_identity = runtime::reticulumIdentityForPeer(*peer);
    return result;
}

MeshSendResult LxmfAdapter::sendTextDetailed(ChannelId channel,
                                             const std::string& text,
                                             MessageId forced_msg_id,
                                             NodeId peer)
{
    (void)forced_msg_id;

    char text_preview[64] = {};
    formatLogTextPreview(text, text_preview, sizeof(text_preview));
    Serial.printf("[LXMF][DirectTX] begin ch=%u peer=%08lX len=%u ready=%u text=\"%s\"\n",
                  static_cast<unsigned>(channel),
                  static_cast<unsigned long>(peer),
                  static_cast<unsigned>(text.size()),
                  isReady() ? 1U : 0U,
                  text_preview);

    if (channel != ChannelId::PRIMARY || text.empty() || peer == 0)
    {
        Serial.printf("[LXMF][DirectTX] reject reason=invalid_input ch=%u peer=%08lX len=%u\n",
                      static_cast<unsigned>(channel),
                      static_cast<unsigned long>(peer),
                      static_cast<unsigned>(text.size()));
        return MeshSendResult::fail(MeshOperationFailure::InvalidInput);
    }

    if (!isReady())
    {
        Serial.printf("[LXMF][DirectTX] reject reason=not_ready peer=%08lX\n",
                      static_cast<unsigned long>(peer));
        return MeshSendResult::fail(MeshOperationFailure::NotReady);
    }

    PeerInfo* peer_info = findOrLoadPeerByNodeId(peer);
    if (!peer_info)
    {
        Serial.printf("[LXMF][DirectTX] reject reason=peer_key_missing peer=%08lX\n",
                      static_cast<unsigned long>(peer));
        return MeshSendResult::fail(MeshOperationFailure::PeerKeyMissing);
    }
    char peer_hash[12] = {};
    char peer_dest_full[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    formatHashPrefix(peer_info->destination_hash, peer_hash, sizeof(peer_hash));
    formatHashHex(peer_info->destination_hash,
                  sizeof(peer_info->destination_hash),
                  peer_dest_full,
                  sizeof(peer_dest_full));

    runtime::RuntimeByteBuffer packed_payload(kMaxLxmfMessageLen, 0);
    size_t packed_payload_len = packed_payload.size();
    if (!encodeTextPayload(static_cast<double>(currentTimestampSeconds()),
                           "",
                           text.c_str(),
                           packed_payload.data(),
                           &packed_payload_len))
    {
        return MeshSendResult::fail(MeshOperationFailure::EncodeFailed);
    }

    OutboundLxmfDispatch dispatch{};
    const bool ok = dispatchLxmfPayload(*peer_info,
                                        packed_payload.data(),
                                        packed_payload_len,
                                        true,
                                        &dispatch);
    const MessageId message_id = dispatch.message_id;
    const bool send_result_event_deferred = dispatch.result_event_deferred;

    char message_hash_prefix[12] = {};
    formatHashPrefix(dispatch.message_hash,
                     message_hash_prefix,
                     sizeof(message_hash_prefix));
    const auto& tx_result = interfaces_.lastTxResult();
    Serial.printf("[LXMF][DirectTX] result ok=%u msg=%lu hash=%s peer=%08lX name=\"%s\" dest=%s dest_full=%s path=%s event_deferred=%u bearer=%s complete=%u payload_len=%u text=\"%s\"\n",
                  ok ? 1U : 0U,
                  static_cast<unsigned long>(message_id),
                  message_hash_prefix,
                  static_cast<unsigned long>(peer_info->node_id),
                  peer_info->display_name[0] != '\0' ? peer_info->display_name : "<unnamed>",
                  peer_hash,
                  peer_dest_full,
                  dispatch.path,
                  send_result_event_deferred ? 1U : 0U,
                  txBearerName(tx_result),
                  tx_result.reachedRequiredInterfaces() ? 1U : 0U,
                  static_cast<unsigned>(packed_payload_len),
                  text_preview);
    MeshSendResult result =
        ok ? MeshSendResult::success(message_id)
           : MeshSendResult::fail(dispatch.failure, message_id);
    result.reticulum_identity = runtime::reticulumIdentityForPeer(*peer_info);
    return result;
}

MeshSendResult LxmfAdapter::sendTextToReticulumDestination(
    ChannelId channel,
    const std::string& text,
    MessageId forced_msg_id,
    const ReticulumPeerIdentity& destination)
{
    char dest_hash[12] = {};
    char dest_full[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    char text_preview[64] = {};
    formatHashPrefix(destination.destination_hash, dest_hash, sizeof(dest_hash));
    formatHashHex(destination.destination_hash,
                  sizeof(destination.destination_hash),
                  dest_full,
                  sizeof(dest_full));
    formatLogTextPreview(text, text_preview, sizeof(text_preview));
    Serial.printf("[LXMF][DestinationTX] begin ch=%u forced=%lu dest=%s dest_full=%s len=%u ready=%u text=\"%s\"\n",
                  static_cast<unsigned>(channel),
                  static_cast<unsigned long>(forced_msg_id),
                  dest_hash,
                  dest_full,
                  static_cast<unsigned>(text.size()),
                  isReady() ? 1U : 0U,
                  text_preview);

    if (channel != ChannelId::PRIMARY || text.empty() ||
        !hasReticulumDestinationIdentity(destination))
    {
        Serial.printf("[LXMF][DestinationTX] reject reason=invalid_input ch=%u dest=%s len=%u\n",
                      static_cast<unsigned>(channel),
                      dest_hash,
                      static_cast<unsigned>(text.size()));
        return MeshSendResult::fail(MeshOperationFailure::InvalidInput);
    }

    if (!isReady())
    {
        Serial.printf("[LXMF][DestinationTX] reject reason=not_ready dest=%s\n", dest_hash);
        return MeshSendResult::fail(MeshOperationFailure::NotReady);
    }

    const bool configured_group = isConfiguredGroupDestination(destination);
    if (!configured_group)
    {
        PeerInfo* peer_info =
            findOrLoadPeerByDestinationHash(destination.destination_hash);
        if (!peer_info)
        {
            const bool path_requested =
                sendPathRequestForDestination(destination.destination_hash);
            Serial.printf("[LXMF][DestinationTX] path_pending dest=%s requested=%u\n",
                          dest_hash,
                          path_requested ? 1U : 0U);
            MeshSendResult result = MeshSendResult::fail(
                MeshOperationFailure::NotReady,
                0,
                path_requested ? 1 : 2);
            result.reticulum_identity =
                makeReticulumDestinationIdentity(destination.destination_hash);
            return result;
        }

        Serial.printf("[LXMF][DestinationTX] resolved dest=%s peer=%08lX name=\"%s\"\n",
                      dest_hash,
                      static_cast<unsigned long>(peer_info->node_id),
                      peer_info->display_name[0] != '\0' ? peer_info->display_name : "<unnamed>");
        MeshSendResult result =
            sendTextDetailed(channel, text, forced_msg_id, peer_info->node_id);
        if (!hasReticulumDestinationIdentity(result.reticulum_identity))
        {
            result.reticulum_identity = runtime::reticulumIdentityForPeer(*peer_info);
        }
        return result;
    }

    uint8_t packed_payload[kMaxLxmfMessageLen] = {};
    size_t packed_payload_len = sizeof(packed_payload);
    if (!encodeTextPayload(static_cast<double>(currentTimestampSeconds()),
                           "",
                           text.c_str(),
                           packed_payload,
                           &packed_payload_len))
    {
        Serial.printf("[LXMF][GroupTX] reject reason=encode_failed dest=%s text_len=%u\n",
                      dest_hash,
                      static_cast<unsigned>(text.size()));
        return MeshSendResult::fail(MeshOperationFailure::EncodeFailed);
    }

    std::memset(scratch_.lxmf_tx_packet, 0, sizeof(scratch_.lxmf_tx_packet));
    size_t packet_len = sizeof(scratch_.lxmf_tx_packet);
    uint8_t message_hash[reticulum::kFullHashSize] = {};
    if (!buildGroupMessagePacket(destination,
                                 packed_payload,
                                 packed_payload_len,
                                 scratch_.lxmf_tx_packet,
                                 &packet_len,
                                 message_hash))
    {
        Serial.printf("[LXMF][GroupTX] reject reason=build_packet_failed dest=%s payload_len=%u\n",
                      dest_hash,
                      static_cast<unsigned>(packed_payload_len));
        return MeshSendResult::fail(MeshOperationFailure::CryptoFailed);
    }

    const MessageId message_id =
        forced_msg_id != 0 ? forced_msg_id : messageIdFromHash(message_hash);
    char message_hash_prefix[12] = {};
    formatHashPrefix(message_hash, message_hash_prefix, sizeof(message_hash_prefix));
    Serial.printf("[LXMF][GroupTX] packet_ready msg=%lu hash=%s dest=%s payload_len=%u packet_len=%u\n",
                  static_cast<unsigned long>(message_id),
                  message_hash_prefix,
                  dest_hash,
                  static_cast<unsigned>(packed_payload_len),
                  static_cast<unsigned>(packet_len));
    const bool ok = routeAndSendPacket(scratch_.lxmf_tx_packet, packet_len, true);
    const auto& tx_result = interfaces_.lastTxResult();
    Serial.printf("[LXMF][GroupTX] raw_send ok=%u msg=%lu dest=%s dest_full=%s bearer=%s complete=%u packet_len=%u text=\"%s\"\n",
                  ok ? 1U : 0U,
                  static_cast<unsigned long>(message_id),
                  dest_hash,
                  dest_full,
                  txBearerName(tx_result),
                  tx_result.reachedRequiredInterfaces() ? 1U : 0U,
                  static_cast<unsigned>(packet_len),
                  text_preview);
    MeshSendResult result =
        ok ? MeshSendResult::success(message_id)
           : MeshSendResult::fail(MeshOperationFailure::RadioTxFailed, message_id);
    result.reticulum_identity =
        makeReticulumDestinationIdentity(destination.destination_hash);
    return result;
}

bool LxmfAdapter::pollIncomingText(MeshIncomingText* out)
{
    return text_receive_queue_.pop(out);
}

void LxmfAdapter::commitIncomingText(const MeshIncomingText& message,
                                     bool durably_accepted)
{
    if (!message.has_reticulum_lxmf_hash)
    {
        return;
    }

    if (!propagation_client_.noteDeliveryCommit(message.reticulum_lxmf_hash,
                                                durably_accepted,
                                                currentTimestampSeconds(),
                                                kMaxPropagationTransients))
    {
        return;
    }

    Serial.printf("[LXMF][PropagationSync] durable_commit msg=%08lX accepted=%u pending=%u\n",
                  static_cast<unsigned long>(message.msg_id),
                  durably_accepted ? 1U : 0U,
                  static_cast<unsigned>(
                      propagation_client_.pendingDeliveryCount()));
}

bool LxmfAdapter::sendAppData(ChannelId channel, uint32_t portnum,
                              const uint8_t* payload, size_t len,
                              NodeId dest, bool want_ack,
                              MessageId packet_id,
                              bool want_response)
{
    (void)want_ack;

    Serial.printf("[LXMF][AppDataTX] begin ch=%u port=%lu dest=%08lX len=%u ready=%u want_ack=%u want_response=%u\n",
                  static_cast<unsigned>(channel),
                  static_cast<unsigned long>(portnum),
                  static_cast<unsigned long>(dest),
                  static_cast<unsigned>(len),
                  isReady() ? 1U : 0U,
                  want_ack ? 1U : 0U,
                  want_response ? 1U : 0U);

    if (channel != ChannelId::PRIMARY || portnum == 0 || (!payload && len != 0) || !isReady())
    {
        Serial.printf("[LXMF][AppDataTX] reject reason=invalid_input ch=%u port=%lu dest=%08lX len=%u ready=%u\n",
                      static_cast<unsigned>(channel),
                      static_cast<unsigned long>(portnum),
                      static_cast<unsigned long>(dest),
                      static_cast<unsigned>(len),
                      isReady() ? 1U : 0U);
        if (packet_id != 0)
        {
            delivery_notifier_.failed(packet_id);
        }
        return false;
    }

    MessageId effective_packet_id = packet_id;
    if (effective_packet_id == 0)
    {
        effective_packet_id = next_app_packet_id_++;
        if (next_app_packet_id_ == 0)
        {
            next_app_packet_id_ = 1;
        }
    }
    else if (effective_packet_id >= next_app_packet_id_)
    {
        next_app_packet_id_ = effective_packet_id + 1;
        if (next_app_packet_id_ == 0)
        {
            next_app_packet_id_ = 1;
        }
    }

    uint8_t packed_payload[kMaxLxmfMessageLen] = {};
    size_t packed_payload_len = sizeof(packed_payload);
    if (!encodeAppDataPayload(portnum,
                              effective_packet_id,
                              0,
                              want_response,
                              payload,
                              len,
                              packed_payload,
                              &packed_payload_len))
    {
        delivery_notifier_.failed(effective_packet_id);
        return false;
    }

    bool ok = false;
    if (dest != 0)
    {
        PeerInfo* peer_info = findOrLoadPeerByNodeId(dest);
        if (!peer_info)
        {
            Serial.printf("[LXMF][AppDataTX] reject reason=peer_key_missing port=%lu dest=%08lX msg=%lu\n",
                          static_cast<unsigned long>(portnum),
                          static_cast<unsigned long>(dest),
                          static_cast<unsigned long>(effective_packet_id));
            delivery_notifier_.failed(effective_packet_id);
            return false;
        }

        if (shouldRequestPath(*peer_info))
        {
            (void)sendPathRequest(*peer_info);
        }

        uint8_t message_hash[reticulum::kFullHashSize] = {};
        uint8_t signed_part[kSignedPartMaxLen] = {};
        size_t signed_part_len = sizeof(signed_part);
        uint8_t signature[reticulum::kSignatureSize] = {};
        uint8_t lxmf_message[kMaxLxmfMessageLen] = {};
        size_t lxmf_message_len = sizeof(lxmf_message);
        const bool have_link_payload =
            buildSignedPart(peer_info->destination_hash,
                            identity_.destinationHash(),
                            packed_payload,
                            packed_payload_len,
                            signed_part,
                            &signed_part_len,
                            message_hash) &&
            identity_.sign(signed_part, signed_part_len, signature) &&
            packMessage(peer_info->destination_hash,
                        identity_.destinationHash(),
                        signature,
                        packed_payload,
                        packed_payload_len,
                        lxmf_message,
                        &lxmf_message_len);

        LinkSession* active_link =
            findActiveLinkSessionByDestination(peer_info->destination_hash, LocalDestinationKind::Delivery);
        if (active_link && have_link_payload)
        {
            if (lxmf_message_len <= active_link->mdu)
            {
                ok = sendLinkPacket(*active_link,
                                    reticulum::PacketType::Data,
                                    reticulum::PacketContext::None,
                                    lxmf_message,
                                    lxmf_message_len,
                                    true);
            }
            else
            {
                ok = queueOutgoingResource(*active_link,
                                           lxmf_message,
                                           lxmf_message_len,
                                           0,
                                           nullptr,
                                           0);
            }
            if (ok)
            {
                (void)sendLinkIdentify(*active_link);
            }
        }
        else
        {
            std::memset(scratch_.lxmf_tx_packet, 0, sizeof(scratch_.lxmf_tx_packet));
            size_t packet_len = sizeof(scratch_.lxmf_tx_packet);
            if (buildSignedMessagePacket(*peer_info,
                                         packed_payload,
                                         packed_payload_len,
                                         scratch_.lxmf_tx_packet,
                                         &packet_len,
                                         message_hash))
            {
                ok = routeAndSendPacket(scratch_.lxmf_tx_packet, packet_len, true);
            }

            if (!ok && have_link_payload)
            {
                bool started = false;
                LinkSession* session =
                    ensureOutboundLinkSession(*peer_info, LocalDestinationKind::Delivery, &started);
                if (session)
                {
                    runtime::DeferredLinkPayload deferred{};
                    deferred.payload.assign(lxmf_message, lxmf_message + lxmf_message_len);
                    link_manager_.appendDeferredPayload(*session, std::move(deferred));
                    if (session->state == LinkState::Active)
                    {
                        flushDeferredLinkPayloads(*session);
                    }
                    ok = true;
                }
            }
        }
    }
    else
    {
        bool have_peer = false;
        unsigned fanout_count = 0;
        ok = true;
        // LXMF delivery is single-destination. For device-side team/business
        // traffic we currently treat dest==0 as fan-out to every known peer.
        destination_registry_.forEach(
            [&](PeerInfo& peer_info)
            {
                if (isZeroBytes(peer_info.destination_hash,
                                sizeof(peer_info.destination_hash)))
                {
                    return;
                }

                have_peer = true;
                ++fanout_count;
                if (shouldRequestPath(peer_info))
                {
                    (void)sendPathRequest(peer_info);
                }

                std::memset(scratch_.lxmf_tx_packet, 0, sizeof(scratch_.lxmf_tx_packet));
                size_t packet_len = sizeof(scratch_.lxmf_tx_packet);
                uint8_t message_hash[reticulum::kFullHashSize] = {};
                if (!buildSignedMessagePacket(peer_info,
                                              packed_payload,
                                              packed_payload_len,
                                              scratch_.lxmf_tx_packet,
                                              &packet_len,
                                              message_hash) ||
                    !routeAndSendPacket(scratch_.lxmf_tx_packet,
                                        packet_len,
                                        true))
                {
                    ok = false;
                }
            });
        ok = have_peer && ok;
        Serial.printf("[LXMF][AppDataTX] fanout port=%lu msg=%lu peers=%u ok=%u\n",
                      static_cast<unsigned long>(portnum),
                      static_cast<unsigned long>(effective_packet_id),
                      fanout_count,
                      ok ? 1U : 0U);
    }

    Serial.printf("[LXMF][AppDataTX] end port=%lu msg=%lu dest=%08lX ok=%u\n",
                  static_cast<unsigned long>(portnum),
                  static_cast<unsigned long>(effective_packet_id),
                  static_cast<unsigned long>(dest),
                  ok ? 1U : 0U);
    delivery_notifier_.publish(
        effective_packet_id,
        ok ? MessageStatus::Queued : MessageStatus::Failed);
    return ok;
}

bool LxmfAdapter::pollIncomingData(MeshIncomingData* out)
{
    return data_receive_queue_.pop(out);
}

bool LxmfAdapter::requestNodeInfo(NodeId dest, bool want_response)
{
    (void)want_response;

    if (dest != 0 && dest != 0xFFFFFFFFUL)
    {
        PeerInfo* peer = findOrLoadPeerByNodeId(dest);
        if (!peer)
        {
            return false;
        }

        if (!shouldRequestPath(*peer))
        {
            return true;
        }
        return sendPathRequest(*peer);
    }

    return broadcastSelfIdentity();
}

bool LxmfAdapter::broadcastSelfIdentity()
{
    if (!announce_scheduler_.beginManualBroadcast(config_.reticulum_anonymous_peer))
    {
        Serial.println("[LXMF][AnnounceTX] skip reason=anonymous_peer trigger=broadcast_self");
        return false;
    }

    const bool delivery_ok = sendAnnounce(LocalDestinationKind::Delivery);
    const bool delivery_complete = lastAnnounceTxReachedRequiredInterfaces(delivery_ok);
    const bool propagation_service =
        rtnet::active().propagation.service_enabled;
    const bool propagation_ok =
        propagation_service && sendAnnounce(LocalDestinationKind::Propagation);
    const bool propagation_complete =
        !propagation_service || lastAnnounceTxReachedRequiredInterfaces(propagation_ok);
    const bool call_audio_ok = sendAnnounce(LocalDestinationKind::CallAudio);
    const bool call_audio_complete =
        !interfaces_.wifiGatewayConfigured() ||
        !interfaces_.hasReadyWifiGateway() ||
        lastAnnounceTxReachedRequiredInterfaces(call_audio_ok);
    announce_scheduler_.completeAttempt(
        millis(),
        delivery_ok || propagation_ok || call_audio_ok,
        delivery_complete && propagation_complete && call_audio_complete);
    return delivery_ok || propagation_ok || call_audio_ok;
}

NodeId LxmfAdapter::getNodeId() const
{
    return identity_.nodeId();
}

bool LxmfAdapter::deriveVmpContactSecret(NodeId peer_id, uint8_t out_secret[32])
{
    if (!out_secret || peer_id == 0U ||
        peer_id == ::chat::voice::vmp::kBroadcastTargetId ||
        !identity_.isReady())
    {
        return false;
    }
    const PeerInfo* const peer = findOrLoadPeerByNodeId(peer_id);
    if (!peer || isZeroBytes(peer->enc_pub, sizeof(peer->enc_pub)))
    {
        return false;
    }

    uint8_t identity_shared_secret[LxmfIdentity::kEncPubKeySize] = {};
    const bool derived = identity_.deriveSharedSecret(peer->enc_pub, identity_shared_secret) &&
                         ::chat::voice::vmp::deriveVmpContactSecret(
                             identity_shared_secret,
                             ::chat::voice::vmp::ContactSecretIdentityFamily::Reticulum,
                             identity_.nodeId(),
                             peer_id,
                             out_secret);
    volatile uint8_t* const cursor = identity_shared_secret;
    for (std::size_t index = 0U; index < sizeof(identity_shared_secret); ++index)
    {
        cursor[index] = 0U;
    }
    return derived;
}

bool LxmfAdapter::getReticulumLocalIdentityInfo(ReticulumLocalIdentityInfo* out) const
{
    if (!out)
    {
        return false;
    }
    *out = ReticulumLocalIdentityInfo{};
    out->anonymous_peer = config_.reticulum_anonymous_peer;
    const char* display_name = effectiveDisplayName();
    if (display_name && display_name[0] != '\0')
    {
        copyCString(out->display_name, sizeof(out->display_name), display_name);
    }
    if (!identity_.isReady())
    {
        return false;
    }

    out->ready = true;
    out->node_id = identity_.nodeId();
    std::memcpy(out->identity_hash,
                identity_.identityHash(),
                reticulum::kTruncatedHashSize);
    localDestinationHash(LocalDestinationKind::Delivery, out->lxmf_address);
    localDestinationHash(LocalDestinationKind::Propagation, out->propagation_address);
    return true;
}

MeshActionResult LxmfAdapter::startReticulumAudioCall(
    const ReticulumPeerIdentity& destination)
{
    if (config_.reticulum_anonymous_peer)
    {
        return MeshActionResult::fail(MeshOperationFailure::Unsupported);
    }
    if (!chat::hasReticulumDestinationIdentity(destination) || !identity_.isReady())
    {
        return MeshActionResult::fail(MeshOperationFailure::InvalidInput);
    }

    interfaces_.maintain();
    if (!interfaces_.wifiGatewayConfigured() || !interfaces_.hasReadyWifiGateway())
    {
        return MeshActionResult::fail(MeshOperationFailure::NotReady);
    }
    if (::platform::ui::reticulum_call::realtime_phase() !=
        ::platform::ui::reticulum_call::RealtimePhase::Idle)
    {
        return MeshActionResult::fail(MeshOperationFailure::Busy);
    }

    const PeerInfo* peer = findOrLoadPeerByDestinationHash(destination.destination_hash);
    if (!peer && !isZeroBytes(destination.identity_hash, sizeof(destination.identity_hash)))
    {
        peer = destination_registry_.findByIdentityHash(destination.identity_hash);
    }

    uint8_t remote_identity_hash[reticulum::kTruncatedHashSize] = {};
    if (peer && !isZeroBytes(peer->identity_hash, sizeof(peer->identity_hash)))
    {
        copyHash(remote_identity_hash, peer->identity_hash, sizeof(remote_identity_hash));
    }
    else if (!isZeroBytes(destination.identity_hash, sizeof(destination.identity_hash)))
    {
        copyHash(remote_identity_hash,
                 destination.identity_hash,
                 sizeof(remote_identity_hash));
    }
    else
    {
        return MeshActionResult::fail(MeshOperationFailure::PeerKeyMissing);
    }

    constexpr ReticulumCallWireProfile wire_profile =
        ReticulumCallWireProfile::SidebandLxst;
    uint8_t call_destination_hash[reticulum::kTruncatedHashSize] = {};
    callDestinationHashForIdentity(remote_identity_hash,
                                   wire_profile,
                                   call_destination_hash);
    if (isZeroBytes(call_destination_hash, sizeof(call_destination_hash)))
    {
        return MeshActionResult::fail(MeshOperationFailure::EncodeFailed);
    }

    if (link_manager_.findOpenSessionByDestination(
            call_destination_hash, LocalDestinationKind::CallAudio))
    {
        return MeshActionResult::fail(MeshOperationFailure::Busy);
    }

    const PathEntry* path =
        path_manager_.findPath(call_destination_hash, millis(), kPathTtlMs);
    if (isLoRaPath(path))
    {
        char call_hash[12] = {};
        formatHashPrefix(call_destination_hash, call_hash, sizeof(call_hash));
        Serial.printf("[LXMF][CallTX] ignore_path dest=%s reason=lora_not_supported\n",
                      call_hash);
        path = nullptr;
    }
    bool path_requested = false;
    bool path_waiting = false;
    if (!path)
    {
        const PendingPathRequest* pending =
            path_manager_.findPendingPathRequest(call_destination_hash);
        path_waiting = pending && !pending->resolved;
        if (!path_waiting)
        {
            path_requested =
                sendPathRequestForDestination(call_destination_hash);
            path_waiting = path_requested;
        }
    }

    const uint32_t now_ms = millis();
    runtime::LinkSessionSpec session_spec{};
    session_spec.now_ms = now_ms;
    session_spec.keepalive_interval_ms = kLinkKeepaliveMaxMs;
    session_spec.stale_timeout_ms = kLinkKeepaliveMaxMs * 2U;
    session_spec.remote_destination_hash = call_destination_hash;
    session_spec.remote_identity_hash = remote_identity_hash;
    session_spec.peer_identity_sig_pub = peer ? peer->sig_pub : nullptr;
    session_spec.expected_hops = path ? path->hops : 0;
    session_spec.destination = LocalDestinationKind::CallAudio;
    session_spec.state = LinkState::Pending;
    session_spec.initiator = true;
    session_spec.remote_identity_known =
        !isZeroBytes(remote_identity_hash, sizeof(remote_identity_hash));

    LinkSession* new_session =
        link_manager_.openSession(kMaxLinkSessions, session_spec);
    if (!new_session)
    {
        return MeshActionResult::fail(MeshOperationFailure::Busy);
    }
    LinkSession& session = *new_session;
    lxst_telephony_client_.beginCallerSession(
        session,
        wire_profile,
        call_profile::kEmbeddedLxstProfile,
        now_ms);

    Curve25519::dh1(session.local_enc_pub, session.local_enc_priv);
    if (isZeroBytes(session.local_enc_priv, sizeof(session.local_enc_priv)) ||
        !generateLinkSigningKey(session.local_sig_pub, session.local_sig_priv) ||
        !prepareLinkRequest(session))
    {
        link_manager_.discardSession(session);
        return MeshActionResult::fail(MeshOperationFailure::EncodeFailed);
    }

    ::platform::ui::reticulum_call::Peer call_peer{};
    copyHash(call_peer.link_id, session.link_id, sizeof(call_peer.link_id));
    if (peer)
    {
        copyHash(call_peer.destination_hash,
                 peer->destination_hash,
                 sizeof(call_peer.destination_hash));
        copyHash(call_peer.identity_hash,
                 peer->identity_hash,
                 sizeof(call_peer.identity_hash));
        call_peer.display_name =
            peer->display_name[0] != '\0' ? peer->display_name : nullptr;
    }
    else
    {
        copyHash(call_peer.destination_hash,
                 destination.destination_hash,
                 sizeof(call_peer.destination_hash));
        copyHash(call_peer.identity_hash,
                 remote_identity_hash,
                 sizeof(call_peer.identity_hash));
    }
    call_peer.incoming = false;
    call_peer.wire_profile =
        call_profile::runtimeWireProfile(session.call_wire_profile);
    call_peer.codec2_mode =
        call_profile::runtimeCodec2Mode(session.call_wire_profile,
                                        lxst_telephony_client_.profile(session));
    if (!::platform::ui::reticulum_call::begin_outgoing(call_peer))
    {
        link_manager_.discardSession(session);
        return MeshActionResult::fail(MeshOperationFailure::Busy);
    }
    lxst_telephony_client_.markRuntimeStarted(session, true);

    if ((path && !sendLinkRequest(session)) || (!path && !path_waiting))
    {
        ::platform::ui::reticulum_call::notify_link_closed(session.link_id);
        link_manager_.discardSession(session);
        return MeshActionResult::fail(MeshOperationFailure::RadioTxFailed);
    }

    char call_hash[12] = {};
    formatHashPrefix(call_destination_hash,
                     call_hash,
                     sizeof(call_hash));
    Serial.printf("[LXMF][CallTX] start dest=%s wire=%u path=%u requested=%u waiting=%u link_pending=1\n",
                  call_hash,
                  static_cast<unsigned>(wire_profile),
                  path ? 1U : 0U,
                  path_requested ? 1U : 0U,
                  path_waiting ? 1U : 0U);

    return MeshActionResult::success();
}

MeshActionResult LxmfAdapter::pingReticulumDestination(
    const ReticulumPeerIdentity& destination)
{
    char dest_hash[12] = {};
    char dest_full[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    formatHashPrefix(destination.destination_hash, dest_hash, sizeof(dest_hash));
    formatHashHex(destination.destination_hash,
                  sizeof(destination.destination_hash),
                  dest_full,
                  sizeof(dest_full));
    Serial.printf("[LXMF][PingTX] begin dest=%s dest_full=%s ready=%u\n",
                  dest_hash,
                  dest_full,
                  isReady() ? 1U : 0U);

    if (!chat::hasReticulumDestinationIdentity(destination))
    {
        Serial.printf("[LXMF][PingTX] reject reason=invalid_input dest=%s\n",
                      dest_hash);
        return MeshActionResult::fail(MeshOperationFailure::InvalidInput);
    }
    if (!isReady())
    {
        Serial.printf("[LXMF][PingTX] reject reason=not_ready dest=%s\n",
                      dest_hash);
        return MeshActionResult::fail(MeshOperationFailure::NotReady);
    }
    if (isConfiguredGroupDestination(destination))
    {
        Serial.printf("[LXMF][PingTX] reject reason=group_destination dest=%s\n",
                      dest_hash);
        return MeshActionResult::fail(MeshOperationFailure::Unsupported);
    }

    // This method is reached from LVGL input callbacks. It validates and
    // records intent only; path lookup, crypto, and radio TX are owned by
    // pumpPendingPingRequests() in the post-frame application runtime.
    return queuePendingReticulumPing(destination.destination_hash);
}

MeshActionResult LxmfAdapter::sendReticulumPingToPeer(
    PeerInfo& peer,
    uint32_t operation_started_ms)
{
    char dest_hash[12] = {};
    char dest_full[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    formatHashPrefix(peer.destination_hash, dest_hash, sizeof(dest_hash));
    formatHashHex(peer.destination_hash,
                  sizeof(peer.destination_hash),
                  dest_full,
                  sizeof(dest_full));

    if (shouldRequestPath(peer))
    {
        const bool requested = sendPathRequest(peer);
        Serial.printf("[LXMF][PingTX] path_refresh dest=%s requested=%u\n",
                      dest_hash,
                      requested ? 1U : 0U);
    }

    runtime::RuntimeByteBuffer packet(kMaxPacketLen, 0);
    size_t packet_len = packet.size();
    if (!buildEncryptedPacketForPeer(peer, nullptr, 0, packet.data(), &packet_len))
    {
        Serial.printf("[LXMF][PingTX] reject reason=build_packet_failed dest=%s\n",
                      dest_hash);
        return MeshActionResult::fail(MeshOperationFailure::CryptoFailed);
    }

    const bool ok = routeAndSendPacket(packet.data(), packet_len, true);
    if (ok)
    {
        uint8_t packet_hash[reticulum::kFullHashSize] = {};
        reticulum::computePacketHash(packet.data(), packet_len, packet_hash);
        path_manager_.notePendingPingReceipt(
            packet_hash,
            peer.destination_hash,
            peer.sig_pub,
            operation_started_ms == 0 ? millis()
                                      : operation_started_ms,
            kMaxPendingPingReceipts);
    }
    const auto& tx_result = interfaces_.lastTxResult();
    Serial.printf("[LXMF][PingTX] raw_send ok=%u dest=%s dest_full=%s bearer=%s complete=%u receipt=%u packet_len=%u\n",
                  ok ? 1U : 0U,
                  dest_hash,
                  dest_full,
                  txBearerName(tx_result),
                  tx_result.reachedRequiredInterfaces() ? 1U : 0U,
                  ok ? 1U : 0U,
                  static_cast<unsigned>(packet_len));
    return ok ? MeshActionResult::success()
              : MeshActionResult::fail(MeshOperationFailure::RadioTxFailed);
}

MeshActionResult LxmfAdapter::queuePendingReticulumPing(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    char dest_hash[12] = {};
    formatHashPrefix(destination_hash, dest_hash, sizeof(dest_hash));
    const runtime::PendingPingQueueResult queue_result =
        ping_service_.queue(destination_hash, millis(), kMaxPendingPingRequests);
    if (queue_result == runtime::PendingPingQueueResult::Duplicate)
    {
        Serial.printf("[LXMF][PingTX] path_pending dest=%s queued=1 duplicate=1\n",
                      dest_hash);
        MeshActionResult result = MeshActionResult::success();
        result.detail = 1;
        return result;
    }
    if (queue_result == runtime::PendingPingQueueResult::Full)
    {
        Serial.printf("[LXMF][PingTX] reject reason=pending_full dest=%s depth=%u\n",
                      dest_hash,
                      static_cast<unsigned>(ping_service_.size()));
        return MeshActionResult::fail(MeshOperationFailure::Busy);
    }
    if (queue_result != runtime::PendingPingQueueResult::Queued)
    {
        return MeshActionResult::fail(MeshOperationFailure::InvalidInput);
    }

    Serial.printf("[LXMF][PingTX] path_pending dest=%s queued=1 dispatch=post_frame depth=%u\n",
                  dest_hash,
                  static_cast<unsigned>(ping_service_.size()));
    MeshActionResult result = MeshActionResult::success();
    result.detail = 1;
    return result;
}

void LxmfAdapter::pumpPendingPingRequests()
{
    const uint32_t now_ms = millis();
    const bool call_preempt_active =
        ::platform::ui::reticulum_call::resource_preempt_active();
    ping_service_.pump(
        now_ms,
        call_preempt_active,
        kPendingPingReceiptTtlMs,
        kPendingPingSendRetryMs,
        kPathRequestMinIntervalMs,
        [this](const uint8_t destination_hash[reticulum::kTruncatedHashSize])
        {
            PeerInfo* peer = findOrLoadPeerByDestinationHash(destination_hash);
            return peer &&
                   !isZeroBytes(peer->identity_hash, sizeof(peer->identity_hash)) &&
                   !isZeroBytes(peer->sig_pub, sizeof(peer->sig_pub)) &&
                   (peerHasUsableRatchet(*peer) ||
                    !isZeroBytes(peer->enc_pub, sizeof(peer->enc_pub)));
        },
        [this](const uint8_t destination_hash[reticulum::kTruncatedHashSize],
               uint32_t created_ms,
               uint32_t elapsed_ms)
        {
            char dest_hash[12] = {};
            formatHashPrefix(destination_hash, dest_hash, sizeof(dest_hash));
            Serial.printf("[LXMF][PingTX] dispatch_after_path dest=%s elapsed_ms=%lu\n",
                          dest_hash,
                          static_cast<unsigned long>(elapsed_ms));
            PeerInfo* peer = findOrLoadPeerByDestinationHash(destination_hash);
            if (!peer)
            {
                return false;
            }
            return sendReticulumPingToPeer(*peer, created_ms).ok;
        },
        [this](const uint8_t destination_hash[reticulum::kTruncatedHashSize],
               uint32_t elapsed_ms)
        {
            const bool requested = sendPathRequestForDestination(destination_hash);
            char dest_hash[12] = {};
            formatHashPrefix(destination_hash, dest_hash, sizeof(dest_hash));
            Serial.printf("[LXMF][PingTX] path_retry dest=%s requested=%u elapsed_ms=%lu\n",
                          dest_hash,
                          requested ? 1U : 0U,
                          static_cast<unsigned long>(elapsed_ms));
        },
        [](const runtime::PendingPingRequest& request, uint32_t elapsed_ms)
        {
            char dest_hash[12] = {};
            formatHashPrefix(request.destination_hash, dest_hash, sizeof(dest_hash));
            Serial.printf("[LXMF][PingRX] timeout dest=%s elapsed_ms=%lu stage=path\n",
                          dest_hash,
                          static_cast<unsigned long>(elapsed_ms));
            sys::EventBus::publish(
                new sys::ReticulumPingResultEvent(
                    request.destination_hash,
                    sys::ReticulumPingResult::Timeout,
                    elapsed_ms),
                100);
        });
}

MeshActionResult LxmfAdapter::persistReticulumPeer(
    const ReticulumPeerIdentity& destination,
    bool favorite)
{
    if (!chat::hasReticulumDestinationIdentity(destination))
    {
        return MeshActionResult::fail(MeshOperationFailure::InvalidInput);
    }

    PeerInfo* peer = findOrLoadPeerByDestinationHash(destination.destination_hash);
    if (!peer && !isZeroBytes(destination.identity_hash, sizeof(destination.identity_hash)))
    {
        if (const PeerInfo* by_identity =
                destination_registry_.findByIdentityHash(destination.identity_hash))
        {
            peer = findOrLoadPeerByDestinationHash(by_identity->destination_hash);
        }
    }
    if (!peer)
    {
        return MeshActionResult::fail(MeshOperationFailure::PeerKeyMissing);
    }

    const MeshActionResult result = persistPeerAddressNow(*peer, favorite);
    if (result.ok)
    {
        publishPeerUpdate(*peer);
    }
    return result;
}

MeshActionResult LxmfAdapter::requestNomadPage(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const char* path,
    const uint8_t* request_data,
    std::size_t request_data_len)
{
    char destination_text[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    formatHashHex(destination_hash,
                  reticulum::kTruncatedHashSize,
                  destination_text,
                  sizeof(destination_text));
    LXMF_NOMAD_PAGE_LOG("queue begin dest=%s path=%s ready=%u pending=%u\n",
                        destination_text,
                        path ? path : "<null>",
                        isReady() ? 1U : 0U,
                        static_cast<unsigned>(network_page_client_.size()));

    if (!destination_hash ||
        isZeroBytes(destination_hash, reticulum::kTruncatedHashSize) ||
        !path || path[0] == '\0' ||
        std::strlen(path) >= kNomadPagePathMaxLen ||
        (request_data_len != 0 && !request_data) ||
        request_data_len > PendingNomadPageRequest::kMaxRequestDataBytes)
    {
        LXMF_NOMAD_PAGE_LOG("queue reject reason=invalid_input dest=%s path=%s\n",
                            destination_text,
                            path ? path : "<null>");
        return MeshActionResult::fail(MeshOperationFailure::InvalidInput);
    }
    if (!isReady())
    {
        LXMF_NOMAD_PAGE_LOG("queue reject reason=not_ready dest=%s path=%s\n",
                            destination_text,
                            path);
        return MeshActionResult::fail(MeshOperationFailure::NotReady);
    }

    PendingNomadPageRequest* queued_request = nullptr;
    const runtime::NetworkPageQueueResult queue_result =
        network_page_client_.queue(destination_hash,
                                   path,
                                   request_data,
                                   request_data_len,
                                   millis(),
                                   kMaxPendingNomadPageRequests,
                                   kNomadPagePathMaxLen,
                                   &queued_request);
    if (queue_result == runtime::NetworkPageQueueResult::Duplicate &&
        queued_request)
    {
        updateNomadPageProgress(*queued_request,
                                5,
                                "Queued Nomad page request",
                                path,
                                true,
                                false,
                                PageFailureKind::None);
        MeshActionResult result = MeshActionResult::success();
        result.detail = 1;
        LXMF_NOMAD_PAGE_LOG("queue duplicate dest=%s path=%s\n",
                            destination_text,
                            path);
        return result;
    }
    if (queue_result == runtime::NetworkPageQueueResult::Full)
    {
        LXMF_NOMAD_PAGE_LOG("queue reject reason=busy dest=%s path=%s depth=%u\n",
                            destination_text,
                            path,
                            static_cast<unsigned>(network_page_client_.size()));
        return MeshActionResult::fail(MeshOperationFailure::Busy);
    }
    if (queue_result != runtime::NetworkPageQueueResult::Queued ||
        !queued_request)
    {
        LXMF_NOMAD_PAGE_LOG("queue reject reason=invalid_owner_result dest=%s path=%s\n",
                            destination_text,
                            path);
        return MeshActionResult::fail(MeshOperationFailure::InvalidInput);
    }

    updateNomadPageProgress(*queued_request,
                            5,
                            "Queued Nomad page request",
                            path,
                            true,
                            false,
                            PageFailureKind::None);

    LXMF_NOMAD_PAGE_LOG("queued dest=%s path=%s pending=%u\n",
                        destination_text,
                        path,
                        static_cast<unsigned>(network_page_client_.size()));
    pumpNomadPageRequests();
    return MeshActionResult::success();
}

bool LxmfAdapter::cancelNomadPage(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const char* path)
{
    PendingNomadPageRequest removed{};
    if (!network_page_client_.erasePage(destination_hash, path, &removed))
    {
        return false;
    }

    LinkSession* session = link_manager_.findOpenSessionByDestination(
        destination_hash, LocalDestinationKind::NomadPage);
    if (!session)
    {
        return true;
    }

    for (std::size_t index = session->incoming_resources.size(); index > 0; --index)
    {
        const LinkResourceTransfer& resource =
            session->incoming_resources[index - 1U];
        if (!removed.request_sent ||
            resource.request_id.size() != sizeof(removed.request_id) ||
            std::memcmp(resource.request_id.data(),
                        removed.request_id,
                        sizeof(removed.request_id)) != 0)
        {
            continue;
        }
        uint8_t resource_hash[reticulum::kFullHashSize] = {};
        copyHash(resource_hash, resource.resource_hash, sizeof(resource_hash));
        (void)sendLinkPacket(*session,
                             reticulum::PacketType::Data,
                             reticulum::PacketContext::ResourceRcl,
                             resource_hash,
                             sizeof(resource_hash),
                             true);
        (void)link_manager_.eraseIncomingResource(*session, resource_hash);
    }
    session->pending_requests.erase(
        std::remove_if(
            session->pending_requests.begin(),
            session->pending_requests.end(),
            [&removed](const LinkPendingRequest& request)
            {
                return removed.request_sent &&
                       request.request_id.size() == sizeof(removed.request_id) &&
                       std::memcmp(request.request_id.data(),
                                   removed.request_id,
                                   sizeof(removed.request_id)) == 0;
            }),
        session->pending_requests.end());
    return true;
}

bool LxmfAdapter::cancelIncomingResource(
    const uint8_t link_id[reticulum::kTruncatedHashSize],
    const uint8_t resource_hash[reticulum::kFullHashSize])
{
    LinkSession* session = findLinkSession(link_id);
    if (!session || session->destination != LocalDestinationKind::Delivery ||
        !link_manager_.findIncomingResource(*session, resource_hash))
    {
        return false;
    }
    const bool sent = sendLinkPacket(*session,
                                     reticulum::PacketType::Data,
                                     reticulum::PacketContext::ResourceRcl,
                                     resource_hash,
                                     reticulum::kFullHashSize,
                                     true);
    const bool erased = link_manager_.eraseIncomingResource(*session, resource_hash);
    return sent && erased;
}

void LxmfAdapter::applyConfig(const MeshConfig& config)
{
    config_ = config;
    if (identity_.init() && !peers_loaded_)
    {
        loadPersistedPeers();
    }
    rtnet::initialize(config_);
    interfaces_.applyConfig(config_, rtnet::active());
    network_config_generation_ = rtnet::status().generation;
    if (identity_.isReady())
    {
        uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t propagation_hash[reticulum::kTruncatedHashSize] = {};
        localDestinationHash(LocalDestinationKind::Delivery, delivery_hash);
        localDestinationHash(LocalDestinationKind::Propagation, propagation_hash);

        char identity_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        char delivery_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        char propagation_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        formatHashHex(identity_.identityHash(),
                      reticulum::kTruncatedHashSize,
                      identity_hex,
                      sizeof(identity_hex));
        formatHashHex(delivery_hash,
                      sizeof(delivery_hash),
                      delivery_hex,
                      sizeof(delivery_hex));
        formatHashHex(propagation_hash,
                      sizeof(propagation_hash),
                      propagation_hex,
                      sizeof(propagation_hex));
        Serial.printf("[LXMF][Identity] node=%08lX identity=%s delivery=%s propagation=%s anonymous=%d peers=%u\n",
                      static_cast<unsigned long>(identity_.nodeId()),
                      identity_hex,
                      delivery_hex,
                      propagation_hex,
                      config_.reticulum_anonymous_peer ? 1 : 0,
                      static_cast<unsigned>(destination_registry_.size()));
    }
    announce_scheduler_.resetAfterConfig(millis(), config_.reticulum_anonymous_peer);
}

void LxmfAdapter::setUserInfo(const char* long_name, const char* short_name)
{
    user_long_name_ = (long_name && long_name[0] != '\0') ? long_name : "";
    user_short_name_ = (short_name && short_name[0] != '\0') ? short_name : "";
    announce_scheduler_.markIdentityChanged();
}

bool LxmfAdapter::setWifiTransportEnabled(bool enabled)
{
    interfaces_.setWifiTransportEnabled(enabled);
    return true;
}

bool LxmfAdapter::isReady() const
{
    return identity_.isReady() && interfaces_.hasReadyInterface();
}

bool LxmfAdapter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    (void)out_data;
    (void)out_len;
    (void)max_len;
    return false;
}

void LxmfAdapter::handleRawPacket(const uint8_t* data, size_t size)
{
    interfaces_.handleRawPacket(data, size);
}

void LxmfAdapter::setLastRxStats(float rssi, float snr)
{
    interfaces_.setLastRxStats(rssi, snr);
}

void LxmfAdapter::processSendQueue()
{
    processRuntime();
}

LxmfAdapter::RuntimeBudget LxmfAdapter::makeRuntimeBudget() const
{
    runtime::RuntimeBudgetInput input{};
    input.max_ingress_packets_per_poll = kMaxIngressPacketsPerPoll;
    input.call_ingress_packets_per_poll = kCallIngressPacketsPerPoll;
    input.call_realtime_active =
        ::platform::ui::reticulum_call::realtime_mode_active();
    input.nomad_request_active = !network_page_client_.empty();
    input.screen_sleeping = screen_runtime::is_sleeping();
    input.screen_saver_active = screen_runtime::is_saver_active();
    return runtime::makeRuntimeBudget(input);
}

void LxmfAdapter::processRuntime()
{
    rtnet::poll(config_);
    const auto network_status = rtnet::status();
    if (network_status.generation != network_config_generation_)
    {
        link_manager_.forEachSession(
            [this](LinkSession& session)
            {
                if (session.state != LinkState::Closed)
                {
                    closeLinkSession(session, LinkCloseReason::Error);
                }
            });
        link_manager_.clear();
        path_manager_.clear();
        deferred_discovery_.clear();
        geocaching_discovery_probe_.reset();

        propagation_client_.resetForNetworkConfig(
            rtnet::active().propagation.sync_on_start);

        interfaces_.applyConfig(config_, rtnet::active());
        network_config_generation_ = network_status.generation;
        Serial.printf("[LXMF][NetworkConfig] generation=%lu source=%s interfaces=%u\n",
                      static_cast<unsigned long>(network_status.generation),
                      rtnet::source_name(network_status.source),
                      static_cast<unsigned>(network_status.configured_interfaces));
    }

    const RuntimeBudget budget = makeRuntimeBudget();
    processRadioPackets(budget);
    if (geocaching_discovery_probe_.take(millis(), geocaching_announcement_handler_ != nullptr,
                                         identity_.isReady() && interfaces_.hasReadyWifiGateway(), budget))
    {
        const bool requested = sendPathRequestForDestination(runtime::kGeocachingDiscoverySeed.data());
        Serial.printf("[Geocaching][Discovery] path_request requested=%u retry_ms=60000\n", requested ? 1U : 0U);
    }
    pumpPendingPingRequests();
    pumpReticulumAudioCall();
    cullTransportState();
    if (::platform::ui::reticulum_call::realtime_mode_active())
    {
        return;
    }

    const runtime::PropagationRuntimeLimits propagation_limits{
        kMaxPropagationEntries,
        kMaxPropagationTransients,
        kMaxPropagationPeers,
        kPropagationEntryTtlS,
        kPropagationTransientTtlS,
        kPropagationEntryTtlS};
    propagation_client_.cull(currentTimestampSeconds(), propagation_limits);

    pumpNomadPageRequests();
    if (budget.allow_propagation_client)
    {
        processPropagationClient();
    }
    processDeferredDiscoveryPackets(budget);

    if (budget.allow_peer_projection)
    {
        pumpPendingPeerUpdates();
    }
    // A sleeping Pager normally suppresses periodic announces, but it must
    // still complete the initial (or retry) announce after a reboot/config
    // change so direct peers can discover its LXMF destination again.
    if (budget.allow_announce_tx || announce_scheduler_.isPending())
    {
        maybeAnnounce();
    }
}

void LxmfAdapter::processRadioPackets(const RuntimeBudget& budget)
{
    uint8_t polled_packets = 0;
    while (polled_packets < budget.live_packet_limit &&
           interfaces_.pollIncomingPacket(&scratch_.rx_packet))
    {
        ++polled_packets;
        (void)processOneRadioPacket(scratch_.rx_packet, budget, false);
    }

    MeshIncomingData discarded;
    while (interfaces_.pollLegacyIncomingData(&discarded))
    {
    }
}

bool LxmfAdapter::processOneRadioPacket(
    const reticulum::interfaces::RxPacket& rx_packet,
    const RuntimeBudget& budget,
    bool deferred_replay)
{
    const uint8_t* packet = rx_packet.data;
    const size_t packet_len = rx_packet.len;
    const auto ingress_interface = rx_packet.interface_kind;
    const bool ingress_wifi =
        ingress_interface != reticulum::interfaces::InterfaceKind::LoRa;
    const char* iface_label =
        ingress_interface == reticulum::interfaces::InterfaceKind::LoRa
            ? "lora"
            : (ingress_interface == reticulum::interfaces::InterfaceKind::Auto
                   ? "auto"
                   : "tcp");
    active_rx_meta_ = rx_packet.rx_meta;
    has_active_rx_meta_ = true;
    active_ingress_interface_id_ = rx_packet.interface_id;
    struct ActiveIngressScope
    {
        bool& active;
        reticulum::interfaces::InterfaceId& interface_id;
        ~ActiveIngressScope()
        {
            active = false;
            interface_id = reticulum::interfaces::kInvalidInterfaceId;
        }
    } active_ingress_scope{has_active_rx_meta_, active_ingress_interface_id_};

    reticulum::ParsedPacket parsed{};
    if (!reticulum::parsePacket(packet, packet_len, &parsed))
    {
        noteRxSummary(false, false, true);
        if (!ingress_wifi)
        {
            Serial.printf("[LXMF][RawRX] drop reason=parse_failed raw_len=%u first=%02X\n",
                          static_cast<unsigned>(packet_len),
                          static_cast<unsigned>(packet_len > 0 ? packet[0] : 0));
        }
        return false;
    }

    char dest_hash[12] = {};
    formatHashPrefix(parsed.destination_hash, dest_hash, sizeof(dest_hash));
    if (::platform::ui::reticulum_call::resource_preempt_active())
    {
        uint8_t call_link_id[reticulum::kTruncatedHashSize] = {};
        const bool has_call_link =
            ::platform::ui::reticulum_call::current_link_id(call_link_id);
        LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
        const bool call_link_request =
            ingress_wifi &&
            parsed.packet_type == reticulum::PacketType::LinkRequest &&
            isLocalDestinationHash(parsed.destination_hash, &local_kind) &&
            local_kind == LocalDestinationKind::CallAudio;
        const bool current_call_link_packet =
            has_call_link &&
            parsed.destination_type == reticulum::DestinationType::Link &&
            parsed.destination_hash &&
            hashesEqual(parsed.destination_hash,
                        call_link_id,
                        reticulum::kTruncatedHashSize);
        const LinkSession* call_session =
            has_call_link ? findLinkSession(call_link_id) : nullptr;
        const bool current_call_path_response =
            ingress_wifi && call_session && call_session->initiator &&
            call_session->destination == LocalDestinationKind::CallAudio &&
            call_session->state == LinkState::Pending &&
            parsed.packet_type == reticulum::PacketType::Announce &&
            parsed.context ==
                static_cast<uint8_t>(reticulum::PacketContext::PathResponse) &&
            parsed.destination_hash &&
            hashesEqual(parsed.destination_hash,
                        call_session->remote_destination_hash,
                        reticulum::kTruncatedHashSize);
        if (!call_link_request && !current_call_link_packet &&
            !current_call_path_response)
        {
            noteRxSummary(true, false, false);
            return false;
        }
    }
    const bool geocaching_discovery = runtime::GeocachingDiscoveryBudget::matches(
        parsed, geocaching_announcement_handler_ != nullptr, budget);
    if (budget.drop_public_discovery && !geocaching_discovery &&
        isPublicDiscoveryPacket(parsed) &&
        !isForegroundDiscoveryDestination(parsed.destination_hash))
    {
        noteRxSummary(false, false, false, false, false, true);
        return false;
    }

    noteRxSummary();
    const bool log_detail =
        shouldLogRxDetail(parsed, ingress_interface, budget) && !deferred_replay;
    if (log_detail)
    {
        Serial.printf("[LXMF][RawRX] packet iface=%s type=%u dest_type=%u context=%u dest=%s raw_len=%u payload_len=%u hops=%u phase=%s\n",
                      iface_label,
                      static_cast<unsigned>(parsed.packet_type),
                      static_cast<unsigned>(parsed.destination_type),
                      static_cast<unsigned>(parsed.context),
                      dest_hash,
                      static_cast<unsigned>(packet_len),
                      static_cast<unsigned>(parsed.payload_len),
                      static_cast<unsigned>(parsed.hops),
                      budget.phase);
    }
    else if (!deferred_replay && ingress_wifi && budget.phase &&
             std::strcmp(budget.phase, "nomad") == 0 && packet_len >= 256U)
    {
        char transport_hash[12] = {};
        formatHashPrefix(parsed.transport_id, transport_hash, sizeof(transport_hash));
        Serial.printf("[LXMF][RawRX] packet_probe iface=%s type=%u dest_type=%u context=%u dest=%s transport=%s raw_len=%u payload_len=%u hops=%u phase=%s\n",
                      iface_label,
                      static_cast<unsigned>(parsed.packet_type),
                      static_cast<unsigned>(parsed.destination_type),
                      static_cast<unsigned>(parsed.context),
                      dest_hash,
                      transport_hash,
                      static_cast<unsigned>(packet_len),
                      static_cast<unsigned>(parsed.payload_len),
                      static_cast<unsigned>(parsed.hops),
                      budget.phase);
    }
    if (parsed.hops < 0xFF)
    {
        parsed.hops += 1;
    }

    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    reticulum::computePacketHash(packet, packet_len, packet_hash);
    // A requested path response may contain exactly the cached announcement
    // seen before this Geocaching session opened. Re-run normal verification
    // under the discovery budget; other duplicate traffic remains rejected.
    const bool geocaching_path_response = geocaching_discovery &&
                                          parsed.context == static_cast<uint8_t>(reticulum::PacketContext::PathResponse);
    if (path_manager_.isDuplicatePacket(packet_hash) && !geocaching_path_response)
    {
        noteRxSummary(false, true, false);
        if (!ingress_wifi && !deferred_replay)
        {
            Serial.printf("[LXMF][RawRX] drop reason=duplicate dest=%s\n", dest_hash);
        }
        return false;
    }

    if (geocaching_discovery && !geocaching_discovery_budget_.consume(millis()))
    {
        noteRxSummary(false, false, false, false, false, true);
        return false;
    }
    if (!geocaching_discovery && shouldDeferDiscoveryPacket(parsed, ingress_interface, budget))
    {
        if (deferred_replay)
        {
            noteRxSummary(false, false, false, false, true);
            return false;
        }
        if (!hasDeferredDiscoveryPacket(packet_hash) &&
            enqueueDeferredDiscoveryPacket(rx_packet, packet_hash))
        {
            noteRxSummary(false, false, false, true, false);
            return true;
        }
        noteRxSummary(false, false, false, true, true);
        return false;
    }

    if (!geocaching_discovery && !deferred_replay && ingress_wifi && !shouldProcessWifiIngressPacket(parsed, budget))
    {
        if (budget.phase && std::strcmp(budget.phase, "nomad") == 0 &&
            (packet_len >= 256U ||
             parsed.destination_type == reticulum::DestinationType::Link ||
             parsed.context == static_cast<uint8_t>(reticulum::PacketContext::Resource)))
        {
            char transport_hash[12] = {};
            formatHashPrefix(parsed.transport_id, transport_hash, sizeof(transport_hash));
            Serial.printf("[LXMF][RawRX] skip reason=wifi_not_foreground type=%u dest_type=%u context=%u dest=%s transport=%s raw_len=%u payload_len=%u hops=%u phase=%s\n",
                          static_cast<unsigned>(parsed.packet_type),
                          static_cast<unsigned>(parsed.destination_type),
                          static_cast<unsigned>(parsed.context),
                          dest_hash,
                          transport_hash,
                          static_cast<unsigned>(packet_len),
                          static_cast<unsigned>(parsed.payload_len),
                          static_cast<unsigned>(parsed.hops),
                          budget.phase);
        }
        noteRxSummary(true, false, false);
        return false;
    }

    path_manager_.rememberPacket(packet_hash, millis(), kMaxPacketFilter);

    switch (packet_router_.route(parsed))
    {
    case runtime::PacketRoute::Announce:
        return handleAnnouncePacket(packet,
                                    packet_len,
                                    parsed,
                                    ingress_interface,
                                    budget.allow_persistence);
    case runtime::PacketRoute::Proof:
        return handleProofPacket(packet, packet_len, parsed, ingress_interface);
    case runtime::PacketRoute::LinkRequest:
        return handleLinkRequestPacket(packet, packet_len, parsed, ingress_interface);
    case runtime::PacketRoute::Data:
        if (!handlePathRequestPacket(parsed) &&
            !handleCacheRequestPacket(parsed) &&
            !handleLocalLinkPacket(packet, packet_len, parsed, ingress_interface) &&
            !maybeForwardLinkPacket(packet, packet_len, parsed) &&
            !maybeForwardTransportPacket(packet, packet_len, parsed))
        {
            return handleDataPacket(packet, packet_len, parsed);
        }
        return true;
    case runtime::PacketRoute::LinkOrTransport:
        break;
    }

    const bool handled_local =
        handleLocalLinkPacket(packet, packet_len, parsed, ingress_interface);
    const bool handled_link = maybeForwardLinkPacket(packet, packet_len, parsed);
    const bool handled_transport = maybeForwardTransportPacket(packet, packet_len, parsed);
    return handled_local || handled_link || handled_transport;
}

bool LxmfAdapter::shouldDeferDiscoveryPacket(
    const reticulum::ParsedPacket& packet,
    reticulum::interfaces::InterfaceKind ingress_interface,
    const RuntimeBudget& budget)
{
    if (!isPublicDiscoveryPacket(packet))
    {
        return false;
    }
    if (budget.drop_public_discovery &&
        isForegroundDiscoveryDestination(packet.destination_hash))
    {
        return false;
    }
    if (!budget.allow_public_discovery)
    {
        return true;
    }

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    // The P4-specific foreground budget above is the admission decision. Do
    // not re-apply the S3 one-announce-per-10-seconds sampler, otherwise the
    // gateway ingress rate still permanently outruns the deferred queue.
    (void)ingress_interface;
    return false;
#else
    return !consumeDiscoveryBudget(ingress_interface);
#endif
}

bool LxmfAdapter::isPublicDiscoveryPacket(const reticulum::ParsedPacket& packet) const
{
    if (packet.packet_type != reticulum::PacketType::Announce ||
        !packet.destination_hash)
    {
        return false;
    }
    if (isLocalDestinationHash(packet.destination_hash, nullptr))
    {
        return false;
    }
    return packet.context != static_cast<uint8_t>(reticulum::PacketContext::PathResponse) ||
           !path_manager_.findPendingPathRequest(packet.destination_hash);
}

bool LxmfAdapter::enqueueDeferredDiscoveryPacket(
    const reticulum::interfaces::RxPacket& packet,
    const uint8_t packet_hash[reticulum::kFullHashSize])
{
    bool dropped = false;
    const bool queued = deferred_discovery_.push(packet, packet_hash, &dropped);
    if (dropped)
    {
        noteRxSummary(false, false, false, false, true);
    }
    return queued;
}

bool LxmfAdapter::hasDeferredDiscoveryPacket(
    const uint8_t packet_hash[reticulum::kFullHashSize]) const
{
    return deferred_discovery_.contains(packet_hash);
}

void LxmfAdapter::processDeferredDiscoveryPackets(const RuntimeBudget& budget)
{
    if (!budget.allow_public_discovery || budget.deferred_discovery_limit == 0)
    {
        return;
    }

    uint8_t processed = 0;
    while (processed < budget.deferred_discovery_limit &&
           deferred_discovery_.pop(&scratch_.rx_packet))
    {
        (void)processOneRadioPacket(scratch_.rx_packet, budget, true);
        ++processed;
    }
}

void LxmfAdapter::maybeAnnounce()
{
    const uint32_t now_ms = millis();
    const runtime::AnnounceScheduleDecision decision =
        announce_scheduler_.next(now_ms,
                                 config_.reticulum_anonymous_peer,
                                 kAnnounceIntervalMs,
                                 kInitialAnnounceDelayMs,
                                 kPendingAnnounceRetryMs);
    if (!decision.should_send)
    {
        return;
    }

    const bool delivery_ok = sendAnnounce(LocalDestinationKind::Delivery);
    const bool delivery_complete = lastAnnounceTxReachedRequiredInterfaces(delivery_ok);
    const bool propagation_service =
        rtnet::active().propagation.service_enabled;
    const bool propagation_ok =
        propagation_service && sendAnnounce(LocalDestinationKind::Propagation);
    const bool propagation_complete =
        !propagation_service || lastAnnounceTxReachedRequiredInterfaces(propagation_ok);
    const bool call_audio_ok = sendAnnounce(LocalDestinationKind::CallAudio);
    const bool call_audio_complete =
        !interfaces_.wifiGatewayConfigured() ||
        lastAnnounceTxReachedRequiredInterfaces(call_audio_ok);
    announce_scheduler_.completeAttempt(
        now_ms,
        delivery_ok || propagation_ok || call_audio_ok,
        delivery_complete && propagation_complete && call_audio_complete);
}

bool LxmfAdapter::sendAnnounce(LocalDestinationKind kind,
                               reticulum::PacketContext context)
{
    interfaces_.maintain();
    if (config_.reticulum_anonymous_peer)
    {
        Serial.printf("[LXMF][AnnounceTX] skip reason=anonymous_peer kind=%u context=%u\n",
                      static_cast<unsigned>(kind),
                      static_cast<unsigned>(context));
        return false;
    }
    const bool call_audio = kind == LocalDestinationKind::CallAudio;
    if (kind == LocalDestinationKind::Propagation &&
        !rtnet::active().propagation.service_enabled)
    {
        return false;
    }
    if (call_audio && !interfaces_.wifiGatewayConfigured())
    {
        return false;
    }
    if (call_audio)
    {
        interfaces_.maintain();
        if (!identity_.isReady() || !interfaces_.hasReadyWifiGateway())
        {
            return false;
        }
    }
    else if (!isReady())
    {
        return false;
    }

    uint8_t app_data[96] = {};
    size_t app_data_len = sizeof(app_data);
    bool app_data_ok = false;
    if (kind == LocalDestinationKind::CallAudio)
    {
        app_data_len = 0;
        app_data_ok = true;
    }
    else if (kind == LocalDestinationKind::Propagation)
    {
        app_data_ok = packPropagationAnnounceAppData(currentTimestampSeconds(),
                                                     true,
                                                     kPropagationTransferLimitKb,
                                                     kPropagationSyncLimitKb,
                                                     effectiveDisplayName(),
                                                     app_data,
                                                     &app_data_len);
    }
    else
    {
        app_data_ok = packPeerAnnounceAppData(effectiveDisplayName(),
                                              false,
                                              0,
                                              app_data,
                                              &app_data_len);
    }
    if (!app_data_ok)
    {
        return false;
    }

    uint8_t combined_pub[reticulum::kCombinedPublicKeySize] = {};
    identity_.combinedPublicKey(combined_pub);

    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    localDestinationHash(kind, destination_hash);

    uint8_t name_hash[reticulum::kNameHashSize] = {};
    if (kind == LocalDestinationKind::CallAudio)
    {
        reticulum::computeNameHash("lxst", "telephony", name_hash);
    }
    else
    {
        reticulum::computeNameHash("lxmf",
                                   (kind == LocalDestinationKind::Propagation) ? "propagation" : "delivery",
                                   name_hash);
    }

    uint8_t random_hash[10] = {};
    fillRandomBytes(random_hash, 5);
    const uint64_t now_s = currentTimestampSeconds();
    random_hash[5] = static_cast<uint8_t>((now_s >> 32) & 0xFFU);
    random_hash[6] = static_cast<uint8_t>((now_s >> 24) & 0xFFU);
    random_hash[7] = static_cast<uint8_t>((now_s >> 16) & 0xFFU);
    random_hash[8] = static_cast<uint8_t>((now_s >> 8) & 0xFFU);
    random_hash[9] = static_cast<uint8_t>(now_s & 0xFFU);

    uint8_t* signed_data = scratch_.announce_tx_signed;
    size_t signed_len = 0;
    memcpy(signed_data + signed_len, destination_hash, reticulum::kTruncatedHashSize);
    signed_len += reticulum::kTruncatedHashSize;
    memcpy(signed_data + signed_len, combined_pub, sizeof(combined_pub));
    signed_len += sizeof(combined_pub);
    memcpy(signed_data + signed_len, name_hash, sizeof(name_hash));
    signed_len += sizeof(name_hash);
    memcpy(signed_data + signed_len, random_hash, sizeof(random_hash));
    signed_len += sizeof(random_hash);
    if (signed_len + app_data_len > reticulum::kReticulumMtu)
    {
        return false;
    }
    memcpy(signed_data + signed_len, app_data, app_data_len);
    signed_len += app_data_len;

    uint8_t signature[reticulum::kSignatureSize] = {};
    if (!identity_.sign(signed_data, signed_len, signature))
    {
        return false;
    }

    uint8_t* announce_payload = scratch_.announce_tx_payload;
    size_t announce_payload_len = 0;
    memcpy(announce_payload + announce_payload_len, combined_pub, sizeof(combined_pub));
    announce_payload_len += sizeof(combined_pub);
    memcpy(announce_payload + announce_payload_len, name_hash, sizeof(name_hash));
    announce_payload_len += sizeof(name_hash);
    memcpy(announce_payload + announce_payload_len, random_hash, sizeof(random_hash));
    announce_payload_len += sizeof(random_hash);
    memcpy(announce_payload + announce_payload_len, signature, sizeof(signature));
    announce_payload_len += sizeof(signature);
    if (announce_payload_len + app_data_len > reticulum::kReticulumMtu)
    {
        return false;
    }
    memcpy(announce_payload + announce_payload_len, app_data, app_data_len);
    announce_payload_len += app_data_len;

    uint8_t* packet = scratch_.announce_tx_packet;
    size_t packet_len = reticulum::kReticulumMtu;
    if (!reticulum::buildHeader1Packet(reticulum::PacketType::Announce,
                                       reticulum::DestinationType::Single,
                                       context,
                                       false,
                                       destination_hash,
                                       announce_payload,
                                       announce_payload_len,
                                       packet,
                                       &packet_len))
    {
        return false;
    }

    char destination_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    formatHashHex(destination_hash,
                  sizeof(destination_hash),
                  destination_hex,
                  sizeof(destination_hex));
    const bool sent = call_audio
                          ? interfaces_.sendPacketWifiOnly(packet, packet_len)
                          : routeAndSendPacket(packet, packet_len, false);
    const auto& tx_result = interfaces_.lastTxResult();
    const char* display_name = effectiveDisplayName();
    Serial.printf("[LXMF][AnnounceTX] kind=%s context=%u dest=%s name=%s app_len=%u packet_len=%u ok=%u complete=%u lora=%u/%u wifi=%u/%u\n",
                  localDestinationKindLabel(kind),
                  static_cast<unsigned>(context),
                  destination_hex,
                  (display_name && display_name[0] != '\0') ? display_name : "<none>",
                  static_cast<unsigned>(app_data_len),
                  static_cast<unsigned>(packet_len),
                  sent ? 1U : 0U,
                  tx_result.reachedRequiredInterfaces() ? 1U : 0U,
                  tx_result.lora_ok ? 1U : 0U,
                  tx_result.lora_required ? 1U : 0U,
                  tx_result.wifi_ok ? 1U : 0U,
                  tx_result.wifi_required ? 1U : 0U);
    if (!sent)
    {
        return false;
    }

    return true;
}

bool LxmfAdapter::lastAnnounceTxReachedRequiredInterfaces(bool sent) const
{
    if (!sent)
    {
        return false;
    }
    return interfaces_.lastTxResult().sent();
}

bool LxmfAdapter::handleAnnouncePacket(const uint8_t* raw_packet, size_t raw_len,
                                       const reticulum::ParsedPacket& packet,
                                       reticulum::interfaces::InterfaceKind ingress_interface,
                                       bool allow_persistence)
{
    const uint32_t now_ms = millis();
    const uint32_t now_s = currentTimestampSeconds();
    runtime::AnnounceIngestOptions options{};
    options.now_ms = now_ms;
    options.now_s = now_s;
    options.path_ttl_ms = kPathTtlMs;
    options.directory_address_refresh_interval_s =
        kDirectoryAddressRefreshIntervalS;
    options.max_paths = kMaxPaths;
    options.max_transport_hops = kMaxTransportHops;
    options.ingress_interface_id = active_ingress_interface_id_;
    options.ingress_interface = ingress_interface;
    options.local_destination_context = this;
    options.resolve_local_destination = &LxmfAdapter::resolveLocalDestinationForAnnounce;

    runtime::AnnounceIngestResult ingest{};
    if (!announce_ingestor_.ingest(raw_packet,
                                   raw_len,
                                   packet,
                                   identity_,
                                   destination_registry_,
                                   path_manager_,
                                   options,
                                   &ingest))
    {
        return false;
    }

    // The ingestor has verified the signature and destination binding before
    // reporting path_rejected. An identical cached path response can refresh
    // application discovery without replacing a route or accepting stale paths.
    const bool cached_discovery = packet.context == static_cast<uint8_t>(reticulum::PacketContext::PathResponse) &&
                                  ingest.status == runtime::AnnounceIngestResult::Status::Ignored &&
                                  ingest.path_decision == runtime::PathAnnounceDecision::RejectReplay &&
                                  ingest.reason && std::strcmp(ingest.reason, "path_rejected") == 0;
    if ((ingest.status == runtime::AnnounceIngestResult::Status::Accepted || cached_discovery) &&
        geocaching_announcement_handler_ && !ingest.local_destination && ingest.announce.name_hash &&
        ingest.announce.public_key && ingest.announce.app_data && ingest.announce.app_data_len <= 96)
    {
        uint8_t name_hash[reticulum::kNameHashSize] = {};
        reticulum::computeNameHash("trailmate", "geocache.directory", name_hash);
        if (hashesEqual(name_hash, ingest.announce.name_hash, sizeof(name_hash)))
        {
            // The verified service announcement authenticates the same identity
            // used by lxmf.delivery. Retain its encryption key without adding a
            // service announcement to the user's contact presentation.
            rememberPeerIdentity(ingest.announce.public_key, nullptr, false);
            uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
            reticulum::computeNameHash("lxmf", "delivery", name_hash);
            reticulum::computeDestinationHash(name_hash, ingest.identity_hash, delivery_hash);
            const GeocachingAnnouncementView view{
                {packet.destination_hash, reticulum::kTruncatedHashSize},
                {delivery_hash, sizeof(delivery_hash)},
                {ingest.announce.public_key, reticulum::kCombinedPublicKeySize},
                {ingest.announce.app_data, ingest.announce.app_data_len}};
            // Borrowed only for this callback; consumers copy bounded metadata.
            geocaching_announcement_handler_(view, geocaching_announcement_context_);
            Serial.printf("[Geocaching][Discovery] verified_announce bytes=%u\n",
                          static_cast<unsigned>(ingest.announce.app_data_len));
        }
    }

    if (ingest.status == runtime::AnnounceIngestResult::Status::Ignored)
    {
        if (ingest.local_destination)
        {
            char destination_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
            formatHashHex(packet.destination_hash,
                          reticulum::kTruncatedHashSize,
                          destination_hex,
                          sizeof(destination_hex));
            Serial.printf("[LXMF][AnnounceRX] ignore reason=local_destination dest=%s kind=%s\n",
                          destination_hex,
                          localDestinationKindLabel(ingest.local_kind));
        }
        return true;
    }
    if (ingest.status != runtime::AnnounceIngestResult::Status::Accepted || !ingest.path) return false;

    PathEntry& path = *ingest.path;
    link_manager_.forEachSession(
        [this, &packet, &path](LinkSession& session)
        {
            if (!session.initiator ||
                session.state != LinkState::Pending ||
                !hashesEqual(session.remote_destination_hash,
                             packet.destination_hash,
                             sizeof(session.remote_destination_hash)))
            {
                return;
            }

            session.expected_hops = path.hops;
            const bool retried = sendLinkRequest(session);
            char dest_hash[12] = {};
            formatHashPrefix(packet.destination_hash, dest_hash, sizeof(dest_hash));
            Serial.printf("[LXMF][LinkTX] retry_after_path dest=%s kind=%u ok=%u\n",
                          dest_hash,
                          static_cast<unsigned>(session.destination),
                          retried ? 1U : 0U);
        });

    if (shouldRebroadcastAnnounce(packet, ingress_interface))
    {
        (void)rebroadcastAnnounce(path, packet);
    }

    char packet_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    char identity_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    char announce_ratchet_id[12] = {};
    formatHashHex(packet.destination_hash,
                  reticulum::kTruncatedHashSize,
                  packet_hash_hex,
                  sizeof(packet_hash_hex));
    formatHashHex(ingest.identity_hash,
                  sizeof(ingest.identity_hash),
                  identity_hash_hex,
                  sizeof(identity_hash_hex));
    formatRatchetIdPrefix(ingest.packet_has_ratchet ? ingest.announce.ratchet : nullptr,
                          announce_ratchet_id,
                          sizeof(announce_ratchet_id));

    rtdir::AnnounceRecord directory_announce{};
    directory_announce.valid = true;
    copyHash(directory_announce.destination_hash,
             packet.destination_hash,
             sizeof(directory_announce.destination_hash));
    copyHash(directory_announce.identity_hash,
             ingest.identity_hash,
             sizeof(directory_announce.identity_hash));
    directory_announce.aspect =
        ingest.delivery_announce
            ? rtdir::AnnounceAspect::LxmfDelivery
            : (ingest.propagation_announce
                   ? rtdir::AnnounceAspect::LxmfPropagation
                   : (ingest.call_audio_announce ? rtdir::AnnounceAspect::CallAudio
                                                 : (ingest.nomad_node_announce
                                                        ? rtdir::AnnounceAspect::NomadNetworkNode
                                                        : rtdir::AnnounceAspect::Unknown)));
    directory_announce.source =
        packet.context == static_cast<uint8_t>(reticulum::PacketContext::PathResponse)
            ? rtdir::EntrySource::PathResponse
            : rtdir::EntrySource::RuntimeRx;
    directory_announce.first_seen_s = now_s;
    directory_announce.last_seen_s = now_s;
    directory_announce.hops = packet.hops;
    directory_announce.path_response =
        packet.context == static_cast<uint8_t>(reticulum::PacketContext::PathResponse);
    directory_announce.local_destination = ingest.local_destination;
    directory_announce.delivery = ingest.delivery_announce;
    directory_announce.propagation = ingest.propagation_announce;
    copyCString(directory_announce.display_name,
                sizeof(directory_announce.display_name),
                ingest.display_name);
    directory_announce.raw_packet = raw_packet;
    directory_announce.raw_packet_len = raw_len;
    directory_announce.app_data = ingest.announce.app_data;
    directory_announce.app_data_len = ingest.announce.app_data_len;
    if (allow_persistence)
    {
        const auto announce_store_status = rtdir::record_announce(directory_announce);
        if (announce_store_status.sd_present && !announce_store_status.saved)
        {
            Serial.printf("[LXMF][AnnounceRX] directory_save failed message=%s detail=%s\n",
                          announce_store_status.message,
                          announce_store_status.detail);
        }
    }

    bool log_announce_detail =
        ingress_interface != reticulum::interfaces::InterfaceKind::WifiGateway ||
        ingest.local_destination;
    if (log_announce_detail &&
        ingress_interface != reticulum::interfaces::InterfaceKind::WifiGateway &&
        !ingest.local_destination &&
        !ingest.contact_announce)
    {
        log_announce_detail = rx_telemetry_.shouldLogLoraAnnounceIgnore(
            millis(),
            kLoraAnnounceIgnoreDetailLogIntervalMs);
    }
    if (log_announce_detail)
    {
        Serial.printf("[LXMF][AnnounceRX] seen dest=%s identity=%s hops=%u context=%u flag=%u app_len=%u ratchet=%u ratchet_id=%s delivery=%u propagation=%u call=%u nomad=%u local=%u kind=%s\n",
                      packet_hash_hex,
                      identity_hash_hex,
                      static_cast<unsigned>(packet.hops),
                      static_cast<unsigned>(packet.context),
                      static_cast<unsigned>(packet.context_flag),
                      static_cast<unsigned>(ingest.announce.app_data_len),
                      ingest.packet_has_ratchet ? 1U : 0U,
                      announce_ratchet_id,
                      ingest.delivery_announce ? 1U : 0U,
                      ingest.propagation_announce ? 1U : 0U,
                      ingest.call_audio_announce ? 1U : 0U,
                      ingest.nomad_node_announce ? 1U : 0U,
                      ingest.local_destination ? 1U : 0U,
                      localDestinationKindLabel(ingest.local_kind));
    }

    if (ingest.propagation_announce && !ingest.local_destination)
    {
        uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
        destinationHashForAspect(ingest.identity_hash, "delivery", delivery_hash);
        DecodedPropagationAnnounce propagation_announce_data{};
        const bool propagation_data_valid =
            decodePropagationAnnounceAppData(ingest.announce.app_data,
                                             ingest.announce.app_data_len,
                                             &propagation_announce_data) &&
            propagation_announce_data.valid;
        if (!propagation_data_valid)
        {
            propagation_announce_data.valid = false;
        }
        const PropagationPeerState* propagation_peer =
            propagation_client_.notePeerAnnounce(packet.destination_hash,
                                                 delivery_hash,
                                                 ingest.identity_hash,
                                                 packet.hops,
                                                 propagation_announce_data,
                                                 ingest.announce.public_key,
                                                 now_s,
                                                 kMaxPropagationPeers);
        if (!propagation_peer)
        {
            return true;
        }
        Serial.printf("[LXMF][Propagation] node_seen dest=%s active=%u hops=%u cost=%u transfer_kb=%lu sync_kb=%lu name=\"%s\"\n",
                      packet_hash_hex,
                      propagation_peer->node_active ? 1U : 0U,
                      static_cast<unsigned>(propagation_peer->hops),
                      static_cast<unsigned>(propagation_peer->stamp_cost),
                      static_cast<unsigned long>(
                          propagation_peer->transfer_limit_kb),
                      static_cast<unsigned long>(
                          propagation_peer->sync_limit_kb),
                      propagation_peer->display_name);
    }

    if (!ingest.contact_announce || ingest.local_destination)
    {
        if (log_announce_detail)
        {
            Serial.printf("[LXMF][AnnounceRX] ignore reason=%s dest=%s\n",
                          ingest.local_destination ? "local_destination" : "not_contact_announce",
                          packet_hash_hex);
        }
        return true;
    }

    if (!ingest.learned_peer)
    {
        return true;
    }
    PeerInfo& peer = *ingest.learned_peer;
    if (allow_persistence && ingest.should_store_address)
    {
        if (!recordPeerInDirectory(
                peer,
                meshPeerSourceFromDirectorySource(directory_announce.source),
                false,
                false))
        {
            Serial.printf("[LXMF][AnnounceRX] address_save failed status=mesh_peer_directory\n");
        }
    }

    if (ingress_interface == reticulum::interfaces::InterfaceKind::WifiGateway)
    {
        peer_directory_service_.queuePeerUpdate(peer);
    }
    else
    {
        if (allow_persistence)
        {
            publishPeerUpdate(peer);
        }
        else
        {
            peer_directory_service_.queuePeerUpdate(peer);
        }
    }
    char peer_ratchet_id[12] = {};
    formatRatchetIdPrefix(peerHasUsableRatchet(peer) ? peer.ratchet_pub : nullptr,
                          peer_ratchet_id,
                          sizeof(peer_ratchet_id));
    Serial.printf("[LXMF][AnnounceRX] learned peer=%08lX dest=%s identity=%s name=%s ratchet=%u ratchet_id=%s peers=%u\n",
                  static_cast<unsigned long>(peer.node_id),
                  packet_hash_hex,
                  identity_hash_hex,
                  peer.display_name[0] != '\0' ? peer.display_name : "<unnamed>",
                  peerHasUsableRatchet(peer) ? 1U : 0U,
                  peer_ratchet_id,
                  static_cast<unsigned>(destination_registry_.size()));
    return true;
}

bool LxmfAdapter::handleDataPacket(const uint8_t* raw_packet, size_t raw_len,
                                   const reticulum::ParsedPacket& packet)
{
    if (!raw_packet || raw_len == 0 || !packet.payload || !packet.destination_hash)
    {
        return false;
    }

    if (packet.destination_type == reticulum::DestinationType::Group)
    {
        char dest_hash[12] = {};
        formatHashPrefix(packet.destination_hash, dest_hash, sizeof(dest_hash));
        Serial.printf("[LXMF][GroupRX] packet raw_len=%u payload_len=%u dest=%s hops=%u\n",
                      static_cast<unsigned>(raw_len),
                      static_cast<unsigned>(packet.payload_len),
                      dest_hash,
                      static_cast<unsigned>(packet.hops));
        const ReticulumGroupDestinationConfig* group =
            findConfiguredGroupDestination(packet.destination_hash);
        if (!group || packet.payload_len == 0)
        {
            Serial.printf("[LXMF][GroupRX] drop reason=%s dest=%s\n",
                          group ? "empty_payload" : "unconfigured_group",
                          dest_hash);
            return false;
        }
        Serial.printf("[LXMF][GroupRX] matched group=%s dest=%s\n",
                      group->name[0] != '\0' ? group->name : "<unnamed>",
                      dest_hash);
        const ReticulumPeerIdentity conversation_identity =
            makeReticulumDestinationIdentity(packet.destination_hash);
        return acceptVerifiedEnvelopeForDestination(packet.destination_hash,
                                                    conversation_identity,
                                                    true,
                                                    false,
                                                    packet.payload,
                                                    packet.payload_len,
                                                    raw_packet,
                                                    raw_len);
    }

    LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
    if (!isLocalDestinationHash(packet.destination_hash, &local_kind))
    {
        char dest_hash[12] = {};
        formatHashPrefix(packet.destination_hash, dest_hash, sizeof(dest_hash));
        Serial.printf("[LXMF][DirectRX] drop reason=not_local dest=%s raw_len=%u payload_len=%u\n",
                      dest_hash,
                      static_cast<unsigned>(raw_len),
                      static_cast<unsigned>(packet.payload_len));
        return false;
    }
    if (packet.destination_type != reticulum::DestinationType::Single ||
        packet.payload_len <= (reticulum::kEncryptionPublicKeySize + reticulum::kTokenOverhead))
    {
        char dest_hash[12] = {};
        formatHashPrefix(packet.destination_hash, dest_hash, sizeof(dest_hash));
        Serial.printf("[LXMF][DirectRX] drop reason=invalid_single_packet kind=%s dest=%s type=%u payload_len=%u\n",
                      localDestinationKindLabel(local_kind),
                      dest_hash,
                      static_cast<unsigned>(packet.destination_type),
                      static_cast<unsigned>(packet.payload_len));
        return false;
    }

    char dest_hash[12] = {};
    formatHashPrefix(packet.destination_hash, dest_hash, sizeof(dest_hash));
    Serial.printf("[LXMF][DirectRX] packet kind=%s dest=%s raw_len=%u payload_len=%u hops=%u\n",
                  localDestinationKindLabel(local_kind),
                  dest_hash,
                  static_cast<unsigned>(raw_len),
                  static_cast<unsigned>(packet.payload_len),
                  static_cast<unsigned>(packet.hops));

    const uint8_t* peer_ephemeral_pub = packet.payload;
    const uint8_t* token = packet.payload + reticulum::kEncryptionPublicKeySize;
    const size_t token_len = packet.payload_len - reticulum::kEncryptionPublicKeySize;

    uint8_t shared_secret[LxmfIdentity::kEncPubKeySize] = {};
    if (!identity_.deriveSharedSecret(peer_ephemeral_pub, shared_secret))
    {
        return false;
    }

    uint8_t derived_key[reticulum::kDerivedTokenKeySize] = {};
    if (!reticulum::hkdfSha256(shared_secret, sizeof(shared_secret),
                               identity_.identityHash(), reticulum::kTruncatedHashSize,
                               nullptr, 0,
                               derived_key, sizeof(derived_key)))
    {
        return false;
    }

    uint8_t plaintext[kMaxTokenPlaintextLen] = {};
    size_t plaintext_len = sizeof(plaintext);
    if (!reticulum::tokenDecrypt(derived_key, token, token_len, plaintext, &plaintext_len))
    {
        Serial.printf("[LXMF][DirectRX] drop reason=decrypt_failed kind=%s dest=%s token_len=%u\n",
                      localDestinationKindLabel(local_kind),
                      dest_hash,
                      static_cast<unsigned>(token_len));
        return false;
    }

    if (local_kind == LocalDestinationKind::Delivery && plaintext_len == 0)
    {
        const bool proof_sent = sendProofForPacket(raw_packet, raw_len);
        Serial.printf("[LXMF][DirectRX] ping kind=%s dest=%s proof=%u\n",
                      localDestinationKindLabel(local_kind),
                      dest_hash,
                      proof_sent ? 1U : 0U);
        return true;
    }

    bool handled = false;
    if (local_kind == LocalDestinationKind::Propagation)
    {
        LinkSession propagation_session{};
        propagation_session.destination = LocalDestinationKind::Propagation;
        propagation_session.state = LinkState::Active;
        handled = handlePropagationBatch(propagation_session, plaintext, plaintext_len);
    }
    else
    {
        handled = acceptVerifiedEnvelope(plaintext, plaintext_len, raw_packet, raw_len);
    }

    if (!handled)
    {
        Serial.printf("[LXMF][DirectRX] drop reason=handler_rejected kind=%s dest=%s plaintext_len=%u\n",
                      localDestinationKindLabel(local_kind),
                      dest_hash,
                      static_cast<unsigned>(plaintext_len));
        return false;
    }

    const bool proof_sent = sendProofForPacket(raw_packet, raw_len);
    Serial.printf("[LXMF][DirectRX] proof kind=%s dest=%s ok=%u\n",
                  localDestinationKindLabel(local_kind),
                  dest_hash,
                  proof_sent ? 1U : 0U);
    return true;
}

bool LxmfAdapter::handleProofPacket(
    const uint8_t* raw_packet, size_t raw_len,
    const reticulum::ParsedPacket& packet,
    reticulum::interfaces::InterfaceKind ingress_interface)
{
    if (!raw_packet || !packet.destination_hash || packet.packet_type != reticulum::PacketType::Proof)
    {
        return false;
    }

    if (packet.destination_type == reticulum::DestinationType::Link)
    {
        if (handleLocalLinkPacket(raw_packet, raw_len, packet, ingress_interface))
        {
            return true;
        }
        if (!network_page_client_.empty() &&
            packet.context == static_cast<uint8_t>(reticulum::PacketContext::LrProof))
        {
            char link_hash[12] = {};
            char transport_hash[12] = {};
            formatHashPrefix(packet.destination_hash, link_hash, sizeof(link_hash));
            formatHashPrefix(packet.transport_id, transport_hash, sizeof(transport_hash));
            Serial.printf("[LXMF][LinkRX] proof_unmatched reason=no_local_link link=%s transport=%s raw_len=%u payload_len=%u hops=%u pending_nomad=%u\n",
                          link_hash,
                          transport_hash,
                          static_cast<unsigned>(raw_len),
                          static_cast<unsigned>(packet.payload_len),
                          static_cast<unsigned>(packet.hops),
                          static_cast<unsigned>(network_page_client_.size()));
        }
    }

    if (packet.context == static_cast<uint8_t>(reticulum::PacketContext::LrProof) ||
        packet.destination_type == reticulum::DestinationType::Link)
    {
        return maybeForwardLinkPacket(raw_packet, raw_len, packet);
    }

    const auto proof_signature_for_hash =
        [&packet](const uint8_t expected_hash[reticulum::kFullHashSize])
        -> const uint8_t*
    {
        if (packet.payload_len == reticulum::kSignatureSize)
        {
            return packet.payload;
        }
        if (packet.payload_len ==
                (reticulum::kFullHashSize + reticulum::kSignatureSize) &&
            packet.payload && expected_hash &&
            hashesEqual(packet.payload,
                        expected_hash,
                        reticulum::kFullHashSize))
        {
            return packet.payload + reticulum::kFullHashSize;
        }
        return nullptr;
    };

    if (runtime::DeliveryAttemptReceipt* pending =
            delivery_attempt_ledger_.findReceiptByProofHash(
                packet.destination_hash))
    {
        const uint8_t* signature =
            proof_signature_for_hash(pending->packet_hash);
        const bool valid =
            signature &&
            LxmfIdentity::verify(pending->peer_sig_pub,
                                 signature,
                                 pending->packet_hash,
                                 sizeof(pending->packet_hash));
        char destination_hash[12] = {};
        formatHashPrefix(pending->destination_hash,
                         destination_hash,
                         sizeof(destination_hash));
        if (!valid)
        {
            Serial.printf("[LXMF][DirectTX] proof_reject reason=invalid_signature msg=%lu dest=%s payload_len=%u\n",
                          static_cast<unsigned long>(pending->message_id),
                          destination_hash,
                          static_cast<unsigned>(packet.payload_len));
            return false;
        }

        const MessageId message_id = pending->message_id;
        const uint32_t elapsed_ms = millis() - pending->created_ms;
        delivery_attempt_ledger_.removeReceiptByProofHash(
            packet.destination_hash);
        Serial.printf("[LXMF][DirectTX] proof_ok msg=%lu representation=opportunistic elapsed_ms=%lu hops=%u\n",
                      static_cast<unsigned long>(message_id),
                      static_cast<unsigned long>(elapsed_ms),
                      static_cast<unsigned>(packet.hops));
        delivery_notifier_.delivered(message_id);
        return true;
    }

    if (runtime::PendingPingReceipt* pending =
            path_manager_.findPendingPingReceipt(packet.destination_hash))
    {
        const uint8_t* signature =
            proof_signature_for_hash(pending->packet_hash);

        const bool valid =
            signature &&
            LxmfIdentity::verify(pending->peer_sig_pub,
                                 signature,
                                 pending->packet_hash,
                                 sizeof(pending->packet_hash));
        char destination_hash[12] = {};
        formatHashPrefix(pending->destination_hash,
                         destination_hash,
                         sizeof(destination_hash));
        if (!valid)
        {
            Serial.printf("[LXMF][PingRX] reject reason=invalid_proof dest=%s payload_len=%u hops=%u\n",
                          destination_hash,
                          static_cast<unsigned>(packet.payload_len),
                          static_cast<unsigned>(packet.hops));
            return false;
        }

        const uint32_t rtt_ms = millis() - pending->created_ms;
        Serial.printf("[LXMF][PingRX] delivered dest=%s rtt_ms=%lu hops=%u\n",
                      destination_hash,
                      static_cast<unsigned long>(rtt_ms),
                      static_cast<unsigned>(packet.hops));
        sys::EventBus::publish(
            new sys::ReticulumPingResultEvent(
                pending->destination_hash,
                sys::ReticulumPingResult::Delivered,
                rtt_ms,
                packet.hops),
            100);
        path_manager_.removePendingPingReceipt(packet.destination_hash);
        return true;
    }

    ReverseEntry* reverse =
        path_manager_.findReversePath(packet.destination_hash);
    if (!reverse)
    {
        return false;
    }
    if (packet.hops != reverse->expected_hops)
    {
        return false;
    }

    std::memset(scratch_.forward_packet, 0, sizeof(scratch_.forward_packet));
    size_t forward_len = sizeof(scratch_.forward_packet);
    if (!reticulum::buildHeader1Packet(packet.packet_type,
                                       packet.destination_type,
                                       static_cast<reticulum::PacketContext>(packet.context),
                                       packet.context_flag != 0,
                                       packet.destination_hash,
                                       packet.payload,
                                       packet.payload_len,
                                       scratch_.forward_packet,
                                       &forward_len,
                                       packet.hops,
                                       reticulum::TransportType::Broadcast))
    {
        return false;
    }

    reverse->created_ms = 0;
    return interfaces_.sendPacketOn(reverse->interface_id,
                                    scratch_.forward_packet,
                                    forward_len);
}

bool LxmfAdapter::handleLinkRequestPacket(
    const uint8_t* raw_packet, size_t raw_len,
    const reticulum::ParsedPacket& packet,
    reticulum::interfaces::InterfaceKind ingress_interface)
{
    if (!raw_packet || raw_len == 0 || !packet.destination_hash ||
        packet.packet_type != reticulum::PacketType::LinkRequest)
    {
        return false;
    }

    LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
    const bool local_destination =
        isLocalDestinationHash(packet.destination_hash, &local_kind);
    const bool wifi_final_delivery =
        local_destination &&
        ingress_interface == reticulum::interfaces::InterfaceKind::WifiGateway;
    if (packet.transport_id && !wifi_final_delivery &&
        !hashesEqual(packet.transport_id,
                     identity_.identityHash(),
                     reticulum::kTruncatedHashSize))
    {
        return false;
    }

    if (local_destination)
    {
        if (local_kind == LocalDestinationKind::CallAudio &&
            ingress_interface == reticulum::interfaces::InterfaceKind::LoRa)
        {
            char call_hash[12] = {};
            formatHashPrefix(packet.destination_hash, call_hash, sizeof(call_hash));
            Serial.printf("[LXMF][CallRX] reject_bearer dest=%s reason=lora_not_supported\n",
                          call_hash);
            return true;
        }

        if (!packet.payload ||
            (packet.payload_len != kLinkRequestBaseLen &&
             packet.payload_len != (kLinkRequestBaseLen + kLinkSignallingLen)))
        {
            return false;
        }

        const size_t signalling_len =
            (packet.payload_len > kLinkRequestBaseLen) ? (packet.payload_len - kLinkRequestBaseLen) : 0;
        if (signalling_len != 0 &&
            linkModeFromSignalling(packet.payload + kLinkRequestBaseLen, signalling_len) != kLinkModeAes256Cbc)
        {
            return false;
        }

        uint8_t link_id[reticulum::kTruncatedHashSize] = {};
        if (!computeLinkIdFromLinkRequest(raw_packet, raw_len, packet, link_id))
        {
            return false;
        }

        auto call_phase = ::platform::ui::reticulum_call::RealtimePhase::Idle;
        uint8_t current_call_link_id[reticulum::kTruncatedHashSize] = {};
        bool current_call_link = false;
        bool reject_busy_call = false;
        if (local_kind == LocalDestinationKind::CallAudio)
        {
            call_phase = ::platform::ui::reticulum_call::realtime_phase();
            current_call_link =
                ::platform::ui::reticulum_call::current_link_id(current_call_link_id) &&
                hashesEqual(current_call_link_id, link_id, sizeof(link_id));
            reject_busy_call =
                call_phase != ::platform::ui::reticulum_call::RealtimePhase::Idle &&
                !current_call_link;
        }

        LinkSession* session = findLinkSession(link_id);
        if (!session)
        {
            const uint32_t now_ms = millis();
            const uint16_t link_mtu =
                (signalling_len != 0)
                    ? mtuFromLinkSignalling(packet.payload + kLinkRequestBaseLen,
                                            signalling_len)
                    : reticulum::kReticulumMtu;
            runtime::LinkSessionSpec session_spec{};
            session_spec.now_ms = now_ms;
            session_spec.link_id = link_id;
            session_spec.local_sig_pub = identity_.signingPublicKey();
            session_spec.peer_enc_pub = packet.payload;
            session_spec.peer_link_sig_pub =
                packet.payload + LxmfIdentity::kEncPubKeySize;
            session_spec.interface_id = active_ingress_interface_id_;
            session_spec.destination = local_kind;
            session_spec.state = LinkState::Handshake;
            session_spec.initiator = false;
            session_spec.mtu = link_mtu;
            session_spec.mdu = linkMduForMtu(link_mtu);
            session = link_manager_.openSessionPreserving(
                kMaxLinkSessions,
                session_spec,
                reject_busy_call ? current_call_link_id : nullptr);
            if (!session)
            {
                return false;
            }
            Curve25519::dh1(session->local_enc_pub, session->local_enc_priv);
            if (local_kind == LocalDestinationKind::CallAudio)
            {
                lxst_telephony_client_.beginSidebandCalleeSession(
                    *session,
                    call_profile::kEmbeddedLxstProfile,
                    now_ms);
            }

            if (!deriveLinkKey(*session))
            {
                link_manager_.discardSession(*session);
                return false;
            }

            if (reject_busy_call)
            {
                const bool proof_sent = sendLinkHandshakeProof(*session, true);
                const bool busy_sent =
                    proof_sent &&
                    (session->call_wire_profile !=
                         ReticulumCallWireProfile::SidebandLxst ||
                     sendLxstSignal(*session,
                                    reticulum::lxst::kStatusBusy,
                                    true));
                const bool close_sent =
                    busy_sent &&
                    sendLinkPacket(*session,
                                   reticulum::PacketType::Data,
                                   reticulum::PacketContext::LinkClose,
                                   session->link_id,
                                   sizeof(session->link_id),
                                   true,
                                   true);
                char link_hash[12] = {};
                formatHashPrefix(session->link_id, link_hash, sizeof(link_hash));
                Serial.printf("[LXMF][CallRX] reject_busy link=%s wire=%u phase=%u proof=%u busy=%u close=%u iface=%u\n",
                              link_hash,
                              static_cast<unsigned>(session->call_wire_profile),
                              static_cast<unsigned>(call_phase),
                              proof_sent ? 1U : 0U,
                              busy_sent ? 1U : 0U,
                              close_sent ? 1U : 0U,
                              static_cast<unsigned>(ingress_interface));
                link_manager_.discardSession(*session);
                return proof_sent && busy_sent && close_sent;
            }

            if (local_kind == LocalDestinationKind::CallAudio)
            {
                char link_hash[12] = {};
                formatHashPrefix(session->link_id, link_hash, sizeof(link_hash));
                ::platform::ui::reticulum_call::Peer call_peer{};
                copyHash(call_peer.link_id,
                         session->link_id,
                         sizeof(call_peer.link_id));
                call_peer.incoming = true;
                call_peer.wire_profile =
                    call_profile::runtimeWireProfile(
                        session->call_wire_profile);
                call_peer.codec2_mode = call_profile::runtimeCodec2Mode(
                    session->call_wire_profile,
                    lxst_telephony_client_.profile(*session));
                const bool ui_started =
                    ::platform::ui::reticulum_call::begin_incoming_identifying(
                        call_peer);
                lxst_telephony_client_.markRuntimeStarted(*session,
                                                          ui_started);
                Serial.printf("[LXMF][CallRX] link_admitted link=%s wire=sideband_lxst ui=%u await_identify=1 iface=%u\n",
                              link_hash,
                              ui_started ? 1U : 0U,
                              static_cast<unsigned>(ingress_interface));
            }
        }
        else
        {
            link_manager_.touchInbound(*session, millis());
        }

        return sendLinkHandshakeProof(*session);
    }

    // Trail Mate is a Reticulum endpoint client. It should not become a
    // transient transport router on LoRa and spend airtime forwarding links.
    return false;
}

bool LxmfAdapter::handlePathRequestPacket(const reticulum::ParsedPacket& packet)
{
    if (!packet.valid ||
        packet.packet_type != reticulum::PacketType::Data ||
        packet.destination_type != reticulum::DestinationType::Plain ||
        !packet.destination_hash ||
        !packet.payload ||
        packet.payload_len <= reticulum::kTruncatedHashSize)
    {
        return false;
    }

    uint8_t control_hash[reticulum::kTruncatedHashSize] = {};
    pathRequestDestinationHash(control_hash);
    if (!hashesEqual(packet.destination_hash, control_hash, sizeof(control_hash)))
    {
        return false;
    }

    const uint8_t* requested_hash = packet.payload;
    const uint8_t* tag = nullptr;
    size_t tag_len = 0;

    if (packet.payload_len > (reticulum::kTruncatedHashSize * 2))
    {
        tag = packet.payload + (reticulum::kTruncatedHashSize * 2);
        tag_len = packet.payload_len - (reticulum::kTruncatedHashSize * 2);
    }
    else
    {
        tag = packet.payload + reticulum::kTruncatedHashSize;
        tag_len = packet.payload_len - reticulum::kTruncatedHashSize;
    }

    if (tag_len > kPathRequestTagSize)
    {
        tag_len = kPathRequestTagSize;
    }
    if (tag_len == 0)
    {
        return true;
    }

    LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
    if (isLocalDestinationHash(requested_hash, &local_kind))
    {
        (void)tag;
        char requested_prefix[12] = {};
        formatHashPrefix(requested_hash,
                         requested_prefix,
                         sizeof(requested_prefix));
        if (local_kind == LocalDestinationKind::CallAudio &&
            isLoRaInterfaceId(active_ingress_interface_id_))
        {
            Serial.printf("[LXMF][PathRX] skip_response dest=%s kind=call_audio reason=lora_not_supported\n",
                          requested_prefix);
            return true;
        }
        const bool sent = sendAnnounce(local_kind,
                                       reticulum::PacketContext::PathResponse);
        Serial.printf("[LXMF][PathRX] response dest=%s kind=%u sent=%u\n",
                      requested_prefix,
                      static_cast<unsigned>(local_kind),
                      sent ? 1U : 0U);
        return sent;
    }

    return true;
}

bool LxmfAdapter::handleCacheRequestPacket(const reticulum::ParsedPacket& packet)
{
    if (!packet.valid ||
        packet.packet_type != reticulum::PacketType::Data ||
        packet.context != static_cast<uint8_t>(reticulum::PacketContext::CacheRequest) ||
        !packet.payload ||
        packet.payload_len != reticulum::kFullHashSize)
    {
        return false;
    }

    return sendCachedPacketReplay(packet.payload);
}

bool LxmfAdapter::handleLocalLinkPacket(
    const uint8_t* raw_packet, size_t raw_len,
    const reticulum::ParsedPacket& packet,
    reticulum::interfaces::InterfaceKind ingress_interface)
{
    if (!packet.destination_hash ||
        packet.destination_type != reticulum::DestinationType::Link)
    {
        return false;
    }

    LinkSession* session = findLinkSession(packet.destination_hash);
    if (!session)
    {
        return false;
    }
    if (session->destination == LocalDestinationKind::CallAudio &&
        ingress_interface == reticulum::interfaces::InterfaceKind::LoRa)
    {
        return false;
    }
    if (session->interface_id != reticulum::interfaces::kInvalidInterfaceId &&
        active_ingress_interface_id_ != reticulum::interfaces::kInvalidInterfaceId &&
        session->interface_id != active_ingress_interface_id_)
    {
        return false;
    }
    if (session->interface_id == reticulum::interfaces::kInvalidInterfaceId)
    {
        session->interface_id = active_ingress_interface_id_;
    }

    link_manager_.touchInbound(*session, millis());
    if (packet.packet_type == reticulum::PacketType::Proof)
    {
        return handleLinkProofPacket(*session, raw_packet, raw_len, packet);
    }
    if (packet.packet_type == reticulum::PacketType::Data)
    {
        return handleLinkDataPacket(*session, raw_packet, raw_len, packet);
    }
    return false;
}

bool LxmfAdapter::handleLinkDataPacket(LinkSession& session,
                                       const uint8_t* raw_packet, size_t raw_len,
                                       const reticulum::ParsedPacket& packet)
{
    if (!packet.payload)
    {
        return false;
    }

    runtime::ResourcePayloadBuffer plaintext;
    const uint8_t context = packet.context;
    const bool raw_payload = packetContextUsesRawLinkPayload(context);
    if (!raw_payload)
    {
        if (!decryptLinkPayload(session, packet.payload, packet.payload_len, &plaintext))
        {
            if (session.destination == LocalDestinationKind::CallAudio)
            {
                char link_hash[12] = {};
                formatHashPrefix(session.link_id, link_hash, sizeof(link_hash));
                Serial.printf("[LXMF][CallRX] data_drop link=%s context=%u reason=decrypt payload=%u\n",
                              link_hash,
                              static_cast<unsigned>(context),
                              static_cast<unsigned>(packet.payload_len));
            }
            return false;
        }
    }

    const uint8_t* payload_ptr = raw_payload ? packet.payload : plaintext.data();
    const size_t payload_len = raw_payload ? packet.payload_len : plaintext.size();
    bool handled = false;
    bool should_prove = false;

    if (context == static_cast<uint8_t>(reticulum::PacketContext::None))
    {
        if (session.state == LinkState::Active)
        {
            if (session.destination == LocalDestinationKind::Propagation)
            {
                handled = rtnet::active().propagation.service_enabled &&
                          handlePropagationBatch(session,
                                                 payload_ptr,
                                                 payload_len);
            }
            else if (session.destination == LocalDestinationKind::CallAudio)
            {
                if (session.call_wire_profile ==
                    ReticulumCallWireProfile::SidebandLxst)
                {
                    handled = handleLxstPacket(session, payload_ptr, payload_len);
                }
#if TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT
                else if (session.call_wire_profile ==
                         ReticulumCallWireProfile::MeshChatCallAudio)
                {
                    handled = ::platform::ui::reticulum_call::enqueue_inbound_audio(
                        session.link_id,
                        payload_ptr,
                        payload_len);
                }
#endif
            }
            else if (session.destination == LocalDestinationKind::Delivery)
            {
                handled = acceptVerifiedEnvelope(payload_ptr,
                                                 payload_len,
                                                 raw_packet,
                                                 raw_len,
                                                 nullptr,
                                                 nullptr,
                                                 &session);
            }
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::LinkIdentify))
    {
        bool identify_len_ok = false;
        bool identify_signature_ok = false;
        bool identify_peer_ok = false;
        if (payload_len == reticulum::kCombinedPublicKeySize + reticulum::kSignatureSize)
        {
            identify_len_ok = true;
            const uint8_t* combined_pub = payload_ptr;
            const uint8_t* signature = payload_ptr + reticulum::kCombinedPublicKeySize;
            std::array<uint8_t, reticulum::kTruncatedHashSize + reticulum::kCombinedPublicKeySize> signed_data{};
            memcpy(signed_data.data(), session.link_id, reticulum::kTruncatedHashSize);
            memcpy(signed_data.data() + reticulum::kTruncatedHashSize,
                   combined_pub,
                   reticulum::kCombinedPublicKeySize);
            const uint8_t* sign_pub = combined_pub + LxmfIdentity::kEncPubKeySize;
            if (LxmfIdentity::verify(sign_pub, signature, signed_data.data(), signed_data.size()))
            {
                identify_signature_ok = true;
                PeerInfo* peer = rememberPeerIdentity(combined_pub);
                if (peer)
                {
                    identify_peer_ok = true;
                    if (session.destination != LocalDestinationKind::CallAudio &&
                        session.destination != LocalDestinationKind::NomadPage)
                    {
                        copyHash(session.remote_destination_hash,
                                 peer->destination_hash,
                                 sizeof(session.remote_destination_hash));
                    }
                    copyHash(session.remote_identity_hash,
                             peer->identity_hash,
                             sizeof(session.remote_identity_hash));
                    memcpy(session.peer_identity_sig_pub,
                           peer->sig_pub,
                           sizeof(session.peer_identity_sig_pub));
                    session.remote_identity_known = true;
                    if (session.destination == LocalDestinationKind::CallAudio)
                    {
                        updateCallRuntimePeer(session, peer);
                        if (!session.initiator &&
                            session.call_wire_profile ==
                                ReticulumCallWireProfile::SidebandLxst)
                        {
                            handled = dispatchLxstCallEvent(
                                session,
                                {reticulum::lxst::call::EventType::
                                     RemoteIdentified});
                        }
                    }
                }
                handled = true;
            }
        }
        if (session.destination == LocalDestinationKind::CallAudio)
        {
            char link_hash[12] = {};
            formatHashPrefix(session.link_id, link_hash, sizeof(link_hash));
            if (handled)
            {
                Serial.printf("[LXMF][CallRX] identify_ok link=%s peer=%u phase=%s\n",
                              link_hash,
                              identify_peer_ok ? 1U : 0U,
                              reticulum::lxst::call::phaseName(
                                  lxst_telephony_client_.phase(session)));
            }
            else
            {
                const char* reason = !identify_len_ok
                                         ? "len"
                                         : (!identify_signature_ok ? "signature" : "peer");
                Serial.printf("[LXMF][CallRX] identify_drop link=%s reason=%s payload=%u\n",
                              link_hash,
                              reason,
                              static_cast<unsigned>(payload_len));
            }
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::Request))
    {
        DecodedLinkRequest request{};
        if (decodeLinkRequestPayload(payload_ptr, payload_len, &request))
        {
            uint8_t request_id[reticulum::kTruncatedHashSize] = {};
            reticulum::computeTruncatedPacketHash(raw_packet, raw_len, request_id);

            if (session.destination == LocalDestinationKind::Propagation)
            {
                handled = rtnet::active().propagation.service_enabled &&
                          handlePropagationRequest(session,
                                                   request,
                                                   request_id,
                                                   sizeof(request_id));
            }
            else
            {
                handled = sendLinkResponse(session,
                                           request_id,
                                           sizeof(request_id),
                                           nullptr,
                                           0,
                                           true);
            }
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::Response))
    {
        DecodedLinkResponse response{};
        if (decodeLinkResponsePayload(payload_ptr, payload_len, &response))
        {
            handled = link_manager_.markPendingResponseReady(
                session,
                response.request_id.data(),
                response.request_id.size(),
                response.packed_data.data(),
                response.packed_data.size(),
                response.data_is_nil);
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::LrRtt))
    {
        double rtt_value = 0.0;
        if (!session.initiator &&
            unpackFloat64(payload_ptr, payload_len, &rtt_value))
        {
            const float rtt_s = static_cast<float>(rtt_value);
            link_manager_.markSessionValidatedActive(
                session,
                rtt_s,
                keepaliveIntervalForRtt(rtt_s));
            if (session.destination == LocalDestinationKind::CallAudio)
            {
                ::platform::ui::reticulum_call::mark_link_active(
                    session.link_id);
                if (session.call_wire_profile ==
                    ReticulumCallWireProfile::SidebandLxst)
                {
                    handled = dispatchLxstCallEvent(
                        session,
                        {reticulum::lxst::call::EventType::LinkActive});
                }
                else
                {
                    (void)sendLinkIdentify(session);
                }
            }
            else
            {
                handled = true;
            }
        }
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::LinkClose))
    {
        if (payload_len == reticulum::kTruncatedHashSize &&
            hashesEqual(payload_ptr, session.link_id, sizeof(session.link_id)))
        {
            if (session.destination == LocalDestinationKind::CallAudio)
            {
                char link_hash[12] = {};
                formatHashPrefix(session.link_id, link_hash, sizeof(link_hash));
                Serial.printf("[LXMF][CallRX] link_close link=%s reason=remote\n",
                              link_hash);
            }
            closeLinkSession(session, LinkCloseReason::RemoteClose);
            handled = true;
        }
        else if (session.destination == LocalDestinationKind::CallAudio)
        {
            char link_hash[12] = {};
            formatHashPrefix(session.link_id, link_hash, sizeof(link_hash));
            Serial.printf("[LXMF][CallRX] link_close_drop link=%s payload=%u\n",
                          link_hash,
                          static_cast<unsigned>(payload_len));
        }
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::Keepalive))
    {
        if (payload_len == 1 && payload_ptr[0] == 0xFF)
        {
            handled = sendLinkKeepaliveAck(session);
        }
        else
        {
            handled = (payload_len == 1 && payload_ptr[0] == 0xFE);
            (void)(handled && link_manager_.reactivateSessionIfStale(session));
        }
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceAdv))
    {
        handled = handleLinkResourceAdvertisement(session, payload_ptr, payload_len);
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceReq))
    {
        handled = handleLinkResourceRequest(session, payload_ptr, payload_len);
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceHmu))
    {
        handled = handleLinkResourceHashmapUpdate(session, payload_ptr, payload_len);
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceIcl))
    {
        if (payload_len == reticulum::kFullHashSize)
        {
            (void)link_manager_.eraseIncomingResource(session, payload_ptr);
            handled = true;
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceRcl))
    {
        if (payload_len == reticulum::kFullHashSize)
        {
            (void)link_manager_.eraseOutgoingResource(session, payload_ptr);
            handled = true;
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::Resource))
    {
        handled = handleLinkResourcePart(session, packet);
    }

    if (handled && should_prove)
    {
        (void)sendLinkPacketProof(session, raw_packet, raw_len);
    }
    return handled;
}

bool LxmfAdapter::handleLinkProofPacket(LinkSession& session,
                                        const uint8_t* raw_packet, size_t raw_len,
                                        const reticulum::ParsedPacket& packet)
{
    (void)raw_packet;
    (void)raw_len;

    if (!packet.payload)
    {
        return false;
    }

    if (packet.context == static_cast<uint8_t>(reticulum::PacketContext::LrProof))
    {
        if (!session.initiator || packet.payload_len < (reticulum::kSignatureSize + LxmfIdentity::kEncPubKeySize))
        {
            logNomadLinkProofEvent(session,
                                   "drop",
                                   "bad_state_or_short",
                                   session.initiator ? 1U : 0U,
                                   static_cast<uint32_t>(packet.payload_len));
            LXMF_LINK_PROOF_LOG("lrproof drop reason=bad_state_or_short kind=%s initiator=%u payload=%u\n",
                                localDestinationKindLabel(session.destination),
                                session.initiator ? 1U : 0U,
                                static_cast<unsigned>(packet.payload_len));
            return false;
        }

        if (session.expected_hops != 0 && packet.hops != session.expected_hops)
        {
            logNomadLinkProofEvent(session,
                                   "drop",
                                   "hop_mismatch",
                                   session.expected_hops,
                                   packet.hops);
            char dest_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
            formatHashHex(session.remote_destination_hash,
                          sizeof(session.remote_destination_hash),
                          dest_hash_hex,
                          sizeof(dest_hash_hex));
            LXMF_LINK_PROOF_LOG("lrproof drop reason=hop_mismatch kind=%s dest=%s expected=%u got=%u\n",
                                localDestinationKindLabel(session.destination),
                                dest_hash_hex,
                                static_cast<unsigned>(session.expected_hops),
                                static_cast<unsigned>(packet.hops));
            return false;
        }

        const uint8_t* signature = packet.payload;
        const uint8_t* peer_enc_pub = packet.payload + reticulum::kSignatureSize;
        const uint8_t* signalling = (packet.payload_len >= (reticulum::kSignatureSize + LxmfIdentity::kEncPubKeySize + kLinkSignallingLen))
                                        ? (packet.payload + reticulum::kSignatureSize + LxmfIdentity::kEncPubKeySize)
                                        : nullptr;
        const size_t signalling_len = signalling ? kLinkSignallingLen : 0;

        const PeerInfo* peer =
            session.destination == LocalDestinationKind::CallAudio
                ? destination_registry_.findByIdentityHash(session.remote_identity_hash)
                : destination_registry_.findByDestinationHash(
                      session.remote_destination_hash);
        const uint8_t* peer_sig_pub = peer ? peer->sig_pub : nullptr;
        uint8_t announce_identity_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t announce_sig_pub[LxmfIdentity::kSigPubKeySize] = {};
        bool announce_identity_known = false;
        if (!peer_sig_pub &&
            !isZeroBytes(session.peer_identity_sig_pub,
                         sizeof(session.peer_identity_sig_pub)))
        {
            peer_sig_pub = session.peer_identity_sig_pub;
        }
        if (!peer_sig_pub && session.destination == LocalDestinationKind::NomadPage)
        {
            char dest_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
            formatHashHex(session.remote_destination_hash,
                          sizeof(session.remote_destination_hash),
                          dest_hash_hex,
                          sizeof(dest_hash_hex));

            const PathEntry* path = path_manager_.findPath(
                session.remote_destination_hash, millis(), kPathTtlMs);
            if (!path)
            {
                logNomadLinkProofEvent(session, "drop", "path_missing");
                LXMF_LINK_PROOF_LOG("lrproof nomad key miss reason=path_missing dest=%s\n",
                                    dest_hash_hex);
            }
            else if (path->cached_announce_len == 0)
            {
                logNomadLinkProofEvent(session,
                                       "drop",
                                       "announce_missing",
                                       path->hops,
                                       0);
                LXMF_LINK_PROOF_LOG("lrproof nomad key miss reason=announce_missing dest=%s hops=%u\n",
                                    dest_hash_hex,
                                    static_cast<unsigned>(path->hops));
            }
            else
            {
                reticulum::ParsedPacket announce_packet{};
                reticulum::ParsedAnnounce announce{};
                const bool parsed =
                    reticulum::parsePacket(path->cached_announce,
                                           path->cached_announce_len,
                                           &announce_packet) &&
                    announce_packet.packet_type == reticulum::PacketType::Announce &&
                    reticulum::parseAnnounce(announce_packet, &announce) &&
                    announce.valid;
                if (!parsed || !isNomadNetworkNodeAnnounce(announce))
                {
                    logNomadLinkProofEvent(session,
                                           "drop",
                                           "announce_parse_or_aspect",
                                           static_cast<uint32_t>(path->cached_announce_len),
                                           0);
                    LXMF_LINK_PROOF_LOG("lrproof nomad key miss reason=announce_parse_or_aspect dest=%s cached=%u\n",
                                        dest_hash_hex,
                                        static_cast<unsigned>(path->cached_announce_len));
                }
                else
                {
                    uint8_t expected_destination_hash[reticulum::kTruncatedHashSize] = {};
                    reticulum::computeIdentityHash(announce.public_key,
                                                   announce_identity_hash);
                    reticulum::computeDestinationHash(announce.name_hash,
                                                      announce_identity_hash,
                                                      expected_destination_hash);
                    if (!hashesEqual(expected_destination_hash,
                                     session.remote_destination_hash,
                                     reticulum::kTruncatedHashSize))
                    {
                        logNomadLinkProofEvent(session,
                                               "drop",
                                               "announce_dest_mismatch");
                        char expected_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
                        formatHashHex(expected_destination_hash,
                                      sizeof(expected_destination_hash),
                                      expected_hash_hex,
                                      sizeof(expected_hash_hex));
                        LXMF_LINK_PROOF_LOG("lrproof nomad key miss reason=announce_dest_mismatch dest=%s expected=%s\n",
                                            dest_hash_hex,
                                            expected_hash_hex);
                    }
                    else
                    {
                        memcpy(announce_sig_pub,
                               announce.public_key + reticulum::kEncryptionPublicKeySize,
                               sizeof(announce_sig_pub));
                        peer_sig_pub = announce_sig_pub;
                        announce_identity_known = true;
                        LXMF_LINK_PROOF_LOG("lrproof nomad key resolved dest=%s hops=%u cached=%u\n",
                                            dest_hash_hex,
                                            static_cast<unsigned>(path->hops),
                                            static_cast<unsigned>(path->cached_announce_len));
                    }
                }
            }
        }
        if (!peer_sig_pub)
        {
            logNomadLinkProofEvent(session,
                                   "drop",
                                   "peer_sig_missing",
                                   session.remote_identity_known ? 1U : 0U,
                                   0);
            char dest_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
            formatHashHex(session.remote_destination_hash,
                          sizeof(session.remote_destination_hash),
                          dest_hash_hex,
                          sizeof(dest_hash_hex));
            LXMF_LINK_PROOF_LOG("lrproof drop reason=peer_sig_missing kind=%s dest=%s identity_known=%u\n",
                                localDestinationKindLabel(session.destination),
                                dest_hash_hex,
                                session.remote_identity_known ? 1U : 0U);
            return false;
        }

        std::array<uint8_t, reticulum::kTruncatedHashSize + LxmfIdentity::kEncPubKeySize +
                                LxmfIdentity::kSigPubKeySize + kLinkSignallingLen>
            signed_data{};
        size_t used = 0;
        memcpy(signed_data.data() + used, session.link_id, reticulum::kTruncatedHashSize);
        used += reticulum::kTruncatedHashSize;
        memcpy(signed_data.data() + used, peer_enc_pub, LxmfIdentity::kEncPubKeySize);
        used += LxmfIdentity::kEncPubKeySize;
        memcpy(signed_data.data() + used, peer_sig_pub, LxmfIdentity::kSigPubKeySize);
        used += LxmfIdentity::kSigPubKeySize;
        if (signalling_len != 0)
        {
            memcpy(signed_data.data() + used, signalling, signalling_len);
            used += signalling_len;
        }

        if (!LxmfIdentity::verify(peer_sig_pub, signature, signed_data.data(), used))
        {
            logNomadLinkProofEvent(session,
                                   "drop",
                                   "signature_failed",
                                   static_cast<uint32_t>(used),
                                   0);
            char dest_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
            formatHashHex(session.remote_destination_hash,
                          sizeof(session.remote_destination_hash),
                          dest_hash_hex,
                          sizeof(dest_hash_hex));
            LXMF_LINK_PROOF_LOG("lrproof drop reason=signature_failed kind=%s dest=%s signed=%u\n",
                                localDestinationKindLabel(session.destination),
                                dest_hash_hex,
                                static_cast<unsigned>(used));
            return false;
        }

        memcpy(session.peer_enc_pub, peer_enc_pub, sizeof(session.peer_enc_pub));
        memcpy(session.peer_identity_sig_pub,
               peer_sig_pub,
               sizeof(session.peer_identity_sig_pub));
        if (peer)
        {
            copyHash(session.remote_identity_hash,
                     peer->identity_hash,
                     sizeof(session.remote_identity_hash));
        }
        else if (announce_identity_known)
        {
            copyHash(session.remote_identity_hash,
                     announce_identity_hash,
                     sizeof(session.remote_identity_hash));
        }
        session.remote_identity_known = true;
        session.mtu = signalling ? mtuFromLinkSignalling(signalling, signalling_len) : reticulum::kReticulumMtu;
        session.mdu = linkMduForMtu(session.mtu);
        if (!deriveLinkKey(session))
        {
            logNomadLinkProofEvent(session, "drop", "derive_key_failed");
            LXMF_LINK_PROOF_LOG("lrproof drop reason=derive_key_failed kind=%s\n",
                                localDestinationKindLabel(session.destination));
            return false;
        }

        const float rtt_s =
            static_cast<float>((millis() - session.request_ms) / 1000.0f);
        link_manager_.markSessionValidatedActive(
            session,
            rtt_s,
            keepaliveIntervalForRtt(rtt_s));
        const bool rtt_sent = sendLinkRtt(session);
        logNomadLinkProofEvent(session,
                               "active",
                               "ok",
                               static_cast<uint32_t>(millis() - session.request_ms),
                               rtt_sent ? 1U : 0U);
        char link_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        formatHashHex(session.link_id,
                      sizeof(session.link_id),
                      link_hash_hex,
                      sizeof(link_hash_hex));
        LXMF_LINK_PROOF_LOG("lrproof active kind=%s link=%s rtt_ms=%lu mtu=%u mdu=%u rtt_tx=%u\n",
                            localDestinationKindLabel(session.destination),
                            link_hash_hex,
                            static_cast<unsigned long>(millis() - session.request_ms),
                            static_cast<unsigned>(session.mtu),
                            static_cast<unsigned>(session.mdu),
                            rtt_sent ? 1U : 0U);
        if (session.destination == LocalDestinationKind::NomadPage)
        {
            updateNomadPageProgressForDestination(session.remote_destination_hash,
                                                  35,
                                                  "Nomad page link active",
                                                  "ready to request page",
                                                  true,
                                                  false,
                                                  PageFailureKind::None);
        }
        if (session.destination == LocalDestinationKind::CallAudio)
        {
            updateCallRuntimePeer(session, peer);
            ::platform::ui::reticulum_call::mark_link_active(
                session.link_id);
#if TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT
            if (session.call_wire_profile ==
                ReticulumCallWireProfile::MeshChatCallAudio)
            {
                (void)sendLinkIdentify(session);
            }
            else
#endif
            {
                (void)dispatchLxstCallEvent(
                    session,
                    {reticulum::lxst::call::EventType::LinkActive});
            }
        }
        else if (session.destination == LocalDestinationKind::Propagation)
        {
            (void)sendLinkIdentify(session);
        }
        flushDeferredLinkPayloads(session);
        return rtt_sent;
    }

    if (packet.context == static_cast<uint8_t>(reticulum::PacketContext::ResourcePrf))
    {
        return handleLinkResourceProof(session, packet);
    }

    if (packet.context != static_cast<uint8_t>(reticulum::PacketContext::None) ||
        packet.payload_len !=
            (reticulum::kFullHashSize + reticulum::kSignatureSize))
    {
        return false;
    }

    const uint8_t* proved_hash = packet.payload;
    runtime::DeliveryAttemptReceipt* receipt =
        delivery_attempt_ledger_.findLinkPacketReceipt(session.link_id,
                                                       proved_hash);
    if (!receipt)
    {
        return false;
    }

    const uint8_t* peer_signing_key =
        session.initiator ? session.peer_identity_sig_pub
                          : session.peer_link_sig_pub;
    if (isZeroBytes(peer_signing_key, LxmfIdentity::kSigPubKeySize) ||
        !LxmfIdentity::verify(peer_signing_key,
                              packet.payload + reticulum::kFullHashSize,
                              proved_hash,
                              reticulum::kFullHashSize))
    {
        return false;
    }

    const uint32_t message_id = receipt->message_id;
    delivery_attempt_ledger_.removeLinkPacketReceipt(session.link_id,
                                                     proved_hash);
    if (message_id != 0)
    {
        Serial.printf("[LXMF][%s] proof_ok msg=%lu representation=packet\n",
                      session.destination == LocalDestinationKind::Propagation
                          ? "PropagationTX"
                          : "DirectTX",
                      static_cast<unsigned long>(message_id));
        const MessageStatus status =
            session.destination == LocalDestinationKind::Delivery
                ? MessageStatus::Delivered
                : MessageStatus::Queued;
        delivery_notifier_.publish(message_id, status);
    }
    return true;
}

bool LxmfAdapter::handleLinkResourceAdvertisement(LinkSession& session,
                                                  const uint8_t* plaintext, size_t plaintext_len)
{
    DecodedResourceAdvertisement advertisement{};
    if (!decodeResourceAdvertisement(plaintext, plaintext_len, &advertisement))
    {
        Serial.printf("[LXMF][ResourceRX] adv_decode_failed len=%u\n",
                      static_cast<unsigned>(plaintext_len));
        return false;
    }

    const bool encrypted = (advertisement.flags & kResourceFlagEncrypted) != 0;
    const bool compressed = (advertisement.flags & kResourceFlagCompressed) != 0;
    const bool split = (advertisement.flags & kResourceFlagSplit) != 0;
    const bool has_metadata = (advertisement.flags & kResourceFlagHasMetadata) != 0;
    char resource_prefix[9] = {};
    formatHashPrefix(advertisement.resource_hash, resource_prefix, sizeof(resource_prefix));
    const char* reject_reason = nullptr;
    if (has_metadata)
    {
        reject_reason = "metadata";
    }
    else if (advertisement.segment_index == 0 ||
             advertisement.total_segments == 0 ||
             advertisement.segment_index > advertisement.total_segments)
    {
        reject_reason = "segment";
    }
    if (reject_reason)
    {
        Serial.printf("[LXMF][ResourceRX] adv_reject reason=%s resource=%s flags=%02X enc=%u comp=%u "
                      "meta=%u split=%u parts=%u seg=%u/%u transfer=%u data=%u req=%u\n",
                      reject_reason,
                      resource_prefix,
                      static_cast<unsigned>(advertisement.flags),
                      encrypted ? 1U : 0U,
                      compressed ? 1U : 0U,
                      has_metadata ? 1U : 0U,
                      split ? 1U : 0U,
                      static_cast<unsigned>(advertisement.part_count),
                      static_cast<unsigned>(advertisement.segment_index),
                      static_cast<unsigned>(advertisement.total_segments),
                      static_cast<unsigned>(advertisement.transfer_size),
                      static_cast<unsigned>(advertisement.data_size),
                      static_cast<unsigned>(advertisement.request_id.size()));
        (void)sendLinkPacket(session,
                             reticulum::PacketType::Data,
                             reticulum::PacketContext::ResourceRcl,
                             advertisement.resource_hash,
                             reticulum::kFullHashSize,
                             true);
        return true;
    }

    if (advertisement.part_count == 0 ||
        advertisement.hashmap.empty() ||
        (advertisement.hashmap.size() % kResourceMapHashLen) != 0)
    {
        return false;
    }

    if (link_manager_.findIncomingResource(session, advertisement.resource_hash))
    {
        return true;
    }

    if (session.destination == LocalDestinationKind::NomadPage || compressed || split)
    {
        Serial.printf("[LXMF][ResourceRX] adv_accept resource=%s flags=%02X enc=%u comp=%u "
                      "split=%u parts=%u seg=%u/%u transfer=%u data=%u req=%u\n",
                      resource_prefix,
                      static_cast<unsigned>(advertisement.flags),
                      encrypted ? 1U : 0U,
                      compressed ? 1U : 0U,
                      split ? 1U : 0U,
                      static_cast<unsigned>(advertisement.part_count),
                      static_cast<unsigned>(advertisement.segment_index),
                      static_cast<unsigned>(advertisement.total_segments),
                      static_cast<unsigned>(advertisement.transfer_size),
                      static_cast<unsigned>(advertisement.data_size),
                      static_cast<unsigned>(advertisement.request_id.size()));
    }

    LinkResourceTransfer* incoming_resource =
        link_manager_.startIncomingResource(session,
                                            advertisement.resource_hash,
                                            advertisement.random_hash,
                                            advertisement.original_hash,
                                            advertisement.request_id.data(),
                                            advertisement.request_id.size(),
                                            advertisement.hashmap.data(),
                                            advertisement.hashmap.size(),
                                            advertisement.data_size,
                                            advertisement.transfer_size,
                                            advertisement.part_count,
                                            advertisement.segment_index,
                                            advertisement.total_segments,
                                            advertisement.flags,
                                            encrypted,
                                            compressed,
                                            has_metadata,
                                            split,
                                            millis(),
                                            kResourceWindowSize);
    if (!incoming_resource)
    {
        Serial.printf("[LXMF][ResourceRX] adv_reject reason=limits_or_capacity resource=%s "
                      "parts=%u seg=%u/%u transfer=%u data=%u\n",
                      resource_prefix,
                      static_cast<unsigned>(advertisement.part_count),
                      static_cast<unsigned>(advertisement.segment_index),
                      static_cast<unsigned>(advertisement.total_segments),
                      static_cast<unsigned>(advertisement.transfer_size),
                      static_cast<unsigned>(advertisement.data_size));
        (void)sendLinkPacket(session,
                             reticulum::PacketType::Data,
                             reticulum::PacketContext::ResourceRcl,
                             advertisement.resource_hash,
                             reticulum::kFullHashSize,
                             true);
        return true;
    }

    if (session.destination == LocalDestinationKind::Delivery)
    {
        ::platform::ui::reticulum_receive::begin(
            session.link_id,
            incoming_resource->resource_hash,
            incoming_resource->part_count,
            incoming_resource->data_size);
    }

    if (session.destination == LocalDestinationKind::NomadPage &&
        !incoming_resource->request_id.empty())
    {
        if (PendingNomadPageRequest* page_request =
                findPendingNomadPageRequestById(
                    session.remote_destination_hash,
                    incoming_resource->request_id.data(),
                    incoming_resource->request_id.size()))
        {
            char detail[32] = {};
            std::snprintf(detail,
                          sizeof(detail),
                          "0/%u parts",
                          static_cast<unsigned>(advertisement.part_count));
            updateNomadPageProgress(*page_request,
                                    45,
                                    "Receiving Nomad page",
                                    detail,
                                    true,
                                    false,
                                    PageFailureKind::None);
        }
    }
    return requestNextResourceWindow(session, *incoming_resource);
}

bool LxmfAdapter::requestNextResourceWindow(LinkSession& session,
                                            LinkResourceTransfer& resource)
{
    if (resource.complete || resource.part_count == 0)
    {
        return false;
    }

    const runtime::ResourceWindowRequest request =
        link_manager_.buildNextResourceWindowRequest(resource);
    if (!request.valid)
    {
        return false;
    }

    runtime::ResourceMetadataBuffer request_data;
    request_data.reserve(1 + kResourceMapHashLen + reticulum::kFullHashSize +
                         (request.requested_hashes.size() * kResourceMapHashLen));
    if (request.needs_more_hashmap)
    {
        request_data.push_back(0xFF);
        request_data.insert(request_data.end(),
                            request.last_known_hash.begin(),
                            request.last_known_hash.end());
    }
    else
    {
        request_data.push_back(0x00);
    }
    request_data.insert(request_data.end(),
                        resource.resource_hash,
                        resource.resource_hash + reticulum::kFullHashSize);
    for (const auto& requested_hash : request.requested_hashes)
    {
        request_data.insert(request_data.end(), requested_hash.begin(), requested_hash.end());
    }

    link_manager_.noteResourceWindowRequested(resource,
                                              request.needs_more_hashmap,
                                              millis());
    const bool ok = sendLinkPacket(session,
                                   reticulum::PacketType::Data,
                                   reticulum::PacketContext::ResourceReq,
                                   request_data.data(),
                                   request_data.size(),
                                   true);
    if (session.destination == LocalDestinationKind::NomadPage)
    {
        char resource_prefix[9] = {};
        char first_hash[9] = {};
        char last_known_hash[9] = {};
        formatHashPrefix(resource.resource_hash, resource_prefix, sizeof(resource_prefix));
        if (!request.requested_hashes.empty())
        {
            formatHashPrefix(request.requested_hashes.front().data(),
                             first_hash,
                             sizeof(first_hash));
        }
        if (request.needs_more_hashmap)
        {
            formatHashPrefix(request.last_known_hash.data(),
                             last_known_hash,
                             sizeof(last_known_hash));
        }
        Serial.printf("[LXMF][ResourceTX] req resource=%s more_hashmap=%u hashes=%u first=%s last_known=%s payload_len=%u ok=%u\n",
                      resource_prefix,
                      request.needs_more_hashmap ? 1U : 0U,
                      static_cast<unsigned>(request.requested_hashes.size()),
                      first_hash[0] != '\0' ? first_hash : "-",
                      last_known_hash[0] != '\0' ? last_known_hash : "-",
                      static_cast<unsigned>(request_data.size()),
                      ok ? 1U : 0U);
    }
    return ok;
}

bool LxmfAdapter::handleLinkResourceRequest(LinkSession& session,
                                            const uint8_t* plaintext, size_t plaintext_len)
{
    if (!plaintext || plaintext_len < (1 + reticulum::kFullHashSize))
    {
        return false;
    }

    const bool wants_more_hashmap = plaintext[0] == 0xFF;
    size_t offset = 1;
    const uint8_t* last_map_hash = nullptr;
    if (wants_more_hashmap)
    {
        if (plaintext_len < (1 + kResourceMapHashLen + reticulum::kFullHashSize))
        {
            return false;
        }
        last_map_hash = plaintext + offset;
        offset += kResourceMapHashLen;
    }

    const uint8_t* resource_hash = plaintext + offset;
    offset += reticulum::kFullHashSize;
    LinkResourceTransfer* resource =
        link_manager_.findOutgoingResource(session, resource_hash);
    if (!resource)
    {
        return false;
    }

    bool sent_any = false;
    while (offset + kResourceMapHashLen <= plaintext_len)
    {
        const uint8_t* requested_hash = plaintext + offset;
        offset += kResourceMapHashLen;

        for (size_t index = 0; index < resource->map_hashes.size() && index < resource->parts.size(); ++index)
        {
            if (memcmp(resource->map_hashes[index].data(), requested_hash, kResourceMapHashLen) == 0)
            {
                if (!resource->parts[index].empty())
                {
                    sent_any = sendLinkPacket(session,
                                              reticulum::PacketType::Data,
                                              reticulum::PacketContext::Resource,
                                              resource->parts[index].data(),
                                              resource->parts[index].size(),
                                              false) ||
                               sent_any;
                }
                break;
            }
        }
    }

    if (wants_more_hashmap && last_map_hash)
    {
        const size_t segment_capacity = resourceHashmapSegmentCapacity(session.mdu);
        size_t last_index = resource->part_count;
        for (size_t index = 0; index < resource->map_hashes.size(); ++index)
        {
            if (memcmp(resource->map_hashes[index].data(), last_map_hash, kResourceMapHashLen) == 0)
            {
                last_index = index;
                break;
            }
        }

        if (last_index < resource->part_count)
        {
            const size_t next_index = last_index + 1U;
            if (next_index < resource->part_count && (next_index % segment_capacity) == 0)
            {
                const uint32_t segment = static_cast<uint32_t>(next_index / segment_capacity);
                const size_t slice_offset = next_index * kResourceMapHashLen;
                const size_t remaining_hashes = static_cast<size_t>(resource->part_count) - next_index;
                const size_t slice_hashes = std::min(segment_capacity, remaining_hashes);

                std::memset(scratch_.resource_hashmap_update,
                            0,
                            sizeof(scratch_.resource_hashmap_update));
                size_t update_len = sizeof(scratch_.resource_hashmap_update) -
                                    reticulum::kFullHashSize;
                if (encodeResourceHashmapUpdate(segment,
                                                resource->hashmap.data() + slice_offset,
                                                slice_hashes * kResourceMapHashLen,
                                                scratch_.resource_hashmap_update +
                                                    reticulum::kFullHashSize,
                                                &update_len))
                {
                    memcpy(scratch_.resource_hashmap_update,
                           resource->resource_hash,
                           reticulum::kFullHashSize);
                    const size_t wire_len = reticulum::kFullHashSize + update_len;
                    sent_any = sendLinkPacket(session,
                                              reticulum::PacketType::Data,
                                              reticulum::PacketContext::ResourceHmu,
                                              scratch_.resource_hashmap_update,
                                              wire_len,
                                              true) ||
                               sent_any;
                }
            }
        }
    }

    link_manager_.touchResource(*resource, millis());
    return sent_any;
}

bool LxmfAdapter::handleLinkResourceHashmapUpdate(LinkSession& session,
                                                  const uint8_t* plaintext, size_t plaintext_len)
{
    if (!plaintext || plaintext_len <= reticulum::kFullHashSize)
    {
        return false;
    }

    LinkResourceTransfer* resource =
        link_manager_.findIncomingResource(session, plaintext);
    if (!resource)
    {
        return false;
    }

    DecodedResourceHashmapUpdate update{};
    if (!decodeResourceHashmapUpdate(plaintext + reticulum::kFullHashSize,
                                     plaintext_len - reticulum::kFullHashSize,
                                     &update) ||
        (update.hashmap.size() % kResourceMapHashLen) != 0)
    {
        return false;
    }

    const size_t segment_capacity = resourceHashmapSegmentCapacity(session.mdu);
    const size_t start_index = static_cast<size_t>(update.segment) * segment_capacity;
    if (start_index >= resource->part_count)
    {
        return false;
    }

    if (!link_manager_.applyIncomingResourceHashmapUpdate(*resource,
                                                          update.segment,
                                                          update.hashmap.data(),
                                                          update.hashmap.size(),
                                                          segment_capacity,
                                                          millis()))
    {
        return false;
    }
    (void)requestNextResourceWindow(session, *resource);
    return true;
}

bool LxmfAdapter::handleLinkResourcePart(LinkSession& session,
                                         const reticulum::ParsedPacket& packet)
{
    if (!packet.payload || packet.payload_len == 0)
    {
        return false;
    }

    bool saw_incoming_resource = false;
    bool handled_resource_part = false;
    link_manager_.forEachIncomingResource(
        session,
        [&](LinkResourceTransfer& resource) -> bool
        {
            if (resource.complete)
            {
                return true;
            }
            saw_incoming_resource = true;

            uint8_t full_hash[reticulum::kFullHashSize] = {};
            if (!fullHashJoined(packet.payload,
                                packet.payload_len,
                                resource.random_hash,
                                sizeof(resource.random_hash),
                                full_hash))
            {
                return false;
            }

            bool complete = false;
            std::size_t matched_index = resource.part_count;
            if (!link_manager_.recordIncomingResourcePart(resource,
                                                          packet.payload,
                                                          packet.payload_len,
                                                          full_hash,
                                                          millis(),
                                                          &matched_index,
                                                          &complete))
            {
                if (session.destination == LocalDestinationKind::NomadPage)
                {
                    char resource_prefix[9] = {};
                    char part_hash[9] = {};
                    char first_expected[9] = {};
                    uint32_t known_count = 0;
                    formatHashPrefix(resource.resource_hash,
                                     resource_prefix,
                                     sizeof(resource_prefix));
                    formatHashPrefix(full_hash, part_hash, sizeof(part_hash));
                    for (std::size_t index = 0; index < resource.map_hash_known.size(); ++index)
                    {
                        known_count += resource.map_hash_known[index] != 0 ? 1U : 0U;
                    }
                    if (!resource.map_hashes.empty())
                    {
                        formatHashPrefix(resource.map_hashes.front().data(),
                                         first_expected,
                                         sizeof(first_expected));
                    }
                    Serial.printf("[LXMF][ResourceRX] part_unmatched resource=%s part_hash=%s first_expected=%s known=%u/%u payload_len=%u\n",
                                  resource_prefix,
                                  part_hash,
                                  first_expected[0] != '\0' ? first_expected : "-",
                                  static_cast<unsigned>(known_count),
                                  static_cast<unsigned>(resource.part_count),
                                  static_cast<unsigned>(packet.payload_len));
                }
                return true;
            }

            if (session.destination == LocalDestinationKind::NomadPage)
            {
                char resource_prefix[9] = {};
                char part_hash[9] = {};
                formatHashPrefix(resource.resource_hash, resource_prefix, sizeof(resource_prefix));
                formatHashPrefix(full_hash, part_hash, sizeof(part_hash));
                Serial.printf("[LXMF][ResourceRX] part_accept resource=%s part_hash=%s index=%u/%u payload_len=%u complete=%u\n",
                              resource_prefix,
                              part_hash,
                              static_cast<unsigned>(matched_index),
                              static_cast<unsigned>(resource.part_count),
                              static_cast<unsigned>(packet.payload_len),
                              complete ? 1U : 0U);
            }

            if (session.destination == LocalDestinationKind::NomadPage &&
                !resource.request_id.empty())
            {
                if (PendingNomadPageRequest* page_request =
                        findPendingNomadPageRequestById(
                            session.remote_destination_hash,
                            resource.request_id.data(),
                            resource.request_id.size()))
                {
                    uint32_t received_count = 0;
                    for (uint8_t received : resource.received_bitmap)
                    {
                        received_count += received != 0 ? 1U : 0U;
                    }
                    const uint32_t total_count = resource.part_count != 0
                                                     ? resource.part_count
                                                     : 1U;
                    int progress_percent =
                        45 + static_cast<int>((received_count * 45U) / total_count);
                    if (progress_percent > 90)
                    {
                        progress_percent = 90;
                    }
                    char detail[40] = {};
                    std::snprintf(detail,
                                  sizeof(detail),
                                  "%u/%u parts",
                                  static_cast<unsigned>(received_count),
                                  static_cast<unsigned>(resource.part_count));
                    updateNomadPageProgress(*page_request,
                                            complete ? 90 : progress_percent,
                                            "Receiving Nomad page",
                                            detail,
                                            true,
                                            false,
                                            PageFailureKind::None);
                }
            }

            if (session.destination == LocalDestinationKind::Delivery)
            {
                uint32_t received_count = 0;
                uint32_t received_bytes = 0;
                for (std::size_t part_index = 0;
                     part_index < resource.parts.size();
                     ++part_index)
                {
                    if (resource.received_bitmap[part_index] != 0)
                    {
                        ++received_count;
                        received_bytes += static_cast<uint32_t>(
                            resource.parts[part_index].size());
                    }
                }
                ::platform::ui::reticulum_receive::update(
                    resource.resource_hash, received_count, received_bytes);
            }

            if (!complete)
            {
                (void)requestNextResourceWindow(session, resource);
                handled_resource_part = true;
                return false;
            }

            runtime::ResourcePayloadBuffer assembled;
            assembled.reserve(resource.transfer_size);
            for (const auto& part : resource.parts)
            {
                assembled.insert(assembled.end(), part.begin(), part.end());
            }
            if (assembled.size() > resource.transfer_size)
            {
                assembled.resize(resource.transfer_size);
            }

            runtime::ResourcePayloadBuffer resource_stream;
            if (resource.encrypted)
            {
                if (!decryptLinkPayload(session,
                                        assembled.data(),
                                        assembled.size(),
                                        &resource_stream))
                {
                    return false;
                }
            }
            else
            {
                resource_stream = std::move(assembled);
            }

            if (resource.has_metadata || resource_stream.size() < kResourceDataPrefixLen)
            {
                char resource_prefix[9] = {};
                formatHashPrefix(resource.resource_hash, resource_prefix, sizeof(resource_prefix));
                Serial.printf("[LXMF][ResourceRX] assemble_reject resource=%s reason=%s stream=%u\n",
                              resource_prefix,
                              resource.has_metadata ? "metadata" : "short_stream",
                              static_cast<unsigned>(resource_stream.size()));
                return false;
            }

            const uint8_t* resource_payload =
                resource_stream.data() + kResourceDataPrefixLen;
            const size_t resource_payload_len =
                resource_stream.size() - kResourceDataPrefixLen;
            runtime::ResourcePayloadBuffer payload_data;
            if (resource.compressed)
            {
                int bz_status = BZ_OK;
                if (!decompressBzip2Payload(resource_payload,
                                            resource_payload_len,
                                            resource.data_size,
                                            &payload_data,
                                            &bz_status))
                {
                    char resource_prefix[9] = {};
                    formatHashPrefix(resource.resource_hash, resource_prefix, sizeof(resource_prefix));
                    Serial.printf("[LXMF][ResourceRX] decompress_failed resource=%s bz=%d comp_len=%u "
                                  "expected=%u stream=%u\n",
                                  resource_prefix,
                                  bz_status,
                                  static_cast<unsigned>(resource_payload_len),
                                  static_cast<unsigned>(resource.data_size),
                                  static_cast<unsigned>(resource_stream.size()));
                    return false;
                }
                if (session.destination == LocalDestinationKind::NomadPage)
                {
                    char resource_prefix[9] = {};
                    formatHashPrefix(resource.resource_hash, resource_prefix, sizeof(resource_prefix));
                    Serial.printf("[LXMF][ResourceRX] decompressed resource=%s comp_len=%u out=%u\n",
                                  resource_prefix,
                                  static_cast<unsigned>(resource_payload_len),
                                  static_cast<unsigned>(payload_data.size()));
                }
            }
            else
            {
                payload_data.assign(resource_stream.begin() + kResourceDataPrefixLen,
                                    resource_stream.end());
            }
            if (payload_data.size() != resource.data_size)
            {
                char resource_prefix[9] = {};
                formatHashPrefix(resource.resource_hash, resource_prefix, sizeof(resource_prefix));
                Serial.printf("[LXMF][ResourceRX] assemble_reject resource=%s reason=size actual=%u expected=%u\n",
                              resource_prefix,
                              static_cast<unsigned>(payload_data.size()),
                              static_cast<unsigned>(resource.data_size));
                return false;
            }

            uint8_t expected_resource_hash[reticulum::kFullHashSize] = {};
            if (!fullHashJoined(payload_data.data(),
                                payload_data.size(),
                                resource.random_hash,
                                sizeof(resource.random_hash),
                                expected_resource_hash))
            {
                return false;
            }
            if (!hashesEqual(expected_resource_hash,
                             resource.resource_hash,
                             reticulum::kFullHashSize))
            {
                return false;
            }

            if (!fullHashJoined(payload_data.data(),
                                payload_data.size(),
                                resource.resource_hash,
                                reticulum::kFullHashSize,
                                resource.expected_proof))
            {
                return false;
            }

            std::array<uint8_t, reticulum::kFullHashSize * 2> proof_payload{};
            memcpy(proof_payload.data(), resource.resource_hash, reticulum::kFullHashSize);
            memcpy(proof_payload.data() + reticulum::kFullHashSize,
                   resource.expected_proof,
                   reticulum::kFullHashSize);

            const bool is_request = (resource.flags & kResourceFlagRequest) != 0;
            const bool is_response = (resource.flags & kResourceFlagResponse) != 0;
            const bool single_segment =
                !resource.split && resource.total_segments <= 1U;
            bool delivery_preaccepted = false;
            if (single_segment && !is_request && !is_response &&
                session.destination == LocalDestinationKind::Delivery)
            {
                delivery_preaccepted =
                    acceptVerifiedEnvelope(payload_data.data(),
                                           payload_data.size(),
                                           nullptr,
                                           0);
                if (!delivery_preaccepted)
                {
                    uint8_t rejected_resource_hash[reticulum::kFullHashSize] = {};
                    copyHash(rejected_resource_hash,
                             resource.resource_hash,
                             sizeof(rejected_resource_hash));
                    char resource_prefix[9] = {};
                    formatHashPrefix(rejected_resource_hash,
                                     resource_prefix,
                                     sizeof(resource_prefix));
                    (void)link_manager_.eraseIncomingResource(
                        session,
                        rejected_resource_hash);
                    Serial.printf("[LXMF][ResourceRX] delivery_rejected resource=%s reason=queue_or_envelope\n",
                                  resource_prefix);
                    return false;
                }
            }

            (void)sendLinkPacket(session,
                                 reticulum::PacketType::Proof,
                                 reticulum::PacketContext::ResourcePrf,
                                 proof_payload.data(),
                                 proof_payload.size(),
                                 false);

            link_manager_.markResourceComplete(resource, millis());

            const runtime::ResourceAssemblyResult assembly_result =
                link_manager_.appendResourceAssemblySegment(session,
                                                            resource,
                                                            payload_data,
                                                            millis());
            if (assembly_result == runtime::ResourceAssemblyResult::Rejected)
            {
                return false;
            }
            if (assembly_result == runtime::ResourceAssemblyResult::WaitingForNextSegment)
            {
                handled_resource_part = true;
                return false;
            }

            if (is_request)
            {
                DecodedLinkRequest request{};
                if (decodeLinkRequestPayload(payload_data.data(), payload_data.size(), &request))
                {
                    uint8_t request_id[reticulum::kTruncatedHashSize] = {};
                    reticulum::truncatedHash(payload_data.data(), payload_data.size(), request_id);
                    if (session.destination == LocalDestinationKind::Propagation)
                    {
                        (void)handlePropagationRequest(session,
                                                       request,
                                                       request_id,
                                                       sizeof(request_id));
                    }
                    else
                    {
                        (void)sendLinkResponse(session,
                                               request_id,
                                               sizeof(request_id),
                                               nullptr,
                                               0,
                                               true);
                    }
                }
            }
            else if (is_response)
            {
                DecodedLinkResponse response{};
                if (decodeLinkResponsePayload(payload_data.data(), payload_data.size(), &response))
                {
                    (void)link_manager_.markPendingResponseReady(
                        session,
                        response.request_id.data(),
                        response.request_id.size(),
                        response.packed_data.data(),
                        response.packed_data.size(),
                        response.data_is_nil);
                }
            }
            else if (session.destination == LocalDestinationKind::Delivery &&
                     !delivery_preaccepted)
            {
                delivery_preaccepted =
                    acceptVerifiedEnvelope(payload_data.data(),
                                           payload_data.size(),
                                           nullptr,
                                           0);
            }
            else if (session.destination == LocalDestinationKind::Propagation)
            {
                if (rtnet::active().propagation.service_enabled)
                {
                    (void)handlePropagationBatch(session,
                                                 payload_data.data(),
                                                 payload_data.size());
                }
            }

            if (session.destination == LocalDestinationKind::Delivery &&
                delivery_preaccepted)
            {
                ::platform::ui::reticulum_receive::complete(
                    resource.resource_hash);
            }

            handled_resource_part = true;
            return false;
        });

    if (!saw_incoming_resource && session.destination == LocalDestinationKind::NomadPage)
    {
        char link_hash[9] = {};
        formatHashPrefix(session.link_id, link_hash, sizeof(link_hash));
        Serial.printf("[LXMF][ResourceRX] part_drop reason=no_incoming_resource link=%s payload_len=%u\n",
                      link_hash,
                      static_cast<unsigned>(packet.payload_len));
    }
    return handled_resource_part;
}

bool LxmfAdapter::handleLinkResourceProof(LinkSession& session,
                                          const reticulum::ParsedPacket& packet)
{
    if (!packet.payload || packet.payload_len != (reticulum::kFullHashSize * 2))
    {
        return false;
    }

    const uint8_t* resource_hash = packet.payload;
    const uint8_t* expected_proof = packet.payload + reticulum::kFullHashSize;
    LinkResourceTransfer* resource =
        link_manager_.findOutgoingResource(session, resource_hash);
    if (!resource)
    {
        return false;
    }

    if (!link_manager_.markOutgoingResourceProofReceived(*resource,
                                                         expected_proof,
                                                         millis()))
    {
        return false;
    }

    runtime::DeliveryAttemptReceipt* receipt =
        delivery_attempt_ledger_.findLinkResourceReceipt(session.link_id,
                                                         resource_hash);
    const uint32_t message_id = receipt ? receipt->message_id : 0;
    if (receipt)
    {
        delivery_attempt_ledger_.removeLinkResourceReceipt(session.link_id,
                                                           resource_hash);
    }
    if (message_id != 0)
    {
        Serial.printf("[LXMF][%s] proof_ok msg=%lu representation=resource\n",
                      session.destination == LocalDestinationKind::Propagation
                          ? "PropagationTX"
                          : "DirectTX",
                      static_cast<unsigned long>(message_id));
        const MessageStatus status =
            session.destination == LocalDestinationKind::Delivery
                ? MessageStatus::Delivered
                : MessageStatus::Queued;
        delivery_notifier_.publish(message_id, status);
    }
    return true;
}

bool LxmfAdapter::handlePropagationBatch(LinkSession& session,
                                         const uint8_t* plaintext,
                                         size_t plaintext_len)
{
    runtime::PropagationBatchContext batch_context{};
    batch_context.offer_validated = session.propagation_offer_validated;
    batch_context.local_delivery_hash_known = true;
    localDestinationHash(LocalDestinationKind::Delivery, batch_context.local_delivery_hash);
    batch_context.peer_context.remote_identity_known = session.remote_identity_known;
    batch_context.now_s = currentTimestampSeconds();
    if (session.remote_identity_known)
    {
        destinationHashForAspect(session.remote_identity_hash,
                                 "delivery",
                                 batch_context.peer_context.remote_delivery_hash);
        destinationHashForAspect(session.remote_identity_hash,
                                 "propagation",
                                 batch_context.peer_context.remote_propagation_hash);
        copyHash(batch_context.peer_context.remote_identity_hash,
                 session.remote_identity_hash,
                 sizeof(batch_context.peer_context.remote_identity_hash));
    }

    const runtime::PropagationBatchLimits batch_limits{
        kMaxPropagationEntries,
        kMaxPropagationTransients,
        kMaxPropagationPeers,
        1};
    runtime::PropagationBatchAcceptance batch_acceptance{};
    if (!propagation_client_.planBatchAcceptance(plaintext,
                                                 plaintext_len,
                                                 batch_context,
                                                 batch_limits,
                                                 &batch_acceptance))
    {
        return false;
    }

    bool handled_any = false;
    for (const auto& message : batch_acceptance.messages)
    {
        bool handled = false;
        if (message.action == runtime::PropagationMessageAction::Duplicate ||
            message.action == runtime::PropagationMessageAction::Stored)
        {
            handled = true;
        }
        else if (message.action == runtime::PropagationMessageAction::DeliverLocal)
        {
            const bool delivered =
                acceptPropagatedDelivery(message.local_delivery_payload.empty()
                                             ? nullptr
                                             : message.local_delivery_payload.data(),
                                         message.local_delivery_payload.size());
            propagation_client_.noteLocalDeliveryResult(message.transient_id,
                                                        delivered,
                                                        currentTimestampSeconds(),
                                                        kMaxPropagationTransients);
            handled = delivered;
        }

        if (handled)
        {
            handled_any = true;
            propagation_client_.noteBatchHandled(batch_acceptance);
        }
    }

    return handled_any;
}

bool LxmfAdapter::handlePropagationRequest(LinkSession& session,
                                           const DecodedLinkRequest& request,
                                           const uint8_t* request_id,
                                           size_t request_id_len)
{
    runtime::PropagationServicePeerContext peer_context{};
    peer_context.remote_identity_known = session.remote_identity_known;
    if (session.remote_identity_known)
    {
        destinationHashForAspect(session.remote_identity_hash,
                                 "delivery",
                                 peer_context.remote_delivery_hash);
        destinationHashForAspect(session.remote_identity_hash,
                                 "propagation",
                                 peer_context.remote_propagation_hash);
        copyHash(peer_context.remote_identity_hash,
                 session.remote_identity_hash,
                 sizeof(peer_context.remote_identity_hash));
    }

    const runtime::PropagationServiceLimits limits{
        kMaxPropagationTransients,
        kMaxPropagationPeers,
        24,
        16};
    runtime::PropagationServiceResponse response{};
    if (!propagation_client_.planServiceResponse(request,
                                                 peer_context,
                                                 currentTimestampSeconds(),
                                                 limits,
                                                 &response))
    {
        return false;
    }
    if (response.offer_validated)
    {
        session.propagation_offer_validated = true;
    }
    if (!response.send_response)
    {
        return false;
    }
    return sendLinkResponse(session,
                            request_id,
                            request_id_len,
                            response.packed_response.empty() ? nullptr
                                                             : response.packed_response.data(),
                            response.packed_response.size(),
                            response.response_data_is_nil);
}

bool LxmfAdapter::acceptPropagatedDelivery(const uint8_t* propagated_payload,
                                           size_t propagated_payload_len,
                                           uint8_t* out_message_hash,
                                           bool* out_awaiting_commit)
{
    if (!propagated_payload ||
        propagated_payload_len <= (reticulum::kEncryptionPublicKeySize + reticulum::kTokenOverhead))
    {
        return false;
    }

    const uint8_t* peer_ephemeral_pub = propagated_payload;
    const uint8_t* token = propagated_payload + reticulum::kEncryptionPublicKeySize;
    const size_t token_len = propagated_payload_len - reticulum::kEncryptionPublicKeySize;

    uint8_t shared_secret[LxmfIdentity::kEncPubKeySize] = {};
    if (!identity_.deriveSharedSecret(peer_ephemeral_pub, shared_secret))
    {
        return false;
    }

    uint8_t derived_key[reticulum::kDerivedTokenKeySize] = {};
    if (!reticulum::hkdfSha256(shared_secret,
                               sizeof(shared_secret),
                               identity_.identityHash(),
                               reticulum::kTruncatedHashSize,
                               nullptr,
                               0,
                               derived_key,
                               sizeof(derived_key)))
    {
        return false;
    }

    runtime::RuntimeByteBuffer plaintext(token_len, 0);
    size_t plaintext_len = plaintext.size();
    if (!reticulum::tokenDecrypt(derived_key,
                                 token,
                                 token_len,
                                 plaintext.data(),
                                 &plaintext_len))
    {
        return false;
    }
    plaintext.resize(plaintext_len);

    runtime::RuntimeByteBuffer lxmf_message(
        reticulum::kTruncatedHashSize + plaintext.size(),
        0);
    memcpy(lxmf_message.data(), identity_.destinationHash(), reticulum::kTruncatedHashSize);
    if (!plaintext.empty())
    {
        memcpy(lxmf_message.data() + reticulum::kTruncatedHashSize,
               plaintext.data(),
               plaintext.size());
    }

    return acceptVerifiedEnvelope(lxmf_message.data(),
                                  lxmf_message.size(),
                                  nullptr,
                                  0,
                                  out_message_hash,
                                  out_awaiting_commit);
}

bool LxmfAdapter::sendForwardPlan(const reticulum::ParsedPacket& packet,
                                  const runtime::PacketForwardPlan& plan)
{
    if (!plan.forward || !packet.destination_hash ||
        plan.interface_id == reticulum::interfaces::kInvalidInterfaceId)
    {
        return false;
    }

    std::memset(scratch_.forward_packet, 0, sizeof(scratch_.forward_packet));
    size_t forward_len = sizeof(scratch_.forward_packet);
    bool built = false;
    if (plan.header == runtime::PacketForwardHeader::Header1Broadcast)
    {
        built = reticulum::buildHeader1Packet(
            packet.packet_type,
            packet.destination_type,
            static_cast<reticulum::PacketContext>(packet.context),
            packet.context_flag != 0,
            packet.destination_hash,
            packet.payload,
            packet.payload_len,
            scratch_.forward_packet,
            &forward_len,
            plan.hops,
            reticulum::TransportType::Broadcast);
    }
    else if (plan.header == runtime::PacketForwardHeader::Header2Transport)
    {
        built = reticulum::buildHeader2Packet(
            packet.packet_type,
            packet.destination_type,
            static_cast<reticulum::PacketContext>(packet.context),
            packet.context_flag != 0,
            plan.next_hop_transport,
            packet.destination_hash,
            packet.payload,
            packet.payload_len,
            scratch_.forward_packet,
            &forward_len,
            plan.hops);
    }

    return built && interfaces_.sendPacketOn(plan.interface_id,
                                             scratch_.forward_packet,
                                             forward_len);
}

bool LxmfAdapter::maybeForwardTransportPacket(const uint8_t* raw_packet, size_t raw_len,
                                              const reticulum::ParsedPacket& packet)
{
    (void)raw_packet;
    (void)raw_len;
    (void)packet;
    return false;
}

bool LxmfAdapter::maybeForwardLinkPacket(const uint8_t* raw_packet, size_t raw_len,
                                         const reticulum::ParsedPacket& packet)
{
    (void)raw_packet;
    (void)raw_len;
    (void)packet;
    return false;
}

bool LxmfAdapter::sendProofForPacket(const uint8_t* raw_packet, size_t raw_len)
{
    if (!raw_packet || raw_len == 0 || !isReady())
    {
        return false;
    }

    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    reticulum::computePacketHash(raw_packet, raw_len, packet_hash);

    uint8_t signature[reticulum::kSignatureSize] = {};
    if (!identity_.sign(packet_hash, sizeof(packet_hash), signature))
    {
        return false;
    }

    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    memcpy(destination_hash, packet_hash, sizeof(destination_hash));

    std::memset(scratch_.proof_packet, 0, sizeof(scratch_.proof_packet));
    size_t proof_len = sizeof(scratch_.proof_packet);
    if (!reticulum::buildHeader1Packet(reticulum::PacketType::Proof,
                                       reticulum::DestinationType::Single,
                                       reticulum::PacketContext::None,
                                       false,
                                       destination_hash,
                                       signature,
                                       sizeof(signature),
                                       scratch_.proof_packet,
                                       &proof_len))
    {
        return false;
    }

    return active_ingress_interface_id_ !=
                   reticulum::interfaces::kInvalidInterfaceId
               ? interfaces_.sendPacketOn(active_ingress_interface_id_,
                                          scratch_.proof_packet,
                                          proof_len)
               : interfaces_.sendPacket(scratch_.proof_packet, proof_len);
}

bool LxmfAdapter::sendPathRequest(PeerInfo& peer)
{
    if (!isReady() || isZeroBytes(peer.destination_hash, sizeof(peer.destination_hash)))
    {
        return false;
    }

    const uint32_t now_ms = millis();
    if (path_manager_.pendingPathRequestCoolingDown(
            peer.destination_hash,
            now_ms,
            kPathRequestMinIntervalMs))
    {
        return false;
    }

    uint8_t request_payload[reticulum::kTruncatedHashSize + kPathRequestTagSize] = {};
    memcpy(request_payload, peer.destination_hash, reticulum::kTruncatedHashSize);
    fillRandomBytes(request_payload + reticulum::kTruncatedHashSize, kPathRequestTagSize);

    uint8_t control_hash[reticulum::kTruncatedHashSize] = {};
    pathRequestDestinationHash(control_hash);

    std::memset(scratch_.path_request_packet,
                0,
                sizeof(scratch_.path_request_packet));
    size_t packet_len = sizeof(scratch_.path_request_packet);
    if (!reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                       reticulum::DestinationType::Plain,
                                       reticulum::PacketContext::None,
                                       false,
                                       control_hash,
                                       request_payload,
                                       sizeof(request_payload),
                                       scratch_.path_request_packet,
                                       &packet_len))
    {
        return false;
    }

    if (!routeAndSendPacket(scratch_.path_request_packet, packet_len, false))
    {
        return false;
    }

    path_manager_.notePendingPathRequest(
        peer.destination_hash, now_ms, kMaxPendingPathRequests);
    path_manager_.notePeerPathRequest(peer, now_ms);
    return true;
}

bool LxmfAdapter::sendPathRequestForDestination(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    if (!destination_hash ||
        isZeroBytes(destination_hash, reticulum::kTruncatedHashSize) ||
        !identity_.isReady())
    {
        return false;
    }

    const uint32_t now_ms = millis();
    if (path_manager_.pendingPathRequestCoolingDown(destination_hash,
                                                    now_ms,
                                                    kPathRequestMinIntervalMs))
    {
        return false;
    }

    uint8_t request_payload[reticulum::kTruncatedHashSize + kPathRequestTagSize] = {};
    memcpy(request_payload, destination_hash, reticulum::kTruncatedHashSize);
    fillRandomBytes(request_payload + reticulum::kTruncatedHashSize, kPathRequestTagSize);

    uint8_t control_hash[reticulum::kTruncatedHashSize] = {};
    pathRequestDestinationHash(control_hash);

    std::memset(scratch_.path_request_packet,
                0,
                sizeof(scratch_.path_request_packet));
    size_t packet_len = sizeof(scratch_.path_request_packet);
    if (!reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                       reticulum::DestinationType::Plain,
                                       reticulum::PacketContext::None,
                                       false,
                                       control_hash,
                                       request_payload,
                                       sizeof(request_payload),
                                       scratch_.path_request_packet,
                                       &packet_len))
    {
        return false;
    }

    if (!routeAndSendPacket(scratch_.path_request_packet,
                            packet_len,
                            false,
                            true))
    {
        return false;
    }

    path_manager_.notePendingPathRequest(
        destination_hash, now_ms, kMaxPendingPathRequests);
    return true;
}

bool LxmfAdapter::shouldRequestPath(const PeerInfo& peer) const
{
    if (isZeroBytes(peer.destination_hash, sizeof(peer.destination_hash)))
    {
        return false;
    }

    return path_manager_.shouldRequestPeerPath(peer,
                                               millis(),
                                               currentTimestampSeconds(),
                                               kPendingPathRequestTtlMs,
                                               kPathRequestMinIntervalMs,
                                               kPathTtlMs,
                                               kPathRefreshAgeS);
}

LxmfAdapter::LinkSession* LxmfAdapter::ensureOutboundLinkSession(PeerInfo& peer,
                                                                 LocalDestinationKind kind,
                                                                 bool* out_started)
{
    if (out_started)
    {
        *out_started = false;
    }

    if (isZeroBytes(peer.destination_hash, sizeof(peer.destination_hash)))
    {
        return nullptr;
    }

    if (LinkSession* session =
            link_manager_.findOpenSessionByDestination(peer.destination_hash, kind))
    {
        return session;
    }

    const PathEntry* path =
        path_manager_.findPath(peer.destination_hash, millis(), kPathTtlMs);
    bool path_requested = false;
    if (!path && shouldRequestPath(peer))
    {
        path_requested = sendPathRequest(peer);
    }

    const uint32_t now_ms = millis();
    runtime::LinkSessionSpec session_spec{};
    session_spec.now_ms = now_ms;
    session_spec.keepalive_interval_ms = kLinkKeepaliveMaxMs;
    session_spec.stale_timeout_ms = kLinkKeepaliveMaxMs * 2U;
    session_spec.remote_destination_hash = peer.destination_hash;
    session_spec.remote_identity_hash = peer.identity_hash;
    session_spec.peer_identity_sig_pub = peer.sig_pub;
    session_spec.expected_hops = path ? path->hops : 0;
    session_spec.destination = kind;
    session_spec.state = LinkState::Pending;
    session_spec.initiator = true;
    session_spec.remote_identity_known =
        !isZeroBytes(peer.identity_hash, sizeof(peer.identity_hash));

    LinkSession* new_session =
        link_manager_.openSession(kMaxLinkSessions, session_spec);
    if (!new_session)
    {
        return nullptr;
    }
    LinkSession& session = *new_session;

    Curve25519::dh1(session.local_enc_pub, session.local_enc_priv);
    char dest_hash[12] = {};
    formatHashPrefix(peer.destination_hash, dest_hash, sizeof(dest_hash));
    if (isZeroBytes(session.local_enc_priv, sizeof(session.local_enc_priv)))
    {
        Serial.printf("[LXMF][LinkTX] start_failed peer=%08lX dest=%s kind=%u reason=local_key\n",
                      static_cast<unsigned long>(peer.node_id),
                      dest_hash,
                      static_cast<unsigned>(kind));
        link_manager_.discardSession(session);
        return nullptr;
    }

    if (!generateLinkSigningKey(session.local_sig_pub, session.local_sig_priv))
    {
        Serial.printf("[LXMF][LinkTX] start_failed peer=%08lX dest=%s kind=%u reason=signing_key\n",
                      static_cast<unsigned long>(peer.node_id),
                      dest_hash,
                      static_cast<unsigned>(kind));
        link_manager_.discardSession(session);
        return nullptr;
    }

    if (!path)
    {
        Serial.printf("[LXMF][LinkTX] wait_path peer=%08lX dest=%s kind=%u requested=%u\n",
                      static_cast<unsigned long>(peer.node_id),
                      dest_hash,
                      static_cast<unsigned>(kind),
                      path_requested ? 1U : 0U);
        if (out_started)
        {
            *out_started = true;
        }
        return &session;
    }

    if (!sendLinkRequest(session))
    {
        Serial.printf("[LXMF][LinkTX] start_failed peer=%08lX dest=%s kind=%u reason=request_send\n",
                      static_cast<unsigned long>(peer.node_id),
                      dest_hash,
                      static_cast<unsigned>(kind));
        link_manager_.discardSession(session);
        return nullptr;
    }

    if (out_started)
    {
        *out_started = true;
    }
    return &session;
}

bool LxmfAdapter::prepareLinkRequest(LinkSession& session)
{
    if (isZeroBytes(session.remote_destination_hash,
                    sizeof(session.remote_destination_hash)) ||
        isZeroBytes(session.local_enc_pub, sizeof(session.local_enc_pub)) ||
        isZeroBytes(session.local_sig_pub, sizeof(session.local_sig_pub)))
    {
        return false;
    }

    constexpr size_t kRequestPayloadLen =
        kLinkRequestBaseLen + kLinkSignallingLen;
    std::memset(scratch_.link_request_payload, 0, kRequestPayloadLen);
    std::memcpy(scratch_.link_request_payload,
                session.local_enc_pub,
                LxmfIdentity::kEncPubKeySize);
    std::memcpy(scratch_.link_request_payload + LxmfIdentity::kEncPubKeySize,
                session.local_sig_pub,
                LxmfIdentity::kSigPubKeySize);
    buildLinkSignallingBytes(
        reticulum::kReticulumMtu,
        scratch_.link_request_payload + kLinkRequestBaseLen);

    link_request_packet_len_ = sizeof(scratch_.link_request_packet);
    if (!reticulum::buildHeader1Packet(
            reticulum::PacketType::LinkRequest,
            reticulum::DestinationType::Single,
            reticulum::PacketContext::None,
            false,
            session.remote_destination_hash,
            scratch_.link_request_payload,
            kRequestPayloadLen,
            scratch_.link_request_packet,
            &link_request_packet_len_))
    {
        link_request_packet_len_ = 0;
        return false;
    }

    reticulum::ParsedPacket parsed{};
    uint8_t prepared_link_id[reticulum::kTruncatedHashSize] = {};
    if (!reticulum::parsePacket(scratch_.link_request_packet,
                                link_request_packet_len_,
                                &parsed) ||
        !computeLinkIdFromLinkRequest(scratch_.link_request_packet,
                                      link_request_packet_len_,
                                      parsed,
                                      prepared_link_id))
    {
        link_request_packet_len_ = 0;
        return false;
    }
    if (!isZeroBytes(session.link_id, sizeof(session.link_id)) &&
        !hashesEqual(session.link_id,
                     prepared_link_id,
                     sizeof(prepared_link_id)))
    {
        link_request_packet_len_ = 0;
        return false;
    }
    copyHash(session.link_id, prepared_link_id, sizeof(session.link_id));
    return true;
}

bool LxmfAdapter::sendLinkRequest(LinkSession& session)
{
    char dest_hash[12] = {};
    formatHashPrefix(session.remote_destination_hash, dest_hash, sizeof(dest_hash));
    if (!isReady() || !prepareLinkRequest(session))
    {
        Serial.printf("[LXMF][LinkTX] request_fail dest=%s kind=%u reason=not_ready_or_prepare ready=%u\n",
                      dest_hash,
                      static_cast<unsigned>(session.destination),
                      isReady() ? 1U : 0U);
        return false;
    }

    reticulum::ParsedPacket parsed{};
    if (!reticulum::parsePacket(scratch_.link_request_packet,
                                link_request_packet_len_,
                                &parsed) ||
        !parsed.destination_hash)
    {
        Serial.printf("[LXMF][LinkTX] request_fail dest=%s kind=%u reason=parse raw_len=%u\n",
                      dest_hash,
                      static_cast<unsigned>(session.destination),
                      static_cast<unsigned>(link_request_packet_len_));
        return false;
    }

    const uint8_t* tx_packet = scratch_.link_request_packet;
    size_t tx_packet_len = link_request_packet_len_;
    bool routed = false;
    const PathEntry* tx_path =
        path_manager_.findPath(parsed.destination_hash, millis(), kPathTtlMs);
    if (session.destination == LocalDestinationKind::CallAudio &&
        isLoRaPath(tx_path))
    {
        Serial.printf("[LXMF][LinkTX] request_fail dest=%s kind=%u reason=call_lora_path\n",
                      dest_hash,
                      static_cast<unsigned>(session.destination));
        return false;
    }
    if (tx_path && tx_path->hops > 1 && !tx_path->direct)
    {
        tx_packet_len = sizeof(scratch_.link_request_routed);
        if (!reticulum::buildHeader2Packet(
                parsed.packet_type,
                parsed.destination_type,
                static_cast<reticulum::PacketContext>(parsed.context),
                parsed.context_flag != 0,
                tx_path->next_hop_transport,
                parsed.destination_hash,
                parsed.payload,
                parsed.payload_len,
                scratch_.link_request_routed,
                &tx_packet_len,
                scratch_.link_request_packet[1]))
        {
            Serial.printf("[LXMF][LinkTX] request_fail dest=%s kind=%u reason=route_build raw_len=%u\n",
                          dest_hash,
                          static_cast<unsigned>(session.destination),
                          static_cast<unsigned>(link_request_packet_len_));
            return false;
        }
        tx_packet = scratch_.link_request_routed;
        routed = true;
    }

    reticulum::ParsedPacket tx_parsed{};
    uint8_t transmitted_link_id[reticulum::kTruncatedHashSize] = {};
    if (!reticulum::parsePacket(tx_packet, tx_packet_len, &tx_parsed) ||
        !computeLinkIdFromLinkRequest(tx_packet,
                                      tx_packet_len,
                                      tx_parsed,
                                      transmitted_link_id) ||
        !hashesEqual(session.link_id,
                     transmitted_link_id,
                     sizeof(transmitted_link_id)))
    {
        Serial.printf("[LXMF][LinkTX] request_fail dest=%s kind=%u reason=link_id raw_len=%u routed=%u\n",
                      dest_hash,
                      static_cast<unsigned>(session.destination),
                      static_cast<unsigned>(tx_packet_len),
                      routed ? 1U : 0U);
        return false;
    }

    const bool wifi_only =
        session.destination == LocalDestinationKind::CallAudio;
    if (wifi_only && !lxst_telephony_client_.runtimeStarted(session))
    {
        Serial.printf("[LXMF][LinkTX] request_fail dest=%s kind=%u reason=call_runtime_not_started\n",
                      dest_hash,
                      static_cast<unsigned>(session.destination));
        return false;
    }

    session.request_ms = millis();
    session.interface_id = tx_path ? tx_path->interface_id
                                   : reticulum::interfaces::kInvalidInterfaceId;
    const bool sent =
        session.interface_id != reticulum::interfaces::kInvalidInterfaceId
            ? interfaces_.sendPacketOn(session.interface_id,
                                       tx_packet,
                                       tx_packet_len,
                                       wifi_only ? session.link_id : nullptr)
            : (wifi_only ? interfaces_.sendPacketWifiOnly(tx_packet,
                                                          tx_packet_len,
                                                          session.link_id)
                         : interfaces_.sendPacket(tx_packet, tx_packet_len));
    const auto& tx_result = interfaces_.lastTxResult();
    if (sent)
    {
        link_manager_.touchOutbound(session, session.request_ms);
        session.mtu = reticulum::kReticulumMtu;
        session.mdu = linkMduForMtu(session.mtu);
        char link_hash[12] = {};
        char next_hop[12] = {};
        formatHashPrefix(session.link_id, link_hash, sizeof(link_hash));
        formatHashPrefix(tx_path ? tx_path->next_hop_transport : nullptr,
                         next_hop,
                         sizeof(next_hop));
        const uint32_t now_s = currentTimestampSeconds();
        const uint32_t path_age_s =
            (tx_path && tx_path->last_seen_s != 0 &&
             now_s >= tx_path->last_seen_s)
                ? (now_s - tx_path->last_seen_s)
                : 0;
        Serial.printf("[LXMF][LinkTX] request_sent dest=%s kind=%u link=%s raw_len=%u routed=%u path_hops=%u path_direct=%u path_age_s=%lu next=%s announce=%u bearer=%s complete=%u\n",
                      dest_hash,
                      static_cast<unsigned>(session.destination),
                      link_hash,
                      static_cast<unsigned>(tx_packet_len),
                      routed ? 1U : 0U,
                      static_cast<unsigned>(tx_path ? tx_path->hops : 0U),
                      tx_path && tx_path->direct ? 1U : 0U,
                      static_cast<unsigned long>(path_age_s),
                      next_hop,
                      static_cast<unsigned>(
                          tx_path ? tx_path->cached_announce_len : 0U),
                      txBearerName(tx_result),
                      tx_result.reachedRequiredInterfaces() ? 1U : 0U);
    }
    else
    {
        Serial.printf("[LXMF][LinkTX] request_fail dest=%s kind=%u reason=send raw_len=%u routed=%u bearer=%s complete=%u\n",
                      dest_hash,
                      static_cast<unsigned>(session.destination),
                      static_cast<unsigned>(tx_packet_len),
                      routed ? 1U : 0U,
                      txBearerName(tx_result),
                      tx_result.reachedRequiredInterfaces() ? 1U : 0U);
    }
    return sent;
}

bool LxmfAdapter::buildSignedMessagePacket(const PeerInfo& peer,
                                           const uint8_t* packed_payload, size_t packed_payload_len,
                                           uint8_t* out_packet, size_t* inout_len,
                                           uint8_t out_message_hash[reticulum::kFullHashSize])
{
    if ((!packed_payload && packed_payload_len != 0) || !out_packet || !inout_len || !out_message_hash)
    {
        return false;
    }

    uint8_t signed_part[kSignedPartMaxLen] = {};
    size_t signed_part_len = sizeof(signed_part);
    if (!buildSignedPart(peer.destination_hash,
                         identity_.destinationHash(),
                         packed_payload,
                         packed_payload_len,
                         signed_part,
                         &signed_part_len,
                         out_message_hash))
    {
        return false;
    }

    uint8_t signature[reticulum::kSignatureSize] = {};
    if (!identity_.sign(signed_part, signed_part_len, signature))
    {
        return false;
    }

    uint8_t lxmf_message[kMaxLxmfMessageLen] = {};
    size_t lxmf_message_len = sizeof(lxmf_message);
    if (!packMessage(peer.destination_hash,
                     identity_.destinationHash(),
                     signature,
                     packed_payload,
                     packed_payload_len,
                     lxmf_message,
                     &lxmf_message_len))
    {
        return false;
    }

    if (lxmf_message_len <= reticulum::kTruncatedHashSize)
    {
        return false;
    }

    return buildEncryptedPacketForPeer(peer,
                                       lxmf_message + reticulum::kTruncatedHashSize,
                                       lxmf_message_len - reticulum::kTruncatedHashSize,
                                       out_packet,
                                       inout_len);
}

bool LxmfAdapter::buildGroupMessagePacket(
    const ReticulumPeerIdentity& destination,
    const uint8_t* packed_payload, size_t packed_payload_len,
    uint8_t* out_packet, size_t* inout_len,
    uint8_t out_message_hash[reticulum::kFullHashSize])
{
    if (!hasReticulumDestinationIdentity(destination) ||
        (!packed_payload && packed_payload_len != 0) ||
        !out_packet ||
        !inout_len ||
        !out_message_hash)
    {
        return false;
    }

    uint8_t signed_part[kSignedPartMaxLen] = {};
    size_t signed_part_len = sizeof(signed_part);
    if (!buildSignedPart(destination.destination_hash,
                         identity_.destinationHash(),
                         packed_payload,
                         packed_payload_len,
                         signed_part,
                         &signed_part_len,
                         out_message_hash))
    {
        return false;
    }

    uint8_t signature[reticulum::kSignatureSize] = {};
    if (!identity_.sign(signed_part, signed_part_len, signature))
    {
        return false;
    }

    uint8_t lxmf_message[kMaxLxmfMessageLen] = {};
    size_t lxmf_message_len = sizeof(lxmf_message);
    if (!packMessage(destination.destination_hash,
                     identity_.destinationHash(),
                     signature,
                     packed_payload,
                     packed_payload_len,
                     lxmf_message,
                     &lxmf_message_len))
    {
        return false;
    }

    if (lxmf_message_len <= reticulum::kTruncatedHashSize)
    {
        return false;
    }

    return reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                         reticulum::DestinationType::Group,
                                         reticulum::PacketContext::None,
                                         false,
                                         destination.destination_hash,
                                         lxmf_message + reticulum::kTruncatedHashSize,
                                         lxmf_message_len - reticulum::kTruncatedHashSize,
                                         out_packet,
                                         inout_len);
}

bool LxmfAdapter::encryptForPeer(const PeerInfo& peer,
                                 const uint8_t* plaintext,
                                 size_t plaintext_len,
                                 uint8_t* out_payload,
                                 size_t* inout_len)
{
    if ((!plaintext && plaintext_len != 0) || !out_payload || !inout_len)
    {
        return false;
    }

    uint8_t ephemeral_pub[LxmfIdentity::kEncPubKeySize] = {};
    uint8_t ephemeral_priv[LxmfIdentity::kEncPrivKeySize] = {};
    Curve25519::dh1(ephemeral_pub, ephemeral_priv);

    const bool use_ratchet = peerHasUsableRatchet(peer);
    const uint8_t* encryption_target = use_ratchet ? peer.ratchet_pub : peer.enc_pub;
    uint8_t shared_secret[LxmfIdentity::kEncPubKeySize] = {};
    memcpy(shared_secret, encryption_target, sizeof(shared_secret));
    if (!Curve25519::dh2(shared_secret, ephemeral_priv))
    {
        return false;
    }

    uint8_t derived_key[reticulum::kDerivedTokenKeySize] = {};
    if (!reticulum::hkdfSha256(shared_secret, sizeof(shared_secret),
                               peer.identity_hash, sizeof(peer.identity_hash),
                               nullptr, 0,
                               derived_key, sizeof(derived_key)))
    {
        return false;
    }

    uint8_t iv[reticulum::kTokenIvSize] = {};
    fillRandomBytes(iv, sizeof(iv));

    const size_t required_len =
        sizeof(ephemeral_pub) + reticulum::tokenSizeForPlaintext(plaintext_len);
    if (*inout_len < required_len)
    {
        *inout_len = required_len;
        return false;
    }

    size_t encrypted_token_len = *inout_len - sizeof(ephemeral_pub);
    if (!reticulum::tokenEncrypt(derived_key,
                                 iv,
                                 plaintext,
                                 plaintext_len,
                                 out_payload + sizeof(ephemeral_pub),
                                 &encrypted_token_len))
    {
        return false;
    }
    memcpy(out_payload, ephemeral_pub, sizeof(ephemeral_pub));
    *inout_len = sizeof(ephemeral_pub) + encrypted_token_len;

    char dest_hash[12] = {};
    char ratchet_id[12] = {};
    formatHashPrefix(peer.destination_hash, dest_hash, sizeof(dest_hash));
    formatRatchetIdPrefix(use_ratchet ? peer.ratchet_pub : nullptr,
                          ratchet_id,
                          sizeof(ratchet_id));
    Serial.printf("[LXMF][EncryptTX] dest=%s enc_target=%s ratchet=%u ratchet_id=%s plaintext_len=%u token_len=%u\n",
                  dest_hash,
                  use_ratchet ? "ratchet" : "identity",
                  use_ratchet ? 1U : 0U,
                  ratchet_id,
                  static_cast<unsigned>(plaintext_len),
                  static_cast<unsigned>(encrypted_token_len));

    return true;
}

bool LxmfAdapter::buildEncryptedPacketForPeer(const PeerInfo& peer,
                                              const uint8_t* plaintext, size_t plaintext_len,
                                              uint8_t* out_packet, size_t* inout_len)
{
    if ((!plaintext && plaintext_len != 0) || !out_packet || !inout_len)
    {
        return false;
    }

    std::memset(scratch_.encrypted_payload, 0, sizeof(scratch_.encrypted_payload));
    size_t payload_len = sizeof(scratch_.encrypted_payload);
    if (!encryptForPeer(peer,
                        plaintext,
                        plaintext_len,
                        scratch_.encrypted_payload,
                        &payload_len))
    {
        return false;
    }

    return reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                         reticulum::DestinationType::Single,
                                         reticulum::PacketContext::None,
                                         false,
                                         peer.destination_hash,
                                         scratch_.encrypted_payload,
                                         payload_len,
                                         out_packet,
                                         inout_len);
}

bool LxmfAdapter::routeAndSendPacket(const uint8_t* raw_packet, size_t raw_len,
                                     bool allow_transport,
                                     bool wifi_only)
{
    if (!raw_packet || raw_len == 0)
    {
        return false;
    }

    auto send_packet = [&](const uint8_t* data,
                           size_t len,
                           reticulum::interfaces::InterfaceId interface_id =
                               reticulum::interfaces::kInvalidInterfaceId) -> bool
    {
        if (interface_id != reticulum::interfaces::kInvalidInterfaceId)
        {
            return interfaces_.sendPacketOn(interface_id, data, len);
        }
        return wifi_only ? interfaces_.sendPacketWifiOnly(data, len)
                         : interfaces_.sendPacket(data, len);
    };

    if (!allow_transport)
    {
        return send_packet(raw_packet, raw_len);
    }

    reticulum::ParsedPacket parsed{};
    if (!reticulum::parsePacket(raw_packet, raw_len, &parsed) || !parsed.destination_hash)
    {
        return send_packet(raw_packet, raw_len);
    }

    if (parsed.packet_type == reticulum::PacketType::Announce ||
        parsed.packet_type == reticulum::PacketType::Proof ||
        parsed.destination_type == reticulum::DestinationType::Plain ||
        parsed.destination_type == reticulum::DestinationType::Group)
    {
        return send_packet(raw_packet, raw_len);
    }

    const PathEntry* path =
        path_manager_.findPath(parsed.destination_hash, millis(), kPathTtlMs);
    if (!path)
    {
        return send_packet(raw_packet, raw_len);
    }
    if (!interfaces_.isInterfaceSelected(path->interface_id))
    {
        return send_packet(raw_packet, raw_len);
    }
    if (path->hops <= 1 || path->direct)
    {
        return send_packet(raw_packet, raw_len, path->interface_id);
    }

    std::memset(scratch_.routed_packet, 0, sizeof(scratch_.routed_packet));
    size_t routed_len = sizeof(scratch_.routed_packet);
    if (!reticulum::buildHeader2Packet(parsed.packet_type,
                                       parsed.destination_type,
                                       static_cast<reticulum::PacketContext>(parsed.context),
                                       parsed.context_flag != 0,
                                       path->next_hop_transport,
                                       parsed.destination_hash,
                                       parsed.payload,
                                       parsed.payload_len,
                                       scratch_.routed_packet,
                                       &routed_len,
                                       raw_packet[1]))
    {
        return false;
    }

    return send_packet(scratch_.routed_packet, routed_len, path->interface_id);
}

bool LxmfAdapter::sendCachedAnnounceResponse(const PathEntry& path,
                                             reticulum::PacketContext context)
{
    if (path.cached_announce_len == 0)
    {
        return false;
    }

    reticulum::ParsedPacket parsed{};
    if (!reticulum::parsePacket(path.cached_announce, path.cached_announce_len, &parsed) ||
        parsed.packet_type != reticulum::PacketType::Announce)
    {
        return false;
    }
    if (isLoRaInterfaceId(active_ingress_interface_id_) &&
        isLxstTelephonyAnnouncePacket(parsed))
    {
        return true;
    }

    std::memset(scratch_.routed_packet, 0, sizeof(scratch_.routed_packet));
    size_t packet_len = sizeof(scratch_.routed_packet);
    if (!reticulum::buildHeader2Packet(reticulum::PacketType::Announce,
                                       reticulum::DestinationType::Single,
                                       context,
                                       parsed.context_flag != 0,
                                       identity_.identityHash(),
                                       parsed.destination_hash,
                                       parsed.payload,
                                       parsed.payload_len,
                                       scratch_.routed_packet,
                                       &packet_len,
                                       path.hops))
    {
        return false;
    }

    return active_ingress_interface_id_ !=
                   reticulum::interfaces::kInvalidInterfaceId
               ? interfaces_.sendPacketOn(active_ingress_interface_id_,
                                          scratch_.routed_packet,
                                          packet_len)
               : interfaces_.sendPacket(scratch_.routed_packet, packet_len);
}

bool LxmfAdapter::sendCachedPacketReplay(const uint8_t packet_hash[reticulum::kFullHashSize])
{
    if (!packet_hash)
    {
        return false;
    }

    bool sent = false;
    path_manager_.forEachPath(
        [this, packet_hash, &sent](const PathEntry& path)
        {
            if (sent || path.cached_announce_len == 0)
            {
                return;
            }
            if (!hashesEqual(path.cached_packet_hash,
                             packet_hash,
                             reticulum::kFullHashSize))
            {
                return;
            }
            sent = active_ingress_interface_id_ !=
                           reticulum::interfaces::kInvalidInterfaceId
                       ? interfaces_.sendPacketOn(active_ingress_interface_id_,
                                                  path.cached_announce,
                                                  path.cached_announce_len)
                       : interfaces_.sendPacket(path.cached_announce,
                                                path.cached_announce_len);
        });

    return sent;
}

bool LxmfAdapter::shouldProcessWifiIngressPacket(const reticulum::ParsedPacket& packet,
                                                 const RuntimeBudget& budget)
{
    if (!packet.destination_hash)
    {
        return false;
    }

    if (isLocalDestinationHash(packet.destination_hash, nullptr))
    {
        return true;
    }

    if (packet.transport_id &&
        hashesEqual(packet.transport_id, identity_.identityHash(), reticulum::kTruncatedHashSize))
    {
        return true;
    }

    if (packet.destination_type == reticulum::DestinationType::Group &&
        findConfiguredGroupDestination(packet.destination_hash))
    {
        return true;
    }

    if (packet.destination_type == reticulum::DestinationType::Link &&
        findLinkSession(packet.destination_hash))
    {
        return true;
    }

    if (packet.packet_type == reticulum::PacketType::Proof &&
        (path_manager_.findReversePath(packet.destination_hash) ||
         path_manager_.findPendingPingReceipt(packet.destination_hash)))
    {
        return true;
    }

    if (packet.packet_type == reticulum::PacketType::Data &&
        packet.destination_type == reticulum::DestinationType::Plain &&
        packet.payload &&
        packet.payload_len > reticulum::kTruncatedHashSize)
    {
        uint8_t control_hash[reticulum::kTruncatedHashSize] = {};
        pathRequestDestinationHash(control_hash);
        if (!hashesEqual(packet.destination_hash,
                         control_hash,
                         sizeof(control_hash)))
        {
            return false;
        }

        const uint8_t* requested_hash = packet.payload;
        LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
        const bool requested_local =
            isLocalDestinationHash(requested_hash, &local_kind);
        if (requested_local || findConfiguredGroupDestination(requested_hash))
        {
            char requested_prefix[12] = {};
            formatHashPrefix(requested_hash,
                             requested_prefix,
                             sizeof(requested_prefix));
            Serial.printf("[LXMF][PathRX] admit requested=%s local=%u kind=%u phase=%s\n",
                          requested_prefix,
                          requested_local ? 1U : 0U,
                          static_cast<unsigned>(local_kind),
                          budget.phase ? budget.phase : "-");
            return true;
        }
        return false;
    }

    if (packet.packet_type == reticulum::PacketType::Announce)
    {
        if (packet.context == static_cast<uint8_t>(reticulum::PacketContext::PathResponse) &&
            path_manager_.findPendingPathRequest(packet.destination_hash))
        {
            return true;
        }
        return budget.allow_public_discovery ||
               (screen_runtime::is_sleeping() &&
                !screen_runtime::is_saver_active());
    }

    return false;
}

bool LxmfAdapter::shouldLogRxDetail(
    const reticulum::ParsedPacket& packet,
    reticulum::interfaces::InterfaceKind ingress_interface,
    const RuntimeBudget& budget)
{
    if (ingress_interface != reticulum::interfaces::InterfaceKind::WifiGateway)
    {
        if (!isPublicDiscoveryPacket(packet))
        {
            return true;
        }
        const uint32_t now_ms = millis();
        const uint32_t interval_ms =
            budget.allow_persistence
                ? kLoraDiscoverySleepDetailLogIntervalMs
                : kLoraDiscoveryForegroundDetailLogIntervalMs;
        return rx_telemetry_.shouldLogLoraDiscoveryDetail(now_ms,
                                                          interval_ms,
                                                          budget.phase);
    }
    if (!packet.destination_hash)
    {
        return false;
    }
    if (isLocalDestinationHash(packet.destination_hash, nullptr))
    {
        return true;
    }
    if (packet.destination_type == reticulum::DestinationType::Group &&
        findConfiguredGroupDestination(packet.destination_hash))
    {
        return true;
    }
    return packet.destination_type == reticulum::DestinationType::Link;
}

bool LxmfAdapter::consumeDiscoveryBudget(
    reticulum::interfaces::InterfaceKind ingress_interface)
{
    return rx_telemetry_.consumeDiscoveryBudget(ingress_interface,
                                                millis(),
                                                kDiscoverySampleIntervalMs);
}

bool LxmfAdapter::isForegroundDiscoveryDestination(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const
{
    if (!destination_hash)
    {
        return false;
    }

    bool pending_page = false;
    network_page_client_.forEach(
        [destination_hash, &pending_page](const PendingNomadPageRequest& request)
        {
            if (pending_page)
            {
                return;
            }
            pending_page = hashesEqual(request.destination_hash,
                                       destination_hash,
                                       reticulum::kTruncatedHashSize);
        });
    if (pending_page)
    {
        return true;
    }

    bool foreground = false;
    link_manager_.forEachSession(
        [destination_hash, &foreground](const LinkSession& session)
        {
            if (foreground ||
                session.state == LinkState::Closed ||
                (session.destination != LocalDestinationKind::CallAudio &&
                 session.destination != LocalDestinationKind::NomadPage))
            {
                return;
            }
            foreground = hashesEqual(session.remote_destination_hash,
                                     destination_hash,
                                     reticulum::kTruncatedHashSize);
        });

    return foreground;
}

void LxmfAdapter::noteRxSummary(bool wifi_skipped,
                                bool duplicate,
                                bool parse_failed,
                                bool deferred,
                                bool deferred_dropped,
                                bool throttled_discovery)
{
    rx_telemetry_.noteSummary(wifi_skipped,
                              duplicate,
                              parse_failed,
                              deferred,
                              deferred_dropped,
                              throttled_discovery,
                              millis(),
                              kRxSummaryIntervalMs);
}

bool LxmfAdapter::shouldRebroadcastAnnounce(
    const reticulum::ParsedPacket& packet,
    reticulum::interfaces::InterfaceKind ingress_interface) const
{
    (void)packet;
    (void)ingress_interface;
    return false;
}

bool LxmfAdapter::rebroadcastAnnounce(const PathEntry& path, const reticulum::ParsedPacket& packet)
{
    if (!packet.destination_hash || path.cached_announce_len == 0)
    {
        return false;
    }

    const uint32_t now_ms = millis();
    if (!announce_scheduler_.rebroadcastDue(now_ms,
                                            kAnnounceRebroadcastIntervalMs))
    {
        return false;
    }

    std::memset(scratch_.forward_packet, 0, sizeof(scratch_.forward_packet));
    size_t rebroadcast_len = sizeof(scratch_.forward_packet);
    if (!reticulum::buildHeader2Packet(reticulum::PacketType::Announce,
                                       reticulum::DestinationType::Single,
                                       reticulum::PacketContext::None,
                                       packet.context_flag != 0,
                                       identity_.identityHash(),
                                       packet.destination_hash,
                                       packet.payload,
                                       packet.payload_len,
                                       scratch_.forward_packet,
                                       &rebroadcast_len,
                                       packet.hops))
    {
        return false;
    }

    return interfaces_.sendPacket(scratch_.forward_packet, rebroadcast_len);
}

void LxmfAdapter::cullTransportState()
{
    const uint32_t now_ms = millis();
    path_manager_.forEachPendingPingReceipt(
        [now_ms](const runtime::PendingPingReceipt& receipt)
        {
            if (receipt.created_ms == 0 ||
                (now_ms - receipt.created_ms) <= kPendingPingReceiptTtlMs)
            {
                return;
            }
            char destination_hash[12] = {};
            formatHashPrefix(receipt.destination_hash,
                             destination_hash,
                             sizeof(destination_hash));
            Serial.printf("[LXMF][PingRX] timeout dest=%s elapsed_ms=%lu\n",
                          destination_hash,
                          static_cast<unsigned long>(now_ms - receipt.created_ms));
            sys::EventBus::publish(
                new sys::ReticulumPingResultEvent(
                    receipt.destination_hash,
                    sys::ReticulumPingResult::Timeout,
                    now_ms - receipt.created_ms),
                100);
        });

    delivery_attempt_ledger_.forEachReceipt(
        [now_ms](const runtime::DeliveryAttemptReceipt& receipt)
        {
            if (receipt.kind != runtime::DeliveryAttemptKind::DirectPacket)
            {
                return;
            }
            if (receipt.created_ms == 0 ||
                (now_ms - receipt.created_ms) <=
                    kPendingDeliveryReceiptTtlMs)
            {
                return;
            }
            char destination_hash[12] = {};
            formatHashPrefix(receipt.destination_hash,
                             destination_hash,
                             sizeof(destination_hash));
            Serial.printf("[LXMF][DirectTX] proof_timeout msg=%lu dest=%s elapsed_ms=%lu status=sent\n",
                          static_cast<unsigned long>(receipt.message_id),
                          destination_hash,
                          static_cast<unsigned long>(now_ms -
                                                     receipt.created_ms));
        });
    delivery_attempt_ledger_.cull(runtime::DeliveryAttemptKind::DirectPacket,
                                  now_ms,
                                  kPendingDeliveryReceiptTtlMs,
                                  kMaxPendingDeliveryReceipts);

    const runtime::TransportRuntimeLimits limits{
        kMaxPaths,
        kMaxPacketFilter,
        kMaxReverseEntries,
        kMaxLinkRelays,
        kMaxPendingPathRequests,
        kPacketFilterTtlMs,
        kPendingPathRequestTtlMs,
        kReverseEntryTtlMs,
        kLinkRelayTtlMs,
        kMaxPendingPingReceipts,
        kPathTtlMs,
        kPendingPingReceiptTtlMs};
    path_manager_.cull(now_ms, limits);
    cullLinkSessions();
}

void LxmfAdapter::localDestinationHash(LocalDestinationKind kind,
                                       uint8_t out_hash[reticulum::kTruncatedHashSize]) const
{
    if (!out_hash)
    {
        return;
    }

    uint8_t name_hash[reticulum::kNameHashSize] = {};
    if (kind == LocalDestinationKind::Propagation)
    {
        reticulum::computeNameHash("lxmf", "propagation", name_hash);
    }
    else if (kind == LocalDestinationKind::CallAudio)
    {
        reticulum::computeNameHash("lxst", "telephony", name_hash);
    }
    else
    {
        reticulum::computeNameHash("lxmf", "delivery", name_hash);
    }
    reticulum::computeDestinationHash(name_hash, identity_.identityHash(), out_hash);
}

bool LxmfAdapter::isLocalDestinationHash(const uint8_t hash[reticulum::kTruncatedHashSize],
                                         LocalDestinationKind* out_kind) const
{
    if (!hash)
    {
        return false;
    }

    uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
    localDestinationHash(LocalDestinationKind::Delivery, delivery_hash);
    if (hashesEqual(hash, delivery_hash, sizeof(delivery_hash)))
    {
        if (out_kind)
        {
            *out_kind = LocalDestinationKind::Delivery;
        }
        return true;
    }

    uint8_t propagation_hash[reticulum::kTruncatedHashSize] = {};
    localDestinationHash(LocalDestinationKind::Propagation, propagation_hash);
    if (rtnet::active().propagation.service_enabled &&
        hashesEqual(hash, propagation_hash, sizeof(propagation_hash)))
    {
        if (out_kind)
        {
            *out_kind = LocalDestinationKind::Propagation;
        }
        return true;
    }

    uint8_t call_audio_hash[reticulum::kTruncatedHashSize] = {};
    localDestinationHash(LocalDestinationKind::CallAudio, call_audio_hash);
    if (hashesEqual(hash, call_audio_hash, sizeof(call_audio_hash)))
    {
        if (out_kind)
        {
            *out_kind = LocalDestinationKind::CallAudio;
        }
        return true;
    }

    return false;
}

uint16_t LxmfAdapter::linkMduForMtu(uint16_t mtu)
{
    const size_t encrypted_payload_budget =
        (mtu > reticulum::kPacketHeader1Size) ? (mtu - reticulum::kPacketHeader1Size) : 0;
    if (encrypted_payload_budget <= reticulum::kTokenOverhead)
    {
        return 0;
    }

    const size_t token_budget = encrypted_payload_budget - reticulum::kTokenOverhead;
    const size_t padded = (token_budget / 16U) * 16U;
    if (padded == 0)
    {
        return 0;
    }
    return static_cast<uint16_t>(padded - 1U);
}

bool LxmfAdapter::generateLinkSigningKey(uint8_t out_pub[LxmfIdentity::kSigPubKeySize],
                                         uint8_t out_priv[LxmfIdentity::kSigPrivKeySize])
{
    if (!out_pub || !out_priv)
    {
        return false;
    }

    uint8_t seed[32] = {};
    fillRandomBytes(seed, sizeof(seed));
    ed25519_create_keypair(out_pub, out_priv, seed);
    memset(seed, 0, sizeof(seed));
    return !isZeroBytes(out_pub, LxmfIdentity::kSigPubKeySize) &&
           !isZeroBytes(out_priv, LxmfIdentity::kSigPrivKeySize);
}

bool LxmfAdapter::signWithKey(const uint8_t sign_pub[LxmfIdentity::kSigPubKeySize],
                              const uint8_t sign_priv[LxmfIdentity::kSigPrivKeySize],
                              const uint8_t* message,
                              size_t message_len,
                              uint8_t out_signature[reticulum::kSignatureSize])
{
    if (!sign_pub || !sign_priv || !message || !out_signature)
    {
        return false;
    }

    ed25519_sign(out_signature, message, message_len, sign_pub, sign_priv);
    return true;
}

bool LxmfAdapter::deriveLinkKey(LinkSession& session)
{
    if (isZeroBytes(session.peer_enc_pub, sizeof(session.peer_enc_pub)) ||
        isZeroBytes(session.local_enc_priv, sizeof(session.local_enc_priv)))
    {
        return false;
    }

    uint8_t shared_secret[LxmfIdentity::kEncPubKeySize] = {};
    memcpy(shared_secret, session.peer_enc_pub, sizeof(shared_secret));
    uint8_t local_priv[LxmfIdentity::kEncPrivKeySize] = {};
    memcpy(local_priv, session.local_enc_priv, sizeof(local_priv));
    if (!Curve25519::dh2(shared_secret, local_priv))
    {
        return false;
    }

    return reticulum::hkdfSha256(shared_secret, sizeof(shared_secret),
                                 session.link_id, sizeof(session.link_id),
                                 nullptr, 0,
                                 session.derived_key, sizeof(session.derived_key));
}

bool LxmfAdapter::encryptLinkPayload(const LinkSession& session,
                                     const uint8_t* plaintext, size_t plaintext_len,
                                     uint8_t* out_payload, size_t* inout_len) const
{
    if (!plaintext || plaintext_len == 0 || !out_payload || !inout_len)
    {
        return false;
    }

    uint8_t iv[reticulum::kTokenIvSize] = {};
    fillRandomBytes(iv, sizeof(iv));
    return reticulum::tokenEncrypt(session.derived_key,
                                   iv,
                                   plaintext,
                                   plaintext_len,
                                   out_payload,
                                   inout_len);
}

bool LxmfAdapter::decryptLinkPayload(const LinkSession& session,
                                     const uint8_t* payload, size_t payload_len,
                                     runtime::ResourcePayloadBuffer* out_plaintext) const
{
    if (!payload || payload_len == 0 || !out_plaintext)
    {
        return false;
    }

    runtime::ResourcePayloadBuffer plaintext(
        reticulum::paddedTokenPlaintextSize(payload_len),
        0);
    size_t plaintext_len = plaintext.size();
    if (!reticulum::tokenDecrypt(session.derived_key,
                                 payload,
                                 payload_len,
                                 plaintext.data(),
                                 &plaintext_len))
    {
        return false;
    }

    plaintext.resize(plaintext_len);
    *out_plaintext = std::move(plaintext);
    return true;
}

bool LxmfAdapter::sendLinkPacket(LinkSession& session,
                                 reticulum::PacketType packet_type,
                                 reticulum::PacketContext context,
                                 const uint8_t* payload, size_t payload_len,
                                 bool encrypt_payload,
                                 bool call_admission_control,
                                 uint8_t out_packet_hash[reticulum::kFullHashSize])
{
    if (!isReady() || (!payload && payload_len != 0))
    {
        return false;
    }

    const uint8_t* effective_payload = payload;
    size_t effective_payload_len = payload_len;
    if (encrypt_payload)
    {
        std::memset(scratch_.link_wire_payload,
                    0,
                    sizeof(scratch_.link_wire_payload));
        effective_payload = scratch_.link_wire_payload;
        effective_payload_len = sizeof(scratch_.link_wire_payload);
        if (!encryptLinkPayload(session,
                                payload,
                                payload_len,
                                scratch_.link_wire_payload,
                                &effective_payload_len))
        {
            return false;
        }
    }

    std::memset(scratch_.link_packet, 0, sizeof(scratch_.link_packet));
    size_t packet_len = sizeof(scratch_.link_packet);
    if (!reticulum::buildHeader1Packet(packet_type,
                                       reticulum::DestinationType::Link,
                                       context,
                                       false,
                                       session.link_id,
                                       effective_payload,
                                       effective_payload_len,
                                       scratch_.link_packet,
                                       &packet_len))
    {
        return false;
    }

    const bool has_bound_interface =
        session.interface_id != reticulum::interfaces::kInvalidInterfaceId;
    const bool ok = has_bound_interface
                        ? interfaces_.sendPacketOn(session.interface_id,
                                                   scratch_.link_packet,
                                                   packet_len,
                                                   session.destination ==
                                                           LocalDestinationKind::CallAudio
                                                       ? session.link_id
                                                       : nullptr,
                                                   call_admission_control)
                        : (session.destination == LocalDestinationKind::CallAudio
                               ? interfaces_.sendPacketWifiOnly(scratch_.link_packet,
                                                                packet_len,
                                                                session.link_id,
                                                                call_admission_control)
                               : interfaces_.sendPacket(scratch_.link_packet,
                                                        packet_len));
    if (ok)
    {
        link_manager_.touchOutbound(session, millis());
        if (out_packet_hash)
        {
            reticulum::computePacketHash(scratch_.link_packet,
                                         packet_len,
                                         out_packet_hash);
        }
    }
    return ok;
}

bool LxmfAdapter::sendNomadPageRequestPacket(
    LinkSession& session,
    PendingNomadPageRequest& request)
{
    if (!isReady() || session.state != LinkState::Active || !request.path[0])
    {
        return false;
    }

    uint8_t path_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::truncatedHash(reinterpret_cast<const uint8_t*>(request.path),
                             std::strlen(request.path),
                             path_hash);

    std::memset(scratch_.nomad_page_request_payload,
                0,
                sizeof(scratch_.nomad_page_request_payload));
    size_t request_payload_len = sizeof(scratch_.nomad_page_request_payload);
    if (!encodeLinkRequestPayload(static_cast<double>(currentTimestampSeconds()),
                                  path_hash,
                                  request.request_data,
                                  request.request_data_len,
                                  request.request_data_len == 0,
                                  scratch_.nomad_page_request_payload,
                                  &request_payload_len) ||
        request_payload_len > session.mdu)
    {
        return false;
    }

    std::memset(scratch_.nomad_page_wire_payload,
                0,
                sizeof(scratch_.nomad_page_wire_payload));
    size_t wire_payload_len = sizeof(scratch_.nomad_page_wire_payload);
    if (!encryptLinkPayload(session,
                            scratch_.nomad_page_request_payload,
                            request_payload_len,
                            scratch_.nomad_page_wire_payload,
                            &wire_payload_len))
    {
        return false;
    }

    std::memset(scratch_.nomad_page_packet,
                0,
                sizeof(scratch_.nomad_page_packet));
    size_t packet_len = sizeof(scratch_.nomad_page_packet);
    if (!reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                       reticulum::DestinationType::Link,
                                       reticulum::PacketContext::Request,
                                       false,
                                       session.link_id,
                                       scratch_.nomad_page_wire_payload,
                                       wire_payload_len,
                                       scratch_.nomad_page_packet,
                                       &packet_len))
    {
        return false;
    }

    uint8_t request_id[reticulum::kTruncatedHashSize] = {};
    reticulum::computeTruncatedPacketHash(scratch_.nomad_page_packet,
                                          packet_len,
                                          request_id);
    const bool ok =
        session.interface_id != reticulum::interfaces::kInvalidInterfaceId
            ? interfaces_.sendPacketOn(session.interface_id,
                                       scratch_.nomad_page_packet,
                                       packet_len)
            : interfaces_.sendPacket(scratch_.nomad_page_packet, packet_len);
    if (!ok)
    {
        return false;
    }

    link_manager_.queuePendingRequest(session,
                                      request_id,
                                      sizeof(request_id),
                                      millis(),
                                      false);
    link_manager_.touchOutbound(session, millis());
    return network_page_client_.noteRequestPacketSent(request,
                                                      request_id,
                                                      sizeof(request_id),
                                                      millis());
}

LxmfAdapter::PendingNomadPageRequest*
LxmfAdapter::findPendingNomadPageRequestById(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const uint8_t* request_id,
    std::size_t request_id_len)
{
    return network_page_client_.findByRequestId(destination_hash,
                                                request_id,
                                                request_id_len);
}

void LxmfAdapter::updateNomadPageProgress(
    const PendingNomadPageRequest& request,
    int progress_percent,
    const char* message,
    const char* detail,
    bool active,
    bool complete,
    PageFailureKind failure)
{
    char destination_text[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    formatHashHex(request.destination_hash,
                  reticulum::kTruncatedHashSize,
                  destination_text,
                  sizeof(destination_text));
    rtpage::update_request_progress(destination_text,
                                    request.path,
                                    progress_percent,
                                    message,
                                    detail,
                                    active,
                                    complete,
                                    failure);
}

void LxmfAdapter::updateNomadPageProgressForDestination(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    int progress_percent,
    const char* message,
    const char* detail,
    bool active,
    bool complete,
    PageFailureKind failure)
{
    if (!destination_hash)
    {
        return;
    }

    network_page_client_.forEach(
        [this,
         destination_hash,
         progress_percent,
         message,
         detail,
         active,
         complete,
         failure](const PendingNomadPageRequest& request)
        {
            if (hashesEqual(request.destination_hash,
                            destination_hash,
                            reticulum::kTruncatedHashSize))
            {
                updateNomadPageProgress(request,
                                        progress_percent,
                                        message,
                                        detail,
                                        active,
                                        complete,
                                        failure);
            }
        });
}

void LxmfAdapter::completeNomadPageRequest(
    PendingNomadPageRequest& request,
    const runtime::ResourcePayloadBuffer& packed_response)
{
    char destination_text[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    formatHashHex(request.destination_hash,
                  reticulum::kTruncatedHashSize,
                  destination_text,
                  sizeof(destination_text));

    runtime::ResourcePayloadBuffer page_body;
    if (!decodeMsgpackByteString(packed_response.data(),
                                 packed_response.size(),
                                 &page_body))
    {
        updateNomadPageProgress(request,
                                100,
                                "Nomad page response decode failed",
                                request.path,
                                false,
                                false,
                                PageFailureKind::Terminal);
        LXMF_NOMAD_PAGE_LOG("response decode failed dest=%s path=%s packed=%u\n",
                            destination_text,
                            request.path,
                            static_cast<unsigned>(packed_response.size()));
        return;
    }

    const char* body =
        page_body.empty() ? "" : reinterpret_cast<const char*>(page_body.data());
    updateNomadPageProgress(request,
                            95,
                            "Caching Nomad page",
                            request.path,
                            true,
                            false,
                            PageFailureKind::None);
    const rtpage::Status status =
        rtpage::store_cached_page_now(destination_text,
                                      request.path,
                                      body,
                                      page_body.size());
    updateNomadPageProgress(request,
                            status.saved ? 100 : 95,
                            status.saved ? "Nomad page loaded"
                                         : "Nomad page cache write failed",
                            status.detail,
                            false,
                            status.saved,
                            status.saved ? PageFailureKind::None
                                         : PageFailureKind::Terminal);
    LXMF_NOMAD_PAGE_LOG("response store dest=%s path=%s body=%u saved=%u sd=%u file=%u msg=\"%s\" detail=\"%s\"\n",
                        destination_text,
                        request.path,
                        static_cast<unsigned>(page_body.size()),
                        status.saved ? 1U : 0U,
                        status.sd_present ? 1U : 0U,
                        status.file_present ? 1U : 0U,
                        status.message,
                        status.detail);
}

void LxmfAdapter::pumpNomadPageRequests()
{
    const uint32_t now_ms = millis();
    for (std::size_t index = 0; index < network_page_client_.size();)
    {
        PendingNomadPageRequest* current_request = network_page_client_.at(index);
        if (!current_request)
        {
            break;
        }
        PendingNomadPageRequest& request = *current_request;

        if (request.created_ms != 0 &&
            (now_ms - request.created_ms) > kNomadPageRequestTtlMs)
        {
            LinkSession* open_link =
                link_manager_.findOpenSessionByDestination(
                    request.destination_hash,
                    LocalDestinationKind::NomadPage);
            char timeout_destination[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
            char timeout_link[12] = {};
            formatHashHex(request.destination_hash,
                          reticulum::kTruncatedHashSize,
                          timeout_destination,
                          sizeof(timeout_destination));
            formatHashPrefix(open_link ? open_link->link_id : nullptr,
                             timeout_link,
                             sizeof(timeout_link));
            Serial.printf("[LXMF][NomadPage] timeout dest=%s path=%s sent=%u link_started=%u open_link=%u state=%s link=%s expected_hops=%u last_attempt_age_ms=%lu\n",
                          timeout_destination,
                          request.path,
                          request.request_sent ? 1U : 0U,
                          request.link_started ? 1U : 0U,
                          open_link ? 1U : 0U,
                          open_link ? linkStateLabel(open_link->state) : "-",
                          timeout_link,
                          static_cast<unsigned>(open_link ? open_link->expected_hops : 0U),
                          static_cast<unsigned long>(
                              network_page_client_.lastAttemptAge(request,
                                                                  now_ms)));
            updateNomadPageProgress(request,
                                    100,
                                    "Nomad page request timed out",
                                    request.path,
                                    false,
                                    false,
                                    PageFailureKind::Terminal);
            if (kNomadPageTraceEnabled)
            {
                char destination_text[(reticulum::kTruncatedHashSize * 2U) +
                                      1U] = {};
                formatHashHex(request.destination_hash,
                              reticulum::kTruncatedHashSize,
                              destination_text,
                              sizeof(destination_text));
                LXMF_NOMAD_PAGE_LOG("timeout dest=%s path=%s sent=%u link=%u\n",
                                    destination_text,
                                    request.path,
                                    request.request_sent ? 1U : 0U,
                                    request.link_started ? 1U : 0U);
            }
            network_page_client_.eraseAt(index);
            continue;
        }

        bool completed = false;
        if (request.request_sent)
        {
            if (LinkSession* session =
                    findActiveLinkSessionByDestination(request.destination_hash,
                                                       LocalDestinationKind::NomadPage))
            {
                LinkPendingRequest* pending =
                    link_manager_.findPendingRequest(*session,
                                                     request.request_id,
                                                     sizeof(request.request_id));
                if (pending && pending->response_ready)
                {
                    completeNomadPageRequest(request, pending->response);
                    link_manager_.erasePendingRequest(*session, *pending);
                    completed = true;
                }
            }
        }

        if (completed)
        {
            network_page_client_.eraseAt(index);
            continue;
        }

        LinkSession* active_link =
            findActiveLinkSessionByDestination(request.destination_hash,
                                               LocalDestinationKind::NomadPage);
        if (active_link && !request.request_sent &&
            network_page_client_.attemptDue(request,
                                            now_ms,
                                            kNomadPageSendRetryMs))
        {
            network_page_client_.noteAttempt(request, now_ms);
            const bool sent = sendNomadPageRequestPacket(*active_link, request);
            updateNomadPageProgress(request,
                                    sent ? 40 : 35,
                                    sent ? "Nomad page request sent"
                                         : "Nomad page request TX failed",
                                    request.path,
                                    sent,
                                    false,
                                    sent ? PageFailureKind::None
                                         : PageFailureKind::Retryable);
            if (kNomadPageTraceEnabled)
            {
                char destination_text[(reticulum::kTruncatedHashSize * 2U) +
                                      1U] = {};
                formatHashHex(request.destination_hash,
                              reticulum::kTruncatedHashSize,
                              destination_text,
                              sizeof(destination_text));
                LXMF_NOMAD_PAGE_LOG("request tx dest=%s path=%s ok=%u pending_link_requests=%u\n",
                                    destination_text,
                                    request.path,
                                    sent ? 1U : 0U,
                                    static_cast<unsigned>(
                                        link_manager_.pendingRequestCount(*active_link)));
            }
        }
        else if (!active_link)
        {
            const PathEntry* path = path_manager_.findPath(
                request.destination_hash, millis(), kPathTtlMs);
            if (!path)
            {
                if (network_page_client_.pathRequestDue(
                        request,
                        now_ms,
                        kPathRequestMinIntervalMs))
                {
                    const bool path_sent =
                        sendPathRequestForDestination(request.destination_hash);
                    network_page_client_.notePathRequest(request,
                                                         path_sent,
                                                         now_ms);
                    updateNomadPageProgress(request,
                                            path_sent ? 10 : 5,
                                            path_sent ? "Resolving Nomad page path"
                                                      : "Nomad page path request failed",
                                            request.path,
                                            true,
                                            false,
                                            path_sent ? PageFailureKind::None
                                                      : PageFailureKind::Retryable);
                    if (kNomadPageTraceEnabled)
                    {
                        char destination_text[(reticulum::kTruncatedHashSize *
                                               2U) +
                                              1U] = {};
                        formatHashHex(request.destination_hash,
                                      reticulum::kTruncatedHashSize,
                                      destination_text,
                                      sizeof(destination_text));
                        LXMF_NOMAD_PAGE_LOG("path request dest=%s path=%s ok=%u\n",
                                            destination_text,
                                            request.path,
                                            path_sent ? 1U : 0U);
                    }
                }
            }
            else
            {
                LinkSession* open_link =
                    link_manager_.findOpenSessionByDestination(
                        request.destination_hash,
                        LocalDestinationKind::NomadPage);
                if (open_link &&
                    (open_link->state == LinkState::Pending ||
                     open_link->state == LinkState::Handshake) &&
                    network_page_client_.attemptDue(request,
                                                    now_ms,
                                                    kNomadPageLinkRetryMs))
                {
                    const bool link_sent = sendLinkRequest(*open_link);
                    network_page_client_.noteLinkStart(request,
                                                       link_sent,
                                                       now_ms,
                                                       true);
                    updateNomadPageProgress(request,
                                            link_sent ? 25 : 10,
                                            link_sent
                                                ? "Retrying Nomad page link"
                                                : "Nomad page link retry failed",
                                            request.path,
                                            link_sent,
                                            false,
                                            link_sent ? PageFailureKind::None
                                                      : PageFailureKind::Retryable);
                    char destination_text[(reticulum::kTruncatedHashSize * 2U) +
                                          1U] = {};
                    char link_hash[12] = {};
                    formatHashHex(request.destination_hash,
                                  reticulum::kTruncatedHashSize,
                                  destination_text,
                                  sizeof(destination_text));
                    formatHashPrefix(open_link->link_id,
                                     link_hash,
                                     sizeof(link_hash));
                    Serial.printf("[LXMF][NomadPage] link_retry dest=%s path=%s link=%s state=%s ok=%u age_ms=%lu\n",
                                  destination_text,
                                  request.path,
                                  link_hash,
                                  linkStateLabel(open_link->state),
                                  link_sent ? 1U : 0U,
                                  static_cast<unsigned long>(
                                      now_ms - open_link->created_ms));
                }
                else if (!open_link &&
                         network_page_client_.attemptDue(
                             request,
                             now_ms,
                             kNomadPageSendRetryMs))
                {
                    runtime::LinkSessionSpec session_spec{};
                    session_spec.now_ms = now_ms;
                    session_spec.keepalive_interval_ms = kLinkKeepaliveMaxMs;
                    session_spec.stale_timeout_ms = kLinkKeepaliveMaxMs * 2U;
                    session_spec.remote_destination_hash =
                        request.destination_hash;
                    session_spec.expected_hops = path->hops;
                    session_spec.destination = LocalDestinationKind::NomadPage;
                    session_spec.state = LinkState::Pending;
                    session_spec.initiator = true;
                    LinkSession* new_session =
                        link_manager_.openSession(kMaxLinkSessions,
                                                  session_spec);
                    if (!new_session)
                    {
                        updateNomadPageProgress(request,
                                                10,
                                                "Nomad page link start failed",
                                                request.path,
                                                false,
                                                false,
                                                PageFailureKind::Retryable);
                        ++index;
                        continue;
                    }
                    LinkSession& session = *new_session;

                    Curve25519::dh1(session.local_enc_pub,
                                    session.local_enc_priv);
                    const bool link_sent =
                        !isZeroBytes(session.local_enc_priv,
                                     sizeof(session.local_enc_priv)) &&
                        generateLinkSigningKey(session.local_sig_pub,
                                               session.local_sig_priv) &&
                        sendLinkRequest(session);
                    if (!link_sent)
                    {
                        link_manager_.discardSession(session);
                    }
                    network_page_client_.noteLinkStart(request,
                                                       link_sent,
                                                       now_ms,
                                                       false);
                    updateNomadPageProgress(request,
                                            link_sent ? 25 : 10,
                                            link_sent ? "Opening Nomad page link"
                                                      : "Nomad page link start failed",
                                            request.path,
                                            link_sent,
                                            false,
                                            link_sent ? PageFailureKind::None
                                                      : PageFailureKind::Retryable);
                    if (kNomadPageTraceEnabled)
                    {
                        char destination_text[(reticulum::kTruncatedHashSize *
                                               2U) +
                                              1U] = {};
                        formatHashHex(request.destination_hash,
                                      reticulum::kTruncatedHashSize,
                                      destination_text,
                                      sizeof(destination_text));
                        LXMF_NOMAD_PAGE_LOG("link start dest=%s path=%s ok=%u hops=%u\n",
                                            destination_text,
                                            request.path,
                                            link_sent ? 1U : 0U,
                                            static_cast<unsigned>(path->hops));
                    }
                }
            }
        }

        ++index;
    }
}

bool LxmfAdapter::sendLinkHandshakeProof(LinkSession& session,
                                         bool call_admission_control)
{
    uint8_t signalling[kLinkSignallingLen] = {};
    buildLinkSignallingBytes(session.mtu, signalling);

    uint8_t signed_data[reticulum::kTruncatedHashSize +
                        LxmfIdentity::kEncPubKeySize +
                        LxmfIdentity::kSigPubKeySize +
                        kLinkSignallingLen] = {};
    size_t used = 0;
    memcpy(signed_data + used, session.link_id, reticulum::kTruncatedHashSize);
    used += reticulum::kTruncatedHashSize;
    memcpy(signed_data + used, session.local_enc_pub, LxmfIdentity::kEncPubKeySize);
    used += LxmfIdentity::kEncPubKeySize;
    memcpy(signed_data + used, identity_.signingPublicKey(), LxmfIdentity::kSigPubKeySize);
    used += LxmfIdentity::kSigPubKeySize;
    memcpy(signed_data + used, signalling, sizeof(signalling));
    used += sizeof(signalling);

    uint8_t proof_payload[reticulum::kSignatureSize +
                          LxmfIdentity::kEncPubKeySize +
                          kLinkSignallingLen] = {};
    if (!identity_.sign(signed_data, used, proof_payload))
    {
        return false;
    }
    memcpy(proof_payload + reticulum::kSignatureSize,
           session.local_enc_pub,
           LxmfIdentity::kEncPubKeySize);
    memcpy(proof_payload + reticulum::kSignatureSize + LxmfIdentity::kEncPubKeySize,
           signalling,
           sizeof(signalling));

    return sendLinkPacket(session,
                          reticulum::PacketType::Proof,
                          reticulum::PacketContext::LrProof,
                          proof_payload,
                          sizeof(proof_payload),
                          false,
                          call_admission_control);
}

bool LxmfAdapter::sendLinkRtt(LinkSession& session)
{
    uint8_t payload[16] = {};
    size_t payload_len = sizeof(payload);
    if (!packFloat64(static_cast<double>(session.rtt_s), payload, &payload_len))
    {
        return false;
    }
    return sendLinkPacket(session,
                          reticulum::PacketType::Data,
                          reticulum::PacketContext::LrRtt,
                          payload,
                          payload_len,
                          true);
}

bool LxmfAdapter::sendLinkKeepalive(LinkSession& session)
{
    const uint8_t probe = 0xFF;
    const bool ok = sendLinkPacket(session,
                                   reticulum::PacketType::Data,
                                   reticulum::PacketContext::Keepalive,
                                   &probe,
                                   sizeof(probe),
                                   false);
    if (ok)
    {
        link_manager_.noteKeepaliveSent(session, millis());
    }
    return ok;
}

bool LxmfAdapter::sendLinkKeepaliveAck(LinkSession& session)
{
    const uint8_t ack = 0xFE;
    return sendLinkPacket(session,
                          reticulum::PacketType::Data,
                          reticulum::PacketContext::Keepalive,
                          &ack,
                          sizeof(ack),
                          false);
}

bool LxmfAdapter::sendLinkIdentify(LinkSession& session)
{
    if (!identity_.isReady() || !session.initiator ||
        session.state != LinkState::Active || session.local_identity_sent)
    {
        return false;
    }

    uint8_t combined_pub[reticulum::kCombinedPublicKeySize] = {};
    identity_.combinedPublicKey(combined_pub);

    std::array<uint8_t, reticulum::kTruncatedHashSize + reticulum::kCombinedPublicKeySize>
        signed_data{};
    memcpy(signed_data.data(), session.link_id, reticulum::kTruncatedHashSize);
    memcpy(signed_data.data() + reticulum::kTruncatedHashSize,
           combined_pub,
           reticulum::kCombinedPublicKeySize);

    uint8_t payload[reticulum::kCombinedPublicKeySize + reticulum::kSignatureSize] = {};
    memcpy(payload, combined_pub, sizeof(combined_pub));
    if (!identity_.sign(signed_data.data(),
                        signed_data.size(),
                        payload + reticulum::kCombinedPublicKeySize))
    {
        return false;
    }

    const bool sent = sendLinkPacket(session,
                                     reticulum::PacketType::Data,
                                     reticulum::PacketContext::LinkIdentify,
                                     payload,
                                     sizeof(payload),
                                     true);
    if (sent)
    {
        session.local_identity_sent = true;
    }
    return sent;
}

bool LxmfAdapter::sendLinkPacketProof(LinkSession& session,
                                      const uint8_t* raw_packet, size_t raw_len)
{
    if (!raw_packet || raw_len == 0)
    {
        return false;
    }

    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    reticulum::computePacketHash(raw_packet, raw_len, packet_hash);

    uint8_t proof_payload[reticulum::kFullHashSize + reticulum::kSignatureSize] = {};
    memcpy(proof_payload, packet_hash, sizeof(packet_hash));
    bool signed_ok = false;
    if (session.initiator)
    {
        signed_ok = signWithKey(session.local_sig_pub,
                                session.local_sig_priv,
                                packet_hash,
                                sizeof(packet_hash),
                                proof_payload + sizeof(packet_hash));
    }
    else
    {
        signed_ok = identity_.sign(packet_hash,
                                   sizeof(packet_hash),
                                   proof_payload + sizeof(packet_hash));
    }
    if (!signed_ok)
    {
        return false;
    }

    return sendLinkPacket(session,
                          reticulum::PacketType::Proof,
                          reticulum::PacketContext::None,
                          proof_payload,
                          sizeof(proof_payload),
                          false);
}

bool LxmfAdapter::sendLinkResponse(LinkSession& session,
                                   const uint8_t* request_id,
                                   size_t request_id_len,
                                   const uint8_t* packed_data,
                                   size_t packed_data_len,
                                   bool data_is_nil)
{
    const size_t response_capacity = request_id_len + packed_data_len + 32;
    runtime::ResourcePayloadBuffer response_payload(response_capacity, 0);
    size_t response_len = response_payload.size();
    if (!encodeLinkResponsePayload(request_id,
                                   request_id_len,
                                   packed_data,
                                   packed_data_len,
                                   data_is_nil,
                                   response_payload.data(),
                                   &response_len))
    {
        return false;
    }

    response_payload.resize(response_len);
    if (response_len <= session.mdu)
    {
        return sendLinkPacket(session,
                              reticulum::PacketType::Data,
                              reticulum::PacketContext::Response,
                              response_payload.data(),
                              response_payload.size(),
                              true);
    }

    return queueOutgoingResource(session,
                                 response_payload.data(),
                                 response_payload.size(),
                                 kResourceFlagResponse,
                                 request_id,
                                 request_id_len);
}

bool LxmfAdapter::advertiseLinkResource(LinkSession& session,
                                        LinkResourceTransfer& resource,
                                        uint32_t hashmap_segment)
{
    const size_t segment_capacity = resourceHashmapSegmentCapacity(session.mdu);
    const size_t start_hash = static_cast<size_t>(hashmap_segment) * segment_capacity;
    if (segment_capacity == 0 || start_hash >= resource.part_count)
    {
        return false;
    }

    const size_t remaining_hashes = static_cast<size_t>(resource.part_count) - start_hash;
    size_t slice_hashes = std::min(segment_capacity, remaining_hashes);
    while (slice_hashes > 0)
    {
        const size_t slice_offset = start_hash * kResourceMapHashLen;
        const size_t slice_len = slice_hashes * kResourceMapHashLen;

        std::memset(scratch_.resource_advertisement,
                    0,
                    sizeof(scratch_.resource_advertisement));
        size_t advertisement_len = sizeof(scratch_.resource_advertisement);
        if (encodeResourceAdvertisement(resource.transfer_size,
                                        resource.data_size,
                                        resource.part_count,
                                        resource.resource_hash,
                                        resource.random_hash,
                                        resource.original_hash,
                                        1,
                                        1,
                                        resource.request_id.empty() ? nullptr : resource.request_id.data(),
                                        resource.request_id.size(),
                                        resource.flags,
                                        resource.hashmap.data() + slice_offset,
                                        slice_len,
                                        scratch_.resource_advertisement,
                                        &advertisement_len) &&
            advertisement_len <= session.mdu)
        {
            resource.last_activity_ms = millis();
            return sendLinkPacket(session,
                                  reticulum::PacketType::Data,
                                  reticulum::PacketContext::ResourceAdv,
                                  scratch_.resource_advertisement,
                                  advertisement_len,
                                  true);
        }

        --slice_hashes;
    }

    return false;
}

bool LxmfAdapter::queueOutgoingResource(LinkSession& session,
                                        const uint8_t* data, size_t len,
                                        uint8_t flags,
                                        const uint8_t* request_id,
                                        size_t request_id_len,
                                        uint32_t message_id)
{
    if (!data || len == 0 || session.mdu == 0 || (request_id_len != 0 && !request_id) ||
        len > std::numeric_limits<uint32_t>::max() ||
        len > (std::numeric_limits<size_t>::max() - kResourceDataPrefixLen) ||
        len > (std::numeric_limits<size_t>::max() - sizeof(runtime::LinkResourceTransfer::random_hash)) ||
        len > (std::numeric_limits<size_t>::max() - reticulum::kFullHashSize))
    {
        return false;
    }

    runtime::ResourcePayloadBuffer stream(kResourceDataPrefixLen + len, 0);
    fillRandomBytes(stream.data(), kResourceDataPrefixLen);
    memcpy(stream.data() + kResourceDataPrefixLen, data, len);

    const size_t encrypted_capacity = reticulum::tokenSizeForPlaintext(stream.size());
    runtime::ResourcePayloadBuffer encrypted_stream(encrypted_capacity, 0);
    size_t encrypted_len = encrypted_stream.size();
    uint8_t iv[reticulum::kTokenIvSize] = {};
    fillRandomBytes(iv, sizeof(iv));
    if (!reticulum::tokenEncrypt(session.derived_key,
                                 iv,
                                 stream.data(),
                                 stream.size(),
                                 encrypted_stream.data(),
                                 &encrypted_len))
    {
        return false;
    }
    encrypted_stream.resize(encrypted_len);

    const size_t part_count =
        (encrypted_stream.size() + static_cast<size_t>(session.mdu) - 1U) / static_cast<size_t>(session.mdu);
    if (part_count == 0)
    {
        return false;
    }

    const size_t segment_capacity = resourceHashmapSegmentCapacity(session.mdu);
    const size_t collision_guard = (kResourceWindowSize * 2U) + segment_capacity;

    LinkResourceTransfer resource{};
    if (!link_manager_.initialiseOutgoingResource(resource,
                                                  request_id,
                                                  request_id_len,
                                                  static_cast<uint32_t>(len),
                                                  static_cast<uint32_t>(encrypted_stream.size()),
                                                  static_cast<uint32_t>(part_count),
                                                  flags | kResourceFlagEncrypted,
                                                  millis(),
                                                  kResourceWindowSize))
    {
        return false;
    }
    bool mapped = false;
    for (size_t attempt = 0; attempt < 8 && !mapped; ++attempt)
    {
        fillRandomBytes(resource.random_hash, sizeof(resource.random_hash));

        if (!fullHashJoined(data,
                            len,
                            resource.random_hash,
                            sizeof(resource.random_hash),
                            resource.resource_hash))
        {
            return false;
        }
        memcpy(resource.original_hash, resource.resource_hash, sizeof(resource.original_hash));

        if (!fullHashJoined(data,
                            len,
                            resource.resource_hash,
                            reticulum::kFullHashSize,
                            resource.expected_proof))
        {
            return false;
        }

        resource.hashmap.clear();
        runtime::RuntimeMapHashList recent_hashes;
        recent_hashes.reserve(collision_guard);
        bool collision = false;

        for (size_t index = 0; index < part_count; ++index)
        {
            const size_t offset = index * static_cast<size_t>(session.mdu);
            const size_t chunk_len =
                std::min(static_cast<size_t>(session.mdu), encrypted_stream.size() - offset);

            resource.parts[index].assign(encrypted_stream.begin() + offset,
                                         encrypted_stream.begin() + offset + chunk_len);

            uint8_t full_hash[reticulum::kFullHashSize] = {};
            if (!fullHashJoined(resource.parts[index].data(),
                                chunk_len,
                                resource.random_hash,
                                sizeof(resource.random_hash),
                                full_hash))
            {
                return false;
            }

            std::array<uint8_t, kResourceMapHashLen> map_hash{};
            memcpy(map_hash.data(), full_hash, map_hash.size());
            if (std::find(recent_hashes.begin(), recent_hashes.end(), map_hash) != recent_hashes.end())
            {
                collision = true;
                break;
            }

            resource.map_hashes[index] = map_hash;
            resource.hashmap.insert(resource.hashmap.end(), map_hash.begin(), map_hash.end());
            recent_hashes.push_back(map_hash);
            if (recent_hashes.size() > collision_guard)
            {
                recent_hashes.erase(recent_hashes.begin());
            }
        }

        mapped = !collision;
    }

    if (!mapped)
    {
        return false;
    }

    LinkResourceTransfer* queued_resource =
        link_manager_.appendOutgoingResource(session, std::move(resource));
    if (!queued_resource || !advertiseLinkResource(session, *queued_resource, 0))
    {
        (void)link_manager_.discardLastOutgoingResource(session);
        return false;
    }
    if (message_id != 0)
    {
        delivery_attempt_ledger_.noteLinkResourceReceipt(
            queued_resource->resource_hash,
            session.link_id,
            message_id,
            millis(),
            kMaxPendingDeliveryReceipts);
    }

    return true;
}

void LxmfAdapter::closeLinkSession(LinkSession& session, LinkCloseReason reason)
{
    link_manager_.forEachDeferredPayload(
        session,
        [this, &session, reason](const runtime::DeferredLinkPayload& deferred)
        {
            if (deferred.message_id != 0)
            {
                Serial.printf("[LXMF][%s] deferred_failed msg=%lu reason=link_close close_reason=%u\n",
                              session.destination ==
                                      LocalDestinationKind::Propagation
                                  ? "PropagationTX"
                                  : "DirectTX",
                              static_cast<unsigned long>(deferred.message_id),
                              static_cast<unsigned>(reason));
                delivery_notifier_.failed(deferred.message_id);
            }
        });

    delivery_attempt_ledger_.takeReceiptsForLink(
        session.link_id,
        [this](const runtime::DeliveryAttemptReceipt& receipt)
        {
            if (receipt.message_id != 0)
            {
                delivery_notifier_.failed(receipt.message_id);
            }
        });

    const bool transitioned =
        link_manager_.closeSession(session, reason, millis());
    if (!transitioned)
    {
        return;
    }

    if (session.destination == LocalDestinationKind::CallAudio)
    {
        (void)lxst_telephony_client_.dispatch(
            session,
            {reticulum::lxst::call::EventType::LinkClosed},
            millis(),
            nullptr);
        ::platform::ui::reticulum_call::notify_link_closed(session.link_id);
    }

    path_manager_.removeLinkRelay(session.link_id);

    if ((reason == LinkCloseReason::Timeout || reason == LinkCloseReason::Error) &&
        !isZeroBytes(session.remote_destination_hash, sizeof(session.remote_destination_hash)))
    {
        path_manager_.expirePath(session.remote_destination_hash);
        bool requested = false;
        destination_registry_.forEach(
            [&](PeerInfo& peer)
            {
                if (requested ||
                    !hashesEqual(peer.destination_hash,
                                 session.remote_destination_hash,
                                 sizeof(peer.destination_hash)))
                {
                    return;
                }
                path_manager_.resetPeerPathRequest(peer);
                (void)sendPathRequest(peer);
                requested = true;
            });
    }
}

void LxmfAdapter::flushDeferredLinkPayloads(LinkSession& session)
{
    if (session.state != LinkState::Active)
    {
        return;
    }

    while (const runtime::DeferredLinkPayload* deferred =
               link_manager_.firstDeferredPayload(session))
    {
        bool sent = false;
        if (deferred->payload.size() <= session.mdu)
        {
            uint8_t packet_hash[reticulum::kFullHashSize] = {};
            sent = sendLinkPacket(session,
                                  reticulum::PacketType::Data,
                                  reticulum::PacketContext::None,
                                  deferred->payload.data(),
                                  deferred->payload.size(),
                                  true,
                                  false,
                                  deferred->message_id != 0 ? packet_hash
                                                            : nullptr);
            if (sent && deferred->message_id != 0)
            {
                delivery_attempt_ledger_.noteLinkPacketReceipt(
                    packet_hash,
                    session.link_id,
                    deferred->message_id,
                    millis(),
                    kMaxPendingDeliveryReceipts);
            }
        }
        else
        {
            sent = queueOutgoingResource(session,
                                         deferred->payload.data(),
                                         deferred->payload.size(),
                                         deferred->resource_flags,
                                         deferred->request_id.empty()
                                             ? nullptr
                                             : deferred->request_id.data(),
                                         deferred->request_id.size(),
                                         deferred->message_id);
        }

        if (!sent)
        {
            break;
        }

        if (session.destination == LocalDestinationKind::Delivery)
        {
            (void)sendLinkIdentify(session);
        }

        if (deferred->message_id != 0)
        {
            Serial.printf("[LXMF][%s] awaiting_proof msg=%lu path=link payload_len=%u representation=%s\n",
                          session.destination ==
                                  LocalDestinationKind::Propagation
                              ? "PropagationTX"
                              : "DirectTX",
                          static_cast<unsigned long>(deferred->message_id),
                          static_cast<unsigned>(deferred->payload.size()),
                          deferred->payload.size() <= session.mdu
                              ? "packet"
                              : "resource");
        }

        link_manager_.popFirstDeferredPayload(session);
    }
}

LxmfAdapter::LinkSession* LxmfAdapter::findLinkSession(
    const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    return link_manager_.findSession(link_id);
}

LxmfAdapter::LinkSession* LxmfAdapter::findActiveLinkSessionByDestination(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    LocalDestinationKind kind)
{
    return link_manager_.findActiveSessionByDestination(destination_hash, kind);
}

void LxmfAdapter::cullLinkSessions()
{
    const uint32_t now_ms = millis();
    const auto call_snapshot =
        ::platform::ui::reticulum_call::snapshot();
    const runtime::LinkRuntimeLimits limits{
        kMaxLinkSessions,
        kLinkRequestTtlMs,
        kLinkHandshakeTimeoutMs,
        kLinkIdleTimeoutMs,
        kLinkSessionTtlMs,
        kLinkStaleGraceMs,
        kLinkKeepaliveTimeoutFactor,
        5000};
    const runtime::ResourceRuntimeLimits resource_limits{kResourceTransferTtlMs};
    delivery_attempt_ledger_.takeExpiredReceipts(
        runtime::DeliveryAttemptKind::LinkPacket,
        now_ms,
        kLinkPacketReceiptTtlMs,
        [this](const runtime::DeliveryAttemptReceipt& receipt)
        {
            if (receipt.message_id != 0)
            {
                delivery_notifier_.failed(receipt.message_id);
            }
        });

    link_manager_.forEachSession(
        [this, now_ms, &call_snapshot, &limits, &resource_limits](LinkSession& session)
        {
            if (session.destination == LocalDestinationKind::CallAudio &&
                session.state == LinkState::Active)
            {
                if (lxst_telephony_client_.phaseTimedOut(session, now_ms))
                {
                    const auto& call_state =
                        lxst_telephony_client_.state(session);
                    char link_hash[12] = {};
                    formatHashPrefix(session.link_id,
                                     link_hash,
                                     sizeof(link_hash));
                    Serial.printf("[LXMF][Call] phase_timeout link=%s phase=%s local=%u remote=%u elapsed_ms=%lu\n",
                                  link_hash,
                                  reticulum::lxst::call::phaseName(
                                      call_state.phase),
                                  static_cast<unsigned>(
                                      call_state.local_status),
                                  static_cast<unsigned>(
                                      call_state.remote_status),
                                  static_cast<unsigned long>(
                                      now_ms -
                                      call_state.phase_started_ms));
                    (void)dispatchLxstCallEvent(
                        session,
                        {reticulum::lxst::call::EventType::Timeout});
                    return;
                }

#if TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT
                if (session.call_wire_profile ==
                        ReticulumCallWireProfile::MeshChatCallAudio &&
                    !session.initiator &&
                    hashesEqual(call_snapshot.link_id,
                                session.link_id,
                                sizeof(session.link_id)) &&
                    call_snapshot.realtime_phase ==
                        ::platform::ui::reticulum_call::RealtimePhase::IncomingRinging &&
                    now_ms - call_snapshot.updated_ms >= 60000)
                {
                    (void)sendLinkPacket(session,
                                         reticulum::PacketType::Data,
                                         reticulum::PacketContext::LinkClose,
                                         session.link_id,
                                         sizeof(session.link_id),
                                         true,
                                         true);
                    closeLinkSession(session, LinkCloseReason::Timeout);
                    return;
                }
#endif
            }

            link_manager_.cullSessionTables(session, now_ms, limits);
            link_manager_.forEachExpiredOutgoingResource(
                session,
                now_ms,
                kResourceTransferTtlMs,
                [this, &session](
                    const runtime::LinkResourceTransfer& resource)
                {
                    runtime::DeliveryAttemptReceipt* receipt =
                        delivery_attempt_ledger_.findLinkResourceReceipt(
                            session.link_id,
                            resource.resource_hash);
                    if (!receipt)
                    {
                        return;
                    }
                    const uint32_t message_id = receipt->message_id;
                    delivery_attempt_ledger_.removeLinkResourceReceipt(
                        session.link_id,
                        resource.resource_hash);
                    if (message_id != 0)
                    {
                        delivery_notifier_.failed(message_id);
                    }
                });
            link_manager_.cullResources(session, now_ms, resource_limits);
            const runtime::LinkRuntimeMaintenance maintenance =
                link_manager_.advanceSessionLifecycle(session, now_ms, limits);
            if (maintenance.close_timeout)
            {
                closeLinkSession(session, LinkCloseReason::Timeout);
            }
            else
            {
                if (maintenance.flush_deferred_payloads)
                {
                    flushDeferredLinkPayloads(session);
                }
                if (maintenance.send_keepalive)
                {
                    (void)sendLinkKeepalive(session);
                }
                if (maintenance.marked_stale)
                {
                    link_manager_.markSessionStale(session);
                }
            }
        });

    link_manager_.removeExpiredSessions(now_ms, limits);
}

LxmfAdapter::PeerInfo* LxmfAdapter::rememberPeerIdentity(
    const uint8_t combined_pub[reticulum::kCombinedPublicKeySize],
    const char* display_name, bool publish_contact)
{
    if (!combined_pub)
    {
        return nullptr;
    }

    uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::computeIdentityHash(combined_pub, identity_hash);

    uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf", "delivery", name_hash);
    reticulum::computeDestinationHash(name_hash, identity_hash, delivery_hash);

    PeerInfo& peer = destination_registry_.upsertDestination(delivery_hash);
    copyHash(peer.identity_hash, identity_hash, sizeof(peer.identity_hash));
    memcpy(peer.enc_pub, combined_pub, LxmfIdentity::kEncPubKeySize);
    memcpy(peer.sig_pub,
           combined_pub + LxmfIdentity::kEncPubKeySize,
           LxmfIdentity::kSigPubKeySize);
    peer.last_seen_s = currentTimestampSeconds();
    if (display_name && display_name[0] != '\0')
    {
        copyCString(peer.display_name, sizeof(peer.display_name), display_name);
    }
    if (!publish_contact)
    {
        return &peer;
    }
    const bool allow_persistence =
        screen_runtime::is_sleeping() && !screen_runtime::is_saver_active();
    if (allow_persistence)
    {
        if (!recordPeerInDirectory(peer, MeshPeerSource::RuntimeRx, false, false))
        {
            Serial.printf("[LXMF][Directory] address_save failed status=mesh_peer_directory\n");
        }
    }
    publishPeerUpdate(peer);
    return &peer;
}

bool LxmfAdapter::acceptVerifiedEnvelope(const uint8_t* plaintext, size_t plaintext_len,
                                         const uint8_t* raw_packet, size_t raw_len,
                                         uint8_t* out_message_hash,
                                         bool* out_awaiting_commit,
                                         LinkSession* incoming_delivery_session)
{
    if (out_message_hash)
    {
        std::memset(out_message_hash, 0, reticulum::kFullHashSize);
    }
    if (out_awaiting_commit)
    {
        *out_awaiting_commit = false;
    }
    ReticulumPeerIdentity conversation_identity{};
    return acceptVerifiedEnvelopeForDestination(identity_.destinationHash(),
                                                conversation_identity,
                                                false,
                                                true,
                                                plaintext,
                                                plaintext_len,
                                                raw_packet,
                                                raw_len,
                                                out_message_hash,
                                                out_awaiting_commit,
                                                incoming_delivery_session);
}

bool LxmfAdapter::acceptVerifiedEnvelopeForDestination(
    const uint8_t expected_destination_hash[reticulum::kTruncatedHashSize],
    const ReticulumPeerIdentity& conversation_identity,
    bool destination_is_group,
    bool encrypted,
    const uint8_t* plaintext, size_t plaintext_len,
    const uint8_t* raw_packet, size_t raw_len,
    uint8_t* out_message_hash,
    bool* out_awaiting_commit,
    LinkSession* incoming_delivery_session)
{
    if (out_awaiting_commit)
    {
        *out_awaiting_commit = false;
    }
    char expected_hash[12] = {};
    formatHashPrefix(expected_destination_hash, expected_hash, sizeof(expected_hash));
    Serial.printf("[LXMF][%sRX] envelope begin dest=%s plaintext_len=%u raw_len=%u encrypted=%u\n",
                  destination_is_group ? "Group" : "Direct",
                  expected_hash,
                  static_cast<unsigned>(plaintext_len),
                  static_cast<unsigned>(raw_len),
                  encrypted ? 1U : 0U);
    if (!expected_destination_hash || !plaintext || plaintext_len == 0)
    {
        Serial.printf("[LXMF][%sRX] drop reason=invalid_input dest=%s\n",
                      destination_is_group ? "Group" : "Direct",
                      expected_hash);
        return false;
    }

    const auto release_packet_for_retry = [this, raw_packet, raw_len]()
    {
        if (!raw_packet || raw_len == 0)
        {
            return;
        }
        uint8_t packet_hash[reticulum::kFullHashSize] = {};
        reticulum::computePacketHash(raw_packet, raw_len, packet_hash);
        path_manager_.forgetPacket(packet_hash);
    };

    DecodedEnvelope envelope{};
    bool embedded_destination_hash = false;
    if (!unpackRnsLxmfEnvelope(expected_destination_hash,
                               plaintext,
                               plaintext_len,
                               &envelope,
                               &embedded_destination_hash))
    {
        Serial.printf("[LXMF][%sRX] drop reason=unpack_envelope_failed dest=%s plaintext_len=%u\n",
                      destination_is_group ? "Group" : "Direct",
                      expected_hash,
                      static_cast<unsigned>(plaintext_len));
        return false;
    }
    char envelope_dest_hash[12] = {};
    char source_hash[12] = {};
    formatHashPrefix(envelope.destination_hash,
                     envelope_dest_hash,
                     sizeof(envelope_dest_hash));
    formatHashPrefix(envelope.source_hash, source_hash, sizeof(source_hash));
    const char* wire_form = embedded_destination_hash ? "full" : "opportunistic_tail";
    Serial.printf("[LXMF][%sRX] envelope parsed wire=%s dest=%s source=%s payload_len=%u\n",
                  destination_is_group ? "Group" : "Direct",
                  wire_form,
                  envelope_dest_hash,
                  source_hash,
                  static_cast<unsigned>(envelope.packed_payload.size()));
    if (!hashesEqual(envelope.destination_hash,
                     expected_destination_hash,
                     reticulum::kTruncatedHashSize))
    {
        Serial.printf("[LXMF][%sRX] drop reason=destination_mismatch expected=%s envelope=%s source=%s\n",
                      destination_is_group ? "Group" : "Direct",
                      expected_hash,
                      envelope_dest_hash,
                      source_hash);
        return false;
    }

    const size_t signed_part_required =
        (reticulum::kTruncatedHashSize * 2) +
        envelope.packed_payload.size() +
        reticulum::kFullHashSize;
    runtime::RuntimeByteBuffer signed_part(signed_part_required);
    size_t signed_part_len = signed_part.size();
    uint8_t message_hash[reticulum::kFullHashSize] = {};
    if (!buildSignedPart(envelope.destination_hash,
                         envelope.source_hash,
                         envelope.packed_payload.data(),
                         envelope.packed_payload.size(),
                         signed_part.data(),
                         &signed_part_len,
                         message_hash))
    {
        Serial.printf("[LXMF][%sRX] drop reason=signed_part_failed dest=%s source=%s payload_len=%u\n",
                      destination_is_group ? "Group" : "Direct",
                      expected_hash,
                      source_hash,
                      static_cast<unsigned>(envelope.packed_payload.size()));
        return false;
    }
    if (out_message_hash)
    {
        copyHash(out_message_hash,
                 message_hash,
                 reticulum::kFullHashSize);
    }

    PeerInfo* peer = findOrLoadPeerByDestinationHash(envelope.source_hash);
    runtime::LxmfDeliveryContext delivery_context{};
    delivery_context.local_node_id = identity_.nodeId();
    delivery_context.message_id = messageIdFromHash(message_hash);
    delivery_context.has_message_hash = true;
    copyHash(delivery_context.message_hash,
             message_hash,
             sizeof(delivery_context.message_hash));
    delivery_context.timestamp_s = currentTimestampSeconds();
    delivery_context.destination_is_group = destination_is_group;
    delivery_context.encrypted = encrypted;
    populateRxMeta(&delivery_context.rx_meta);

    runtime::LxmfVerifiedDelivery delivery{};
    if (!peer)
    {
        const bool path_requested =
            sendPathRequestForDestination(envelope.source_hash);
        delivery_context.peer_node_id =
            reticulum::nodeIdFromDestinationHash(envelope.source_hash);
        delivery_context.peer_identity =
            makeReticulumDestinationIdentity(envelope.source_hash);
        delivery_context.conversation_identity =
            hasReticulumDestinationIdentity(conversation_identity)
                ? conversation_identity
                : delivery_context.peer_identity;
        delivery_context.source_unverified = true;

        DecodedAppData unverified_app_data{};
        if (decodeAppDataPayload(envelope.packed_payload.data(),
                                 envelope.packed_payload.size(),
                                 &unverified_app_data))
        {
            Serial.printf("[LXMF][%sRX] unverified ignored kind=app_data dest=%s source=%s msg=%lu port=%lu path_requested=%u transport_ack=1\n",
                          destination_is_group ? "Group" : "Direct",
                          expected_hash,
                          source_hash,
                          static_cast<unsigned long>(delivery_context.message_id),
                          static_cast<unsigned long>(unverified_app_data.portnum),
                          path_requested ? 1U : 0U);
            return true;
        }

        DecodedTextPayload text_payload{};
        if (!unpackTextPayload(envelope.packed_payload.data(),
                               envelope.packed_payload.size(),
                               &text_payload) ||
            !runtime::materialiseLxmfTextDelivery(text_payload,
                                                  delivery_context,
                                                  &delivery.text))
        {
            Serial.printf("[LXMF][%sRX] unverified ignored kind=malformed dest=%s source=%s msg=%lu payload_len=%u path_requested=%u transport_ack=1\n",
                          destination_is_group ? "Group" : "Direct",
                          expected_hash,
                          source_hash,
                          static_cast<unsigned long>(delivery_context.message_id),
                          static_cast<unsigned>(envelope.packed_payload.size()),
                          path_requested ? 1U : 0U);
            return true;
        }
        delivery.kind = runtime::LxmfDeliveryKind::Text;
        Serial.printf("[LXMF][%sRX] unverified kind=text msg=%lu from=%08lX dest=%s source=%s path_requested=%u\n",
                      destination_is_group ? "Group" : "Direct",
                      static_cast<unsigned long>(delivery_context.message_id),
                      static_cast<unsigned long>(delivery_context.peer_node_id),
                      expected_hash,
                      source_hash,
                      path_requested ? 1U : 0U);
    }
    else
    {
        if (!LxmfIdentity::verify(peer->sig_pub,
                                  envelope.signature,
                                  signed_part.data(),
                                  signed_part_len))
        {
            Serial.printf("[LXMF][%sRX] ignored reason=signature_failed dest=%s source=%s node=%08lX transport_ack=1\n",
                          destination_is_group ? "Group" : "Direct",
                          expected_hash,
                          source_hash,
                          static_cast<unsigned long>(peer->node_id));
            return true;
        }

        delivery_context.peer_node_id = peer->node_id;
        delivery_context.peer_identity = runtime::reticulumIdentityForPeer(*peer);
        delivery_context.conversation_identity =
            hasReticulumDestinationIdentity(conversation_identity)
                ? conversation_identity
                : delivery_context.peer_identity;
        if (incoming_delivery_session &&
            incoming_delivery_session->destination == LocalDestinationKind::Delivery)
        {
            copyHash(incoming_delivery_session->remote_destination_hash,
                     peer->destination_hash,
                     sizeof(incoming_delivery_session->remote_destination_hash));
            copyHash(incoming_delivery_session->remote_identity_hash,
                     peer->identity_hash,
                     sizeof(incoming_delivery_session->remote_identity_hash));
            std::memcpy(incoming_delivery_session->peer_identity_sig_pub,
                        peer->sig_pub,
                        sizeof(incoming_delivery_session->peer_identity_sig_pub));
            incoming_delivery_session->remote_identity_known = true;
        }
        if (!runtime::materialiseVerifiedLxmfDelivery(envelope.packed_payload.data(),
                                                      envelope.packed_payload.size(),
                                                      delivery_context,
                                                      &delivery))
        {
            Serial.printf("[LXMF][%sRX] ignored reason=materialise_failed dest=%s source=%s msg=%lu payload_len=%u transport_ack=1\n",
                          destination_is_group ? "Group" : "Direct",
                          expected_hash,
                          source_hash,
                          static_cast<unsigned long>(delivery_context.message_id),
                          static_cast<unsigned>(envelope.packed_payload.size()));
            return true;
        }
        Serial.printf("[LXMF][%sRX] verified kind=%u msg=%lu from=%08lX dest=%s source=%s\n",
                      destination_is_group ? "Group" : "Direct",
                      static_cast<unsigned>(delivery.kind),
                      static_cast<unsigned long>(delivery_context.message_id),
                      static_cast<unsigned long>(peer->node_id),
                      expected_hash,
                      source_hash);
    }

    if (geocaching_only_ && delivery.kind != runtime::LxmfDeliveryKind::Text)
    {
        release_packet_for_retry();
        return false;
    }
    if (delivery.kind == runtime::LxmfDeliveryKind::AppData)
    {
        if (delivery.app_data.incoming.portnum ==
            ::platform::esp::arduino_common::voice::vmp_session::kLxmfAppDataPort)
        {
            const bool accepted =
                ::platform::esp::arduino_common::voice::vmp_session::acceptLxmfEnvelope(
                    delivery.app_data.incoming.from,
                    delivery.app_data.payload.empty() ? nullptr
                                                      : delivery.app_data.payload.data(),
                    delivery.app_data.payload.size());
            // VMP is terminal at the local voice inbox.  Even malformed VMP
            // traffic is consumed here so it can never become generic app data
            // that a later service might resend or bridge to another bearer.
            Serial.printf("[VMP][LXMF] inbound from=%08lX bytes=%u local_only=%u\n",
                          static_cast<unsigned long>(delivery.app_data.incoming.from),
                          static_cast<unsigned>(delivery.app_data.payload.size()),
                          accepted ? 1U : 0U);
            return true;
        }
        ::chat::infra::IncomingQueuePushReport report{};
        if (data_receive_queue_.push(delivery.app_data.incoming,
                                     delivery.app_data.payload.empty() ? nullptr
                                                                       : delivery.app_data.payload.data(),
                                     delivery.app_data.payload.size(),
                                     ::chat::infra::IncomingQueuePriority::P0Critical,
                                     &report))
        {
            if (report.dropped_existing)
            {
                Serial.printf("[LXMF] RX data queue pressure evicted_prio=%u depth=%u\n",
                              static_cast<unsigned>(report.dropped_priority),
                              static_cast<unsigned>(data_receive_queue_.size()));
            }
            Serial.printf("[LXMF][%sRX] queued app_data msg=%lu port=%lu len=%u depth=%u\n",
                          destination_is_group ? "Group" : "Direct",
                          static_cast<unsigned long>(delivery_context.message_id),
                          static_cast<unsigned long>(delivery.app_data.incoming.portnum),
                          static_cast<unsigned>(delivery.app_data.payload.size()),
                          static_cast<unsigned>(data_receive_queue_.size()));
            return true;
        }
        Serial.printf("[LXMF] RX data queue drop port=%lu len=%u depth=%u\n",
                      static_cast<unsigned long>(delivery.app_data.incoming.portnum),
                      static_cast<unsigned>(delivery.app_data.payload.size()),
                      static_cast<unsigned>(data_receive_queue_.size()));
        if (delivery.app_data.payload.size() <= ::chat::infra::kIncomingDataPayloadMaxLen)
        {
            release_packet_for_retry();
        }
        return false;
    }

    if (delivery.kind != runtime::LxmfDeliveryKind::Text)
    {
        Serial.printf("[LXMF][%sRX] drop reason=unsupported_delivery kind=%u msg=%lu\n",
                      destination_is_group ? "Group" : "Direct",
                      static_cast<unsigned>(delivery.kind),
                      static_cast<unsigned long>(delivery_context.message_id));
        return false;
    }

    ByteSpan custom_type, custom_data;
    const auto custom_result = extractCustomData(delivery.text.payload, &custom_type, &custom_data);
    if (custom_result == CustomDataResult::Invalid)
    {
        release_packet_for_retry();
        return false;
    }
    constexpr char geocaching_type[] = "trailmate.geocache";
    if (custom_result == CustomDataResult::Valid && custom_type.size == sizeof(geocaching_type) - 1 &&
        std::memcmp(custom_type.data, geocaching_type, custom_type.size) == 0)
    {
        if (delivery_context.source_unverified || destination_is_group || !geocaching_handler_)
        {
            release_packet_for_retry();
            return false;
        }
        const CustomDeliveryView view{
            {envelope.source_hash, sizeof(envelope.source_hash)},
            {expected_destination_hash, reticulum::kTruncatedHashSize},
            {delivery_context.message_hash, sizeof(delivery_context.message_hash)},
            custom_data};
        if (!geocaching_handler_(view, geocaching_handler_context_))
        {
            release_packet_for_retry();
            return false;
        }
        return true;
    }

    if (geocaching_only_)
    {
        release_packet_for_retry();
        return false;
    }
    if (!delivery_context.source_unverified)
    {
        SidebandTelemetryLocation location{};
        if (decodeSidebandTelemetryLocation(delivery.text.payload, &location))
        {
            const int32_t altitude_m = location.altitude_cm >= 0
                                           ? (location.altitude_cm + 50) / 100
                                           : (location.altitude_cm - 50) / 100;
            sys::EventBus::publish(
                new sys::NodePositionUpdateEvent(
                    delivery_context.peer_node_id,
                    location.latitude_e6 * 10,
                    location.longitude_e6 * 10,
                    true,
                    altitude_m,
                    location.timestamp != 0 ? location.timestamp
                                            : delivery_context.timestamp_s,
                    32,
                    0,
                    0,
                    0,
                    location.accuracy_cm * 10),
                0);
            Serial.printf("[LXMF][Sideband] telemetry location node=%08lX lat_e6=%ld lon_e6=%ld alt_cm=%ld accuracy_cm=%lu ts=%lu\n",
                          static_cast<unsigned long>(delivery_context.peer_node_id),
                          static_cast<long>(location.latitude_e6),
                          static_cast<long>(location.longitude_e6),
                          static_cast<long>(location.altitude_cm),
                          static_cast<unsigned long>(location.accuracy_cm),
                          static_cast<unsigned long>(location.timestamp));
        }

        SidebandTelemetryRequest telemetry_request{};
        if (decodeSidebandTelemetryRequest(delivery.text.payload,
                                           &telemetry_request))
        {
            if (!config_.reticulum_allow_location_requests)
            {
                Serial.printf("[LXMF][Sideband] telemetry request denied node=%08lX reason=disabled\n",
                              static_cast<unsigned long>(delivery_context.peer_node_id));
            }
            else if (peer)
            {
                (void)respondToSidebandTelemetryRequest(*peer,
                                                        telemetry_request);
            }
        }
    }

    if (delivery.text.text.empty() && !delivery.text.payload.fields_empty)
    {
        Serial.printf("[LXMF][%sRX] accepted fields_only msg=%lu from=%08lX fields=%u unverified=%u\n",
                      destination_is_group ? "Group" : "Direct",
                      static_cast<unsigned long>(delivery_context.message_id),
                      static_cast<unsigned long>(delivery_context.peer_node_id),
                      static_cast<unsigned>(delivery.text.payload.fields.size()),
                      delivery_context.source_unverified ? 1U : 0U);
        return true;
    }
    ::chat::infra::IncomingQueuePushReport report{};
    if (text_receive_queue_.push(delivery.text.incoming,
                                 delivery.text.text.data(),
                                 delivery.text.text.size(),
                                 ::chat::infra::IncomingQueuePriority::P0Critical,
                                 &report))
    {
        if (out_awaiting_commit)
        {
            *out_awaiting_commit = true;
        }
        if (report.dropped_existing)
        {
            Serial.printf("[LXMF] RX text queue pressure evicted_prio=%u depth=%u\n",
                          static_cast<unsigned>(report.dropped_priority),
                          static_cast<unsigned>(text_receive_queue_.size()));
        }
        Serial.printf("[LXMF][%sRX] queued text msg=%lu from=%08lX to=%08lX len=%u depth=%u dest=%s unverified=%u\n",
                      destination_is_group ? "Group" : "Direct",
                      static_cast<unsigned long>(delivery_context.message_id),
                      static_cast<unsigned long>(delivery.text.incoming.from),
                      static_cast<unsigned long>(delivery.text.incoming.to),
                      static_cast<unsigned>(delivery.text.text.size()),
                      static_cast<unsigned>(text_receive_queue_.size()),
                      expected_hash,
                      delivery.text.incoming.source_unverified ? 1U : 0U);
        return true;
    }
    Serial.printf("[LXMF] RX text queue drop len=%u depth=%u\n",
                  static_cast<unsigned>(delivery.text.text.size()),
                  static_cast<unsigned>(text_receive_queue_.size()));
    if (delivery.text.text.size() <= ::chat::infra::kIncomingTextMaxLen)
    {
        release_packet_for_retry();
    }
    return false;
}

const ReticulumGroupDestinationConfig* LxmfAdapter::findConfiguredGroupDestination(
    const uint8_t hash[reticulum::kTruncatedHashSize]) const
{
    if (!hash)
    {
        return nullptr;
    }

    for (const auto& group : config_.reticulum_groups)
    {
        if (!group.enabled || !hasReticulumDestinationIdentity(group.identity))
        {
            continue;
        }
        if (hashesEqual(group.identity.destination_hash,
                        hash,
                        reticulum::kTruncatedHashSize))
        {
            return &group;
        }
    }
    return nullptr;
}

bool LxmfAdapter::isConfiguredGroupDestination(
    const ReticulumPeerIdentity& destination) const
{
    return hasReticulumDestinationIdentity(destination) &&
           findConfiguredGroupDestination(destination.destination_hash) != nullptr;
}

LxmfAdapter::PeerInfo* LxmfAdapter::findOrLoadPeerByNodeId(NodeId node_id)
{
    const runtime::PeerDirectoryLoadResult result =
        peer_directory_service_.findOrLoadByNodeId(destination_registry_,
                                                   node_id,
                                                   currentTimestampSeconds());
    if (!result.status.succeeded())
    {
        if (result.status.code != MeshPeerDirectoryStatusCode::StorageUnavailable &&
            result.status.code != MeshPeerDirectoryStatusCode::Busy &&
            result.status.code != MeshPeerDirectoryStatusCode::DeviceUnavailable &&
            result.status.code != MeshPeerDirectoryStatusCode::InvalidArgument)
        {
            Serial.printf("[LXMF][Directory] peer_lookup miss node=%08lX status=%u\n",
                          static_cast<unsigned long>(node_id),
                          static_cast<unsigned>(result.status.code));
        }
        return nullptr;
    }

    if (result.loaded_from_directory && result.peer)
    {
        peer_directory_service_.queuePeerUpdate(*result.peer);
        Serial.printf("[LXMF][Directory] peer_lookup loaded node=%08lX name=%s\n",
                      static_cast<unsigned long>(node_id),
                      result.peer->display_name[0] != '\0'
                          ? result.peer->display_name
                          : "<unnamed>");
    }
    return result.peer;
}

LxmfAdapter::PeerInfo* LxmfAdapter::findOrLoadPeerByDestinationHash(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    const runtime::PeerDirectoryLoadResult result =
        peer_directory_service_.findOrLoadByDestinationHash(destination_registry_,
                                                            destination_hash,
                                                            currentTimestampSeconds());
    if (!result.status.succeeded())
    {
        if (result.status.code != MeshPeerDirectoryStatusCode::StorageUnavailable &&
            result.status.code != MeshPeerDirectoryStatusCode::Busy &&
            result.status.code != MeshPeerDirectoryStatusCode::DeviceUnavailable &&
            result.status.code != MeshPeerDirectoryStatusCode::InvalidArgument)
        {
            char dest[12] = {};
            formatHashPrefix(destination_hash, dest, sizeof(dest));
            Serial.printf("[LXMF][Directory] peer_lookup miss dest=%s status=%u\n",
                          dest,
                          static_cast<unsigned>(result.status.code));
        }
        return nullptr;
    }

    if (result.loaded_from_directory && result.peer)
    {
        peer_directory_service_.queuePeerUpdate(*result.peer);
        char dest[12] = {};
        formatHashPrefix(result.peer->destination_hash, dest, sizeof(dest));
        Serial.printf("[LXMF][Directory] peer_lookup loaded dest=%s name=%s\n",
                      dest,
                      result.peer->display_name[0] != '\0'
                          ? result.peer->display_name
                          : "<unnamed>");
    }
    return result.peer;
}

MeshActionResult LxmfAdapter::persistPeerAddressNow(const PeerInfo& peer,
                                                    bool favorite) const
{
    return peer_directory_service_.persistPeerAddressNow(peer,
                                                         favorite,
                                                         currentTimestampSeconds());
}

bool LxmfAdapter::recordPeerInDirectory(const PeerInfo& peer,
                                        MeshPeerSource source,
                                        bool update_favorite,
                                        bool favorite) const
{
    const runtime::PeerDirectoryWriteResult result =
        peer_directory_service_.recordPeer(peer,
                                           source,
                                           update_favorite,
                                           favorite,
                                           currentTimestampSeconds());
    if (!result.record_status.succeeded())
    {
        if (result.record_status.code != MeshPeerDirectoryStatusCode::StorageUnavailable &&
            result.record_status.code != MeshPeerDirectoryStatusCode::Busy &&
            result.record_status.code != MeshPeerDirectoryStatusCode::DeviceUnavailable &&
            result.record_status.code != MeshPeerDirectoryStatusCode::InvalidArgument)
        {
            Serial.printf("[LXMF][Directory] address_save failed status=%u\n",
                          static_cast<unsigned>(result.record_status.code));
        }
        return false;
    }

    if (!result.flags_status.succeeded())
    {
        if (result.flags_status.code != MeshPeerDirectoryStatusCode::StorageUnavailable &&
            result.flags_status.code != MeshPeerDirectoryStatusCode::Busy &&
            result.flags_status.code != MeshPeerDirectoryStatusCode::DeviceUnavailable &&
            result.flags_status.code != MeshPeerDirectoryStatusCode::InvalidArgument)
        {
            Serial.printf("[LXMF][Directory] flag_save failed status=%u\n",
                          static_cast<unsigned>(result.flags_status.code));
        }
        return false;
    }
    return true;
}

void LxmfAdapter::pumpPendingPeerUpdates()
{
    const uint32_t now_ms = millis();
    const bool maintenance_window =
        screen_runtime::is_sleeping() && !screen_runtime::is_saver_active();
    peer_directory_service_.pumpQueuedPeerUpdates(destination_registry_,
                                                  *this,
                                                  now_ms,
                                                  maintenance_window,
                                                  kPeerProjectionSleepIntervalMs,
                                                  kPeerProjectionScreenIntervalMs);
}

void LxmfAdapter::publishPeerUpdate(const PeerInfo& peer)
{
    char short_name[10] = {};
    snprintf(short_name, sizeof(short_name), "%04lX",
             static_cast<unsigned long>(peer.node_id & 0xFFFFUL));

    sys::EventBus::publish(new sys::NodeProtocolUpdateEvent(
                               peer.node_id,
                               peer.last_seen_s,
                               static_cast<uint8_t>(chat::contacts::NodeProtocolType::Reticulum)),
                           0);

    auto* node_event = new sys::NodeInfoUpdateEvent(
        peer.node_id,
        short_name,
        peer.display_name,
        interfaces_.lastRxSnr(),
        interfaces_.lastRxRssi(),
        peer.last_seen_s,
        static_cast<uint8_t>(chat::contacts::NodeProtocolType::Reticulum),
        static_cast<uint8_t>(chat::contacts::NodeRoleType::Client),
        0,
        0,
        0xFF);
    node_event->reticulum_identity = runtime::reticulumIdentityForPeer(peer);
    sys::EventBus::publish(node_event, 0);
}

void LxmfAdapter::loadDirectoryPeers()
{
    if (!peer_directory_service_.hasDirectory())
    {
        Serial.printf("[LXMF][Directory] load skipped reason=no_mesh_peer_directory\n");
        return;
    }

    const runtime::PeerDirectoryLoadRecentResult result =
        peer_directory_service_.loadRecentAndQueue(destination_registry_,
                                                   currentTimestampSeconds());
    if (!result.status.succeeded())
    {
        Serial.printf("[LXMF][Directory] load failed status=%u\n",
                      static_cast<unsigned>(result.status.code));
        return;
    }

    if (result.loaded > 0)
    {
        Serial.printf("[LXMF][Directory] loaded addresses=%u directory=mesh_peer_directory\n",
                      static_cast<unsigned>(result.loaded));
    }
}

void LxmfAdapter::loadPersistedPeers()
{
    peers_loaded_ = true;
    loadDirectoryPeers();
}

uint32_t LxmfAdapter::currentTimestampSeconds() const
{
    const uint32_t epoch_s = now_epoch_seconds();
    if (is_valid_epoch(epoch_s))
    {
        return epoch_s;
    }
    return millis() / 1000U;
}

const char* LxmfAdapter::effectiveDisplayName() const
{
    if (!user_long_name_.empty())
    {
        return user_long_name_.c_str();
    }
    if (!user_short_name_.empty())
    {
        return user_short_name_.c_str();
    }
    return nullptr;
}

void LxmfAdapter::populateRxMeta(RxMeta* out) const
{
    if (!out)
    {
        return;
    }

    const RxMeta& last_meta = has_active_rx_meta_ ? active_rx_meta_ : interfaces_.lastRxMeta();
    if (last_meta.rx_timestamp_ms != 0 || last_meta.rx_timestamp_s != 0)
    {
        *out = last_meta;
        return;
    }

    out->rx_timestamp_ms = millis();
    out->rx_timestamp_s = currentTimestampSeconds();
    out->time_source = is_valid_epoch(out->rx_timestamp_s)
                           ? RxTimeSource::DeviceUtc
                           : RxTimeSource::Uptime;
    out->origin = RxOrigin::LoRa;
    out->direct = true;
    out->from_is = false;
    out->rssi_dbm_x10 = static_cast<int16_t>(lround(interfaces_.lastRxRssi() * 10.0f));
    out->snr_db_x10 = static_cast<int16_t>(lround(interfaces_.lastRxSnr() * 10.0f));
}

uint32_t LxmfAdapter::messageIdFromHash(const uint8_t hash[reticulum::kFullHashSize])
{
    return (static_cast<uint32_t>(hash[28]) << 24) |
           (static_cast<uint32_t>(hash[29]) << 16) |
           (static_cast<uint32_t>(hash[30]) << 8) |
           static_cast<uint32_t>(hash[31]);
}

void LxmfAdapter::pathRequestDestinationHash(uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    if (!out_hash)
    {
        return;
    }

    reticulum::computePathRequestDestinationHash(out_hash);
}

} // namespace chat::lxmf
