/**
 * @file vmp_voice_inbox.h
 * @brief Fixed local-only VMP voice-object store.
 *
 * This deliberately stores already-validated encoded media and has no mesh,
 * radio, MQTT, or forwarding API.  A VMP implementation can only persist,
 * present, or play an object through this type; it cannot re-originate an
 * accepted object onto any air interface.
 */

#pragma once

#include "chat/infra/voice/vmp_wire.h"

#include <cstddef>
#include <cstdint>

namespace chat::voice::vmp
{

// T-Deck's VMP integration is playback-only and must leave most of its PSRAM
// available for the display and map paths.  Retain only the newest two local
// voice objects there; Pager keeps its eight-message history.
#if defined(ARDUINO_T_DECK)
inline constexpr std::size_t kVoiceInboxCapacity = 2U;
#else
inline constexpr std::size_t kVoiceInboxCapacity = 8U;
#endif

/**
 * Local presentation state for a voice object.
 *
 * `Sent` means the selected one-hop carrier accepted the finished VMP object;
 * VMP intentionally has no post-media delivery receipt or resend protocol.
 */
enum class VoiceDeliveryState : uint8_t
{
    Received = 0U,
    Sending = 1U,
    Sent = 2U,
    Failed = 3U,
};

/**
 * Local chat-presentation binding for a VMP object.
 *
 * These values deliberately mirror the stable persisted values of
 * `chat::MeshProtocol` without making VMP a Meshtastic, MeshCore, or
 * Reticulum payload.  The binding only selects the local chat conversation
 * in which an accepted VMP object is displayed.
 */
enum class VoicePresentationProtocol : uint8_t
{
    Unknown = 0U,
    Meshtastic = 1U,
    MeshCore = 2U,
    Reticulum = 4U,
};

inline constexpr uint8_t kVoicePresentationPrimaryChannel = 0U;
inline constexpr uint8_t kVoicePresentationMaxChannel = 7U;

enum VoiceMessageFlag : uint8_t
{
    VoiceMessageFlagSourceUnverified = 1U << 0U,
    VoiceMessageFlagComplete = 1U << 1U,
    VoiceMessageFlagOutgoing = 1U << 2U,
    VoiceMessageFlagRead = 1U << 3U,
};

struct VoiceMessageMetadata
{
    uint64_t local_id = 0U;
    uint32_t sender_id = 0U;
    uint32_t target_id = 0U;
    uint64_t session_id = 0U;
    uint32_t object_fingerprint = 0U;
    uint32_t received_at_seconds = 0U;
    uint16_t encoded_media_len = 0U;
    Codec codec = Codec::Codec2_1300;
    DeliveryMode mode = DeliveryMode::Private;
    /** Packed source/complete/direction/read state; see VoiceMessageFlag. */
    uint8_t flags = 0U;
    VoiceDeliveryState delivery = VoiceDeliveryState::Received;
    VoicePresentationProtocol presentation_protocol =
        VoicePresentationProtocol::Unknown;
    uint8_t presentation_channel = kVoicePresentationPrimaryChannel;
};

// Eight metadata entries are projected to the chat UI. Keep status/direction
// extension budget-neutral rather than silently increasing Pager RAM use.
static_assert(sizeof(VoiceMessageMetadata) == 40U,
              "VMP metadata must remain within its fixed PSRAM budget");

inline bool voiceMessageHasFlag(const VoiceMessageMetadata& metadata,
                                VoiceMessageFlag flag)
{
    return (metadata.flags & static_cast<uint8_t>(flag)) != 0U;
}

inline void setVoiceMessageFlag(VoiceMessageMetadata* metadata,
                                VoiceMessageFlag flag,
                                bool enabled)
{
    if (!metadata)
    {
        return;
    }
    if (enabled)
    {
        metadata->flags |= static_cast<uint8_t>(flag);
    }
    else
    {
        metadata->flags &= static_cast<uint8_t>(~static_cast<uint8_t>(flag));
    }
}

inline bool voiceMessageSourceUnverified(const VoiceMessageMetadata& metadata)
{
    return voiceMessageHasFlag(metadata, VoiceMessageFlagSourceUnverified);
}

inline bool voiceMessageComplete(const VoiceMessageMetadata& metadata)
{
    return voiceMessageHasFlag(metadata, VoiceMessageFlagComplete);
}

/** True for a locally composed object; false for an accepted inbound object. */
inline bool voiceMessageOutgoing(const VoiceMessageMetadata& metadata)
{
    return voiceMessageHasFlag(metadata, VoiceMessageFlagOutgoing);
}

inline bool voiceMessageRead(const VoiceMessageMetadata& metadata)
{
    return voiceMessageHasFlag(metadata, VoiceMessageFlagRead);
}

inline bool isValidVoicePresentationBinding(VoicePresentationProtocol protocol,
                                            uint8_t channel)
{
    return protocol == VoicePresentationProtocol::Meshtastic ||
                   protocol == VoicePresentationProtocol::MeshCore ||
                   protocol == VoicePresentationProtocol::Reticulum
               ? channel <= kVoicePresentationMaxChannel
               : false;
}

struct VoiceMessageView
{
    VoiceMessageMetadata metadata{};
    const uint8_t* encoded_media = nullptr;
};

enum class VoiceInboxStoreResult : uint8_t
{
    Stored = 1,
    Duplicate = 2,
    Invalid = 3,
};

/**
 * @brief Bounded, replacement-on-oldest local VMP inbox.
 *
 * ESP callers must own this as static/runtime storage because each slot holds
 * one encoded voice object.  Replacing an old slot securely clears its encoded
 * media first.  Caller-owned durable storage may mirror accepted entries.
 */
class VoiceMessageInbox final
{
  public:
    VoiceInboxStoreResult store(const ControlFrame& control,
                                const uint8_t* encoded_media,
                                std::size_t encoded_media_len,
                                bool complete,
                                uint32_t received_at_seconds,
                                VoicePresentationProtocol presentation_protocol,
                                uint8_t presentation_channel,
                                uint64_t* out_local_id = nullptr);

