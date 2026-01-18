/**
 * @file log_store.h
 * @brief Append-only log + ring index chat storage (SD-based)
 */

#pragma once

#include "../../ports/i_chat_store.h"
#include <FS.h>
#include <SD.h>
#include <vector>
#include <array>

namespace chat {

/**
 * @brief SD-backed append-only log with per-channel ring index.
 *
 * Layout:
 * - Log file: /chat/chat.log (append-only records)
 * - Index per channel: /chat/idx_chX.bin (head A/B + 1000 slots)
 */
class LogStore : public IChatStore {
public:
    static constexpr size_t kSlotsPerChannel = 1000;
    static constexpr const char* kDir = "/chat";
    static constexpr const char* kLogFile = "/chat/chat.log";

    LogStore() : fs_(nullptr) {}
    ~LogStore() override = default;

    /**
     * @brief Initialize storage. Expects SD already mounted.
     * @param fs Filesystem (e.g., SD)
     * @return true if ready
     */
    bool begin(fs::FS& fs);

    void append(const ChatMessage& msg) override;
    std::vector<ChatMessage> loadRecent(ChannelId channel, size_t n) override;
    void setUnread(ChannelId channel, int unread) override;
    int getUnread(ChannelId channel) const override;
    void clearChannel(ChannelId channel) override;

private:
    fs::FS* fs_;

    struct RecordHeader {
        uint16_t magic;
        uint8_t ver;
        uint8_t flags;
        uint16_t channel;
        uint32_t peer;
        uint32_t timestamp;
        uint16_t payload_len;
    } __attribute__((packed));

    struct IndexHead {
        uint32_t seq;
        uint16_t next;
        uint16_t count;
        uint32_t last_offset;
        uint32_t crc;
    };

    struct IndexSlot {
        uint32_t offset;
        uint16_t len;
        uint32_t ts;
        uint16_t crc16;
        uint8_t flags;
        uint8_t reserved;
        uint32_t peer;
    };

    struct ChannelIndex {
        IndexHead head;
        std::array<IndexSlot, kSlotsPerChannel> slots;
    };

    ChannelIndex indexes_[2]; // PRIMARY, SECONDARY only

    bool loadIndex(ChannelId ch);
    void persistHead(ChannelId ch);
    void persistSlot(ChannelId ch, uint16_t idx);
    bool ensureDir();
    uint32_t crc32(const uint8_t* data, size_t len) const;
    uint16_t crc16(const uint8_t* data, size_t len) const;
    ChannelIndex& idx(ChannelId ch);
    const ChannelIndex& idx(ChannelId ch) const;
};

} // namespace chat
