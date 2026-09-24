#include "platform/esp/arduino_common/chat/infra/store/protocol_chat_codec.h"
#include "sys/crc32.h"

#include <algorithm>
#include <cstring>

namespace chat::storage::v2
{
namespace
{

constexpr uint32_t kMessageMagic = 0x3247534DU; // MSG2
constexpr uint32_t kCatalogMagic = 0x32544143U; // CAT2
constexpr uint32_t kReadMagic = 0x32444152U;    // RAD2
constexpr uint32_t kStatusMagic = 0x32535453U;  // STS2
constexpr uint32_t kSeenMagic = 0x324E4553U;    // SEN2

constexpr uint8_t kMessageSourceUnverified = 0x01U;
constexpr uint8_t kMessageHasGeo = 0x02U;
constexpr uint8_t kMessageHasReticulumIdentity = 0x04U;
constexpr uint8_t kMessageHasLxmfHash = 0x08U;
constexpr uint8_t kProjectionDeleted = 0x01U;
constexpr uint8_t kProjectionHasReticulumIdentity = 0x02U;

struct MessagePrefix
{
    uint32_t magic = kMessageMagic;
    uint16_t schema = kStorageSchemaVersion;
    uint8_t flags = 0;
    uint8_t reserved = 0;
    uint32_t sequence = 0;
    uint32_t message_id = 0;
    uint32_t timestamp = 0;
    uint32_t from = 0;
    uint32_t peer = 0;
    int32_t geo_lat_e7 = 0;
    int32_t geo_lon_e7 = 0;
    uint16_t text_len = 0;
    uint8_t status = 0;
    uint8_t channel = 0;
    uint8_t rx_origin = 0;
    uint8_t team_location_icon = 0;
} __attribute__((packed));

struct MeshtasticMessageSlot
{
    MessagePrefix prefix{};
    char text[kMeshtasticTextMax] = {};
    uint32_t crc = 0;
} __attribute__((packed));

struct MeshCoreMessageSlot
{
    MessagePrefix prefix{};
    char text[kMeshCoreTextMax] = {};
    uint32_t crc = 0;
} __attribute__((packed));

struct ReticulumMessageSlot
{
    MessagePrefix prefix{};
    uint8_t destination_hash[kReticulumPeerHashSize] = {};
    uint8_t identity_hash[kReticulumPeerHashSize] = {};
    uint8_t lxmf_hash[kReticulumLxmfHashSize] = {};
    char text[kReticulumTextMax] = {};
    uint32_t crc = 0;
} __attribute__((packed));

struct CatalogPrefix
{
    uint32_t magic = kCatalogMagic;
    uint16_t schema = kStorageSchemaVersion;
    uint8_t flags = 0;
    uint8_t channel = 0;
    uint32_t peer = 0;
    uint32_t last_message_id = 0;
    uint32_t message_count = 0;
    uint32_t last_sequence = 0;
    uint32_t last_timestamp = 0;
    uint32_t unread = 0;
    uint8_t last_status = 0;
    uint8_t preview_len = 0;
    uint16_t reserved = 0;
    char preview[kCatalogPreviewMax] = {};
} __attribute__((packed));

struct NodeCatalogSlot
{
    CatalogPrefix prefix{};
    uint32_t crc = 0;
} __attribute__((packed));

struct ReticulumCatalogSlot
{
    CatalogPrefix prefix{};
    uint8_t destination_hash[kReticulumPeerHashSize] = {};
    uint8_t identity_hash[kReticulumPeerHashSize] = {};
    uint32_t crc = 0;
} __attribute__((packed));

struct ReadPrefix
{
    uint32_t magic = kReadMagic;
    uint16_t schema = kStorageSchemaVersion;
    uint8_t flags = 0;
    uint8_t channel = 0;
    uint32_t peer = 0;
    uint32_t last_read_sequence = 0;
} __attribute__((packed));

struct NodeReadSlot
{
    ReadPrefix prefix{};
    uint32_t crc = 0;
} __attribute__((packed));

struct ReticulumReadSlot
{
    ReadPrefix prefix{};
    uint8_t destination_hash[kReticulumPeerHashSize] = {};
    uint32_t crc = 0;
} __attribute__((packed));

struct StatusSlot
{
    uint32_t magic = kStatusMagic;
    uint16_t schema = kStorageSchemaVersion;
    uint8_t status = 0;
    uint8_t reserved = 0;
    uint32_t message_id = 0;
    uint32_t sequence = 0;
    uint32_t crc = 0;
} __attribute__((packed));

struct SeenSlot
{
    uint32_t magic = kSeenMagic;
    uint16_t schema = kStorageSchemaVersion;
    uint16_t reserved = 0;
    uint8_t hash[kReticulumLxmfHashSize] = {};
    uint32_t crc = 0;
} __attribute__((packed));

static_assert(sizeof(MessagePrefix) == 42,
              "v2 message prefix size changed");
static_assert(sizeof(MeshtasticMessageSlot) == 279,
              "v2 Meshtastic message slot size changed");
static_assert(sizeof(MeshCoreMessageSlot) == 217,
              "v2 MeshCore message slot size changed");
static_assert(sizeof(ReticulumMessageSlot) == 365,
              "v2 Reticulum message slot size changed");

bool validStatus(uint8_t raw)
{
    return raw <= static_cast<uint8_t>(MessageStatus::Delivered);
}

template <typename T>
bool validSlot(const T& slot, uint32_t expected_magic)
{
    return slot.crc == crc32(&slot, sizeof(T) - sizeof(slot.crc)) &&
           slot.prefix.magic == expected_magic &&
           slot.prefix.schema == kStorageSchemaVersion;
}

bool validStatusSlot(const StatusSlot& slot)
{
    return slot.magic == kStatusMagic &&
           slot.schema == kStorageSchemaVersion &&
           validStatus(slot.status) &&
           slot.crc == crc32(&slot, sizeof(slot) - sizeof(slot.crc));
}

bool validSeenSlot(const SeenSlot& slot)
{
    return slot.magic == kSeenMagic &&
           slot.schema == kStorageSchemaVersion &&
           slot.crc == crc32(&slot, sizeof(slot) - sizeof(slot.crc));
}

void encodeMessagePrefix(MessagePrefix& prefix,
                         const ChatMessage& message,
                         uint32_t sequence,
                         std::size_t text_len)
{
    prefix = MessagePrefix{};
    prefix.sequence = sequence;
    prefix.message_id = message.msg_id;
    prefix.timestamp = message.timestamp;
    prefix.from = message.from;
    prefix.peer = message.peer;
    prefix.geo_lat_e7 = message.geo_lat_e7;
    prefix.geo_lon_e7 = message.geo_lon_e7;
    prefix.text_len = static_cast<uint16_t>(text_len);
    prefix.status = static_cast<uint8_t>(message.status);
    prefix.channel = static_cast<uint8_t>(message.channel);
    prefix.rx_origin = static_cast<uint8_t>(message.rx_origin);
    prefix.team_location_icon = message.team_location_icon;
    if (message.source_unverified)
    {
        prefix.flags |= kMessageSourceUnverified;
    }
    if (message.has_geo)
    {
        prefix.flags |= kMessageHasGeo;
    }
}

bool decodeMessagePrefix(const MessagePrefix& prefix,
                         std::size_t text_capacity,
                         ChatMessage& message,
                         uint32_t* out_sequence)
{
    if (prefix.magic != kMessageMagic ||
        prefix.schema != kStorageSchemaVersion ||
        prefix.text_len > text_capacity || !validStatus(prefix.status))
    {
        return false;
    }
    message.channel = static_cast<ChannelId>(prefix.channel);
    message.from = prefix.from;
    message.peer = prefix.peer;
    message.msg_id = prefix.message_id;
    message.timestamp = prefix.timestamp;
    message.team_location_icon = prefix.team_location_icon;
    message.has_geo = (prefix.flags & kMessageHasGeo) != 0;
    message.geo_lat_e7 = prefix.geo_lat_e7;
    message.geo_lon_e7 = prefix.geo_lon_e7;
    message.source_unverified =
        (prefix.flags & kMessageSourceUnverified) != 0;
    message.rx_origin = static_cast<RxOrigin>(prefix.rx_origin);
    message.status = static_cast<MessageStatus>(prefix.status);
    if (out_sequence)
    {
        *out_sequence = prefix.sequence;
    }
    return true;
}

template <typename T>
bool encodeNodeMessage(const ChatMessage& message,
                       uint32_t sequence,
                       void* out,
                       std::size_t out_len)
{
    T slot{};
    if (!out || out_len != sizeof(T) ||
        message.text.size() > sizeof(slot.text))
    {
        return false;
    }
    encodeMessagePrefix(slot.prefix, message, sequence, message.text.size());
    if (!message.text.empty())
    {
        std::memcpy(slot.text, message.text.data(), message.text.size());
    }
    slot.crc = crc32(&slot, sizeof(slot) - sizeof(slot.crc));
    std::memcpy(out, &slot, sizeof(slot));
    return true;
}

template <typename T>
bool decodeNodeMessage(MeshProtocol protocol,
                       const void* data,
                       std::size_t len,
                       ChatMessage& message,
                       uint32_t* out_sequence)
{
    if (!data || len != sizeof(T))
    {
        return false;
    }
    T slot{};
    std::memcpy(&slot, data, sizeof(slot));
    if (!validSlot(slot, kMessageMagic) ||
        !decodeMessagePrefix(slot.prefix,
                             sizeof(slot.text),
                             message,
                             out_sequence))
    {
        return false;
    }
    message.protocol = protocol;
    message.text.assign(slot.text, slot.prefix.text_len);
    return true;
}

void encodeCatalogPrefix(CatalogPrefix& prefix,
                         const ChatCatalogProjection& projection)
{
    prefix = CatalogPrefix{};
    prefix.channel = static_cast<uint8_t>(projection.conversation.channel);
    prefix.peer = projection.conversation.peer;
    prefix.last_message_id = projection.last_message_id;
    prefix.message_count = projection.message_count;
    prefix.last_sequence = projection.last_sequence;
    prefix.last_timestamp = projection.last_timestamp;
    prefix.unread = projection.unread;
    prefix.last_status = static_cast<uint8_t>(projection.last_status);
    if (projection.deleted)
    {
        prefix.flags |= kProjectionDeleted;
    }
    const std::size_t preview_len =
        std::min<std::size_t>(std::strlen(projection.preview),
                              kCatalogPreviewMax);
    prefix.preview_len = static_cast<uint8_t>(preview_len);
    if (preview_len > 0)
    {
        std::memcpy(prefix.preview, projection.preview, preview_len);
    }
}

bool decodeCatalogPrefix(MeshProtocol protocol,
                         const CatalogPrefix& prefix,
                         ChatCatalogProjection& projection)
{
    if (prefix.magic != kCatalogMagic ||
        prefix.schema != kStorageSchemaVersion ||
        prefix.preview_len > kCatalogPreviewMax ||
        !validStatus(prefix.last_status))
    {
        return false;
    }
    projection = ChatCatalogProjection{};
    projection.conversation.protocol = protocol;
    projection.conversation.channel = static_cast<ChannelId>(prefix.channel);
    projection.conversation.peer = prefix.peer;
    projection.last_message_id = prefix.last_message_id;
    projection.message_count = prefix.message_count;
    projection.last_sequence = prefix.last_sequence;
    projection.last_timestamp = prefix.last_timestamp;
    projection.unread = prefix.unread;
    projection.last_status = static_cast<MessageStatus>(prefix.last_status);
    projection.deleted = (prefix.flags & kProjectionDeleted) != 0;
    std::memcpy(projection.preview, prefix.preview, prefix.preview_len);
    projection.preview[prefix.preview_len] = '\0';
    return true;
}

void encodeReadPrefix(ReadPrefix& prefix,
                      const ChatReadProjection& projection)
{
    prefix = ReadPrefix{};
    prefix.channel = static_cast<uint8_t>(projection.conversation.channel);
    prefix.peer = projection.conversation.peer;
    prefix.last_read_sequence = projection.last_read_sequence;
    if (projection.deleted)
    {
        prefix.flags |= kProjectionDeleted;
    }
}

bool decodeReadPrefix(MeshProtocol protocol,
                      const ReadPrefix& prefix,
                      ChatReadProjection& projection)
{
    if (prefix.magic != kReadMagic || prefix.schema != kStorageSchemaVersion)
    {
        return false;
    }
    projection = ChatReadProjection{};
    projection.conversation.protocol = protocol;
    projection.conversation.channel = static_cast<ChannelId>(prefix.channel);
    projection.conversation.peer = prefix.peer;
    projection.last_read_sequence = prefix.last_read_sequence;
    projection.deleted = (prefix.flags & kProjectionDeleted) != 0;
    return true;
}

} // namespace

uint32_t crc32(const void* data, std::size_t len)
{
    return ::sys::crc32(data, len);
}

MeshProtocol canonicalProtocol(MeshProtocol protocol)
{
    return protocol == MeshProtocol::RNode ? MeshProtocol::Reticulum : protocol;
}

bool supportedProtocol(MeshProtocol protocol)
{
    switch (canonicalProtocol(protocol))
    {
    case MeshProtocol::Meshtastic:
    case MeshProtocol::MeshCore:
    case MeshProtocol::Reticulum:
        return true;
    default:
        return false;
    }
}

std::size_t messageSlotSize(MeshProtocol protocol)
{
    switch (canonicalProtocol(protocol))
    {
    case MeshProtocol::Meshtastic:
        return sizeof(MeshtasticMessageSlot);
    case MeshProtocol::MeshCore:
        return sizeof(MeshCoreMessageSlot);
    case MeshProtocol::Reticulum:
        return sizeof(ReticulumMessageSlot);
    default:
        return 0;
    }
}

std::size_t catalogSlotSize(MeshProtocol protocol)
{
    return canonicalProtocol(protocol) == MeshProtocol::Reticulum
               ? sizeof(ReticulumCatalogSlot)
           : supportedProtocol(protocol) ? sizeof(NodeCatalogSlot)
                                         : 0;
}

std::size_t readStateSlotSize(MeshProtocol protocol)
{
    return canonicalProtocol(protocol) == MeshProtocol::Reticulum
               ? sizeof(ReticulumReadSlot)
           : supportedProtocol(protocol) ? sizeof(NodeReadSlot)
                                         : 0;
}

std::size_t statusSlotSize()
{
    return sizeof(StatusSlot);
}

std::size_t reticulumSeenSlotSize()
{
    return sizeof(SeenSlot);
}

bool encodeMessageSlot(const ChatMessage& message,
                       uint32_t sequence,
                       void* out,
                       std::size_t out_len)
{
    switch (canonicalProtocol(message.protocol))
    {
    case MeshProtocol::Meshtastic:
        return encodeNodeMessage<MeshtasticMessageSlot>(message,
                                                        sequence,
                                                        out,
                                                        out_len);
    case MeshProtocol::MeshCore:
        return encodeNodeMessage<MeshCoreMessageSlot>(message,
                                                      sequence,
                                                      out,
                                                      out_len);
    case MeshProtocol::Reticulum:
    {
        if (!out || out_len != sizeof(ReticulumMessageSlot) ||
            message.text.size() > kReticulumTextMax)
        {
            return false;
        }
        ReticulumMessageSlot slot{};
        encodeMessagePrefix(slot.prefix,
                            message,
                            sequence,
                            message.text.size());
        if (hasReticulumDestinationIdentity(message.reticulum_identity))
        {
            slot.prefix.flags |= kMessageHasReticulumIdentity;
            std::memcpy(slot.destination_hash,
                        message.reticulum_identity.destination_hash,
                        sizeof(slot.destination_hash));
            std::memcpy(slot.identity_hash,
                        message.reticulum_identity.identity_hash,
                        sizeof(slot.identity_hash));
        }
        if (hasReticulumLxmfMessageHash(message))
        {
            slot.prefix.flags |= kMessageHasLxmfHash;
            std::memcpy(slot.lxmf_hash,
                        message.reticulum_lxmf_hash,
                        sizeof(slot.lxmf_hash));
        }
        if (!message.text.empty())
        {
            std::memcpy(slot.text,
                        message.text.data(),
                        message.text.size());
        }
        slot.crc = crc32(&slot, sizeof(slot) - sizeof(slot.crc));
        std::memcpy(out, &slot, sizeof(slot));
        return true;
    }
    default:
        return false;
    }
}

bool decodeMessageSlot(MeshProtocol protocol,
                       const void* data,
                       std::size_t len,
                       ChatMessage& out_message,
                       uint32_t* out_sequence)
{
    protocol = canonicalProtocol(protocol);
    if (protocol == MeshProtocol::Meshtastic)
    {
        return decodeNodeMessage<MeshtasticMessageSlot>(protocol,
                                                        data,
                                                        len,
                                                        out_message,
                                                        out_sequence);
    }
    if (protocol == MeshProtocol::MeshCore)
    {
        return decodeNodeMessage<MeshCoreMessageSlot>(protocol,
                                                      data,
                                                      len,
                                                      out_message,
                                                      out_sequence);
    }
    if (protocol != MeshProtocol::Reticulum || !data ||
        len != sizeof(ReticulumMessageSlot))
    {
        return false;
    }
    ReticulumMessageSlot slot{};
    std::memcpy(&slot, data, sizeof(slot));
    if (!validSlot(slot, kMessageMagic) ||
        !decodeMessagePrefix(slot.prefix,
                             sizeof(slot.text),
                             out_message,
                             out_sequence))
    {
        return false;
    }
    out_message.protocol = protocol;
    out_message.text.assign(slot.text, slot.prefix.text_len);
    if ((slot.prefix.flags & kMessageHasReticulumIdentity) != 0)
    {
        out_message.reticulum_identity =
            makeReticulumPeerIdentity(slot.destination_hash,
                                      slot.identity_hash);
    }
    if ((slot.prefix.flags & kMessageHasLxmfHash) != 0)
    {
        out_message.has_reticulum_lxmf_hash = true;
        std::memcpy(out_message.reticulum_lxmf_hash,
                    slot.lxmf_hash,
                    sizeof(slot.lxmf_hash));
    }
    return true;
}

bool encodeCatalogSlot(MeshProtocol protocol,
                       const ChatCatalogProjection& projection,
                       void* out,
                       std::size_t out_len)
{
    protocol = canonicalProtocol(protocol);
    if (protocol == MeshProtocol::Reticulum)
    {
        if (!out || out_len != sizeof(ReticulumCatalogSlot))
        {
            return false;
        }
        ReticulumCatalogSlot slot{};
        encodeCatalogPrefix(slot.prefix, projection);
        if (hasReticulumDestinationIdentity(
                projection.conversation.reticulum_identity))
        {
            slot.prefix.flags |= kProjectionHasReticulumIdentity;
            std::memcpy(slot.destination_hash,
                        projection.conversation.reticulum_identity
                            .destination_hash,
                        sizeof(slot.destination_hash));
            std::memcpy(slot.identity_hash,
                        projection.conversation.reticulum_identity
                            .identity_hash,
                        sizeof(slot.identity_hash));
        }
        slot.crc = crc32(&slot, sizeof(slot) - sizeof(slot.crc));
        std::memcpy(out, &slot, sizeof(slot));
        return true;
    }
    if (!supportedProtocol(protocol) || !out ||
        out_len != sizeof(NodeCatalogSlot))
    {
        return false;
    }
    NodeCatalogSlot slot{};
    encodeCatalogPrefix(slot.prefix, projection);
    slot.crc = crc32(&slot, sizeof(slot) - sizeof(slot.crc));
    std::memcpy(out, &slot, sizeof(slot));
    return true;
}

bool decodeCatalogSlot(MeshProtocol protocol,
                       const void* data,
                       std::size_t len,
                       ChatCatalogProjection& out_projection)
{
    protocol = canonicalProtocol(protocol);
    if (protocol == MeshProtocol::Reticulum)
    {
        if (!data || len != sizeof(ReticulumCatalogSlot))
        {
            return false;
        }
        ReticulumCatalogSlot slot{};
        std::memcpy(&slot, data, sizeof(slot));
        if (!validSlot(slot, kCatalogMagic) ||
            !decodeCatalogPrefix(protocol, slot.prefix, out_projection))
        {
            return false;
        }
        if ((slot.prefix.flags & kProjectionHasReticulumIdentity) != 0)
        {
            out_projection.conversation.reticulum_identity =
                makeReticulumPeerIdentity(slot.destination_hash,
                                          slot.identity_hash);
        }
        return true;
    }
    if (!data || len != sizeof(NodeCatalogSlot) ||
        !supportedProtocol(protocol))
    {
        return false;
    }
    NodeCatalogSlot slot{};
    std::memcpy(&slot, data, sizeof(slot));
    return validSlot(slot, kCatalogMagic) &&
           decodeCatalogPrefix(protocol, slot.prefix, out_projection);
}

bool encodeReadStateSlot(MeshProtocol protocol,
                         const ChatReadProjection& projection,
                         void* out,
                         std::size_t out_len)
{
    protocol = canonicalProtocol(protocol);
    if (protocol == MeshProtocol::Reticulum)
    {
        if (!out || out_len != sizeof(ReticulumReadSlot) ||
            !hasReticulumDestinationIdentity(
                projection.conversation.reticulum_identity))
        {
            return false;
        }
        ReticulumReadSlot slot{};
        encodeReadPrefix(slot.prefix, projection);
        slot.prefix.flags |= kProjectionHasReticulumIdentity;
        std::memcpy(slot.destination_hash,
                    projection.conversation.reticulum_identity
                        .destination_hash,
                    sizeof(slot.destination_hash));
        slot.crc = crc32(&slot, sizeof(slot) - sizeof(slot.crc));
        std::memcpy(out, &slot, sizeof(slot));
        return true;
    }
    if (!supportedProtocol(protocol) || !out ||
        out_len != sizeof(NodeReadSlot))
    {
        return false;
    }
    NodeReadSlot slot{};
    encodeReadPrefix(slot.prefix, projection);
    slot.crc = crc32(&slot, sizeof(slot) - sizeof(slot.crc));
    std::memcpy(out, &slot, sizeof(slot));
    return true;
}

bool decodeReadStateSlot(MeshProtocol protocol,
                         const void* data,
                         std::size_t len,
                         ChatReadProjection& out_projection)
{
    protocol = canonicalProtocol(protocol);
    if (protocol == MeshProtocol::Reticulum)
    {
        if (!data || len != sizeof(ReticulumReadSlot))
        {
            return false;
        }
        ReticulumReadSlot slot{};
        std::memcpy(&slot, data, sizeof(slot));
        if (!validSlot(slot, kReadMagic) ||
            !decodeReadPrefix(protocol, slot.prefix, out_projection) ||
            (slot.prefix.flags & kProjectionHasReticulumIdentity) == 0)
        {
            return false;
        }
        out_projection.conversation.reticulum_identity =
            makeReticulumDestinationIdentity(slot.destination_hash);
        return true;
    }
    if (!data || len != sizeof(NodeReadSlot) ||
        !supportedProtocol(protocol))
    {
        return false;
    }
    NodeReadSlot slot{};
    std::memcpy(&slot, data, sizeof(slot));
    return validSlot(slot, kReadMagic) &&
           decodeReadPrefix(protocol, slot.prefix, out_projection);
}

bool encodeStatusSlot(const ChatStatusProjection& projection,
                      void* out,
                      std::size_t out_len)
{
    if (!out || out_len != sizeof(StatusSlot) ||
        projection.message_id == 0)
    {
        return false;
    }
    StatusSlot slot{};
    slot.status = static_cast<uint8_t>(projection.status);
    slot.message_id = projection.message_id;
    slot.sequence = projection.sequence;
    slot.crc = crc32(&slot, sizeof(slot) - sizeof(slot.crc));
    std::memcpy(out, &slot, sizeof(slot));
    return true;
}

bool decodeStatusSlot(const void* data,
                      std::size_t len,
                      ChatStatusProjection& out_projection)
{
    if (!data || len != sizeof(StatusSlot))
    {
        return false;
    }
    StatusSlot slot{};
    std::memcpy(&slot, data, sizeof(slot));
    if (!validStatusSlot(slot))
    {
        return false;
    }
    out_projection.message_id = slot.message_id;
    out_projection.status = static_cast<MessageStatus>(slot.status);
    out_projection.sequence = slot.sequence;
    return true;
}

bool encodeReticulumSeenSlot(const ReticulumSeenProjection& projection,
                             void* out,
                             std::size_t out_len)
{
    if (!out || out_len != sizeof(SeenSlot) ||
        isAllZeroKeyBytes(projection.hash, sizeof(projection.hash)))
    {
        return false;
    }
    SeenSlot slot{};
    std::memcpy(slot.hash, projection.hash, sizeof(slot.hash));
    slot.crc = crc32(&slot, sizeof(slot) - sizeof(slot.crc));
    std::memcpy(out, &slot, sizeof(slot));
    return true;
}

bool decodeReticulumSeenSlot(const void* data,
                             std::size_t len,
                             ReticulumSeenProjection& out_projection)
{
    if (!data || len != sizeof(SeenSlot))
    {
        return false;
    }
    SeenSlot slot{};
    std::memcpy(&slot, data, sizeof(slot));
    if (!validSeenSlot(slot))
    {
        return false;
    }
    std::memcpy(out_projection.hash, slot.hash, sizeof(slot.hash));
    return true;
}

} // namespace chat::storage::v2