    /**
     * Stores one locally composed, already-encoded voice object before carrier
     * transmission begins.  This creates the IM-style outgoing bubble without
     * duplicating the media buffer; caller updates its state after the carrier
     * attempt completes.
     */
    VoiceInboxStoreResult storeOutgoing(const ControlFrame& control,
                                        const uint8_t* encoded_media,
                                        std::size_t encoded_media_len,
                                        uint32_t created_at_seconds,
                                        VoicePresentationProtocol presentation_protocol,
                                        uint8_t presentation_channel,
                                        uint64_t* out_local_id = nullptr);

    /** Updates local-only delivery state for a previously stored outgoing object. */
    bool updateDeliveryState(uint64_t local_id, VoiceDeliveryState delivery);

    /** Marks accepted inbound objects in one local conversation as read. */
    bool markConversationRead(VoicePresentationProtocol protocol,
                              uint8_t channel,
                              uint32_t peer_id,
                              bool broadcast);

    /** Assigns a safe binding to legacy snapshots that predate this metadata. */
    bool bindUnassignedMessages(VoicePresentationProtocol protocol,
                                uint8_t channel,
                                bool mark_incoming_read);

    bool get(uint64_t local_id, VoiceMessageView* out_view) const;

    /**
     * @brief Copies metadata newest first without exposing encoded media.
     *
     * This is intentionally a presentation-only operation. The returned
     * metadata cannot be used to forward, serialize, or recreate a VMP radio
     * frame.
     */
    std::size_t listMetadata(VoiceMessageMetadata* out_metadata,
                             std::size_t capacity) const;

    /**
     * Restore one authenticated, completed local object from durable local
     * storage. This is intentionally not a wire ingress API: callers must
     * have already validated the record checksum and the object must have
     * originated from a previously accepted inbox entry.
     *
     * The original local ID is retained so a chat projection's playback
     * target survives a reboot. Restored records participate in the ordinary
     * duplicate check and bounded oldest-entry replacement policy.  A
     * nonzero `insertion_sequence` is supplied by a durable snapshot loader
     * when that format stores records newest-first; it preserves the original
     * time ordering without expanding the on-disk record ABI.
     */
    bool restore(const VoiceMessageMetadata& metadata,
                 const uint8_t* encoded_media,
                 std::size_t encoded_media_len,
                 uint64_t insertion_sequence = 0U);

    bool erase(uint64_t local_id);
    void clear();

    [[nodiscard]] std::size_t size() const { return size_; }
    /** Durable adapters retain IDs allocated to entries that were later erased. */
    [[nodiscard]] uint64_t nextLocalId() const { return next_local_id_; }
    /** Apply only after validating/restoring the complete durable snapshot. */
    bool restoreNextLocalId(uint64_t next)
    {
        if (next == 0U || next < next_local_id_) return false;
        next_local_id_ = next;
        return true;
    }

  private:
    struct Slot
    {
        VoiceMessageMetadata metadata{};
        uint8_t encoded_media[kMaxEncodedMediaSize] = {};
        uint64_t insertion_sequence = 0U;
        bool occupied = false;
    };

    bool isDuplicate(const ControlFrame& control) const;
    Slot* selectDestination();
    static void clearSlot(Slot* slot);

    Slot slots_[kVoiceInboxCapacity] = {};
    std::size_t size_ = 0U;
    uint64_t next_local_id_ = 1U;
    uint64_t next_insertion_sequence_ = 1U;
};

} // namespace chat::voice::vmp
