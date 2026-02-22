/**
 * @file log_store.h
 * @brief Per-conversation ring log store (SD-based)
 */

#pragma once

#include "../../ports/i_chat_store.h"
#include <FS.h>
#include <SD.h>
#include <array>
#include <vector>

namespace chat
{

/**
 * @brief SD-backed append-only log with per-channel ring index.
 *
 * Layout:
 * - Conversation file: /chat/n_<peer>.log or /chat/broadcast_<name>.log
 * - Index file: /chat/index.bin (conversation metadata)
 */
class LogStore : public IChatStore
{
  public:
    static constexpr const char* kDir = "/chat";
    static constexpr const char* kIndexFile = "/chat/index.bin";
    static constexpr size_t kMaxMessagesPerConv = 100;
    static constexpr size_t kMaxTextLen = 233;
    static constexpr size_t kPreviewLen = 48;

    LogStore() : fs_(nullptr) {}
    ~LogStore() override = default;

    /**
     * @brief Initialize storage. Expects SD already mounted.
     * @param fs Filesystem (e.g., SD)
     * @return true if ready
     */
    bool begin(fs::FS& fs);

    void append(const ChatMessage& msg) override;
    std::vector<ChatMessage> loadRecent(const ConversationId& conv, size_t n) override;
    std::vector<ConversationMeta> loadConversationPage(size_t offset,
                                                       size_t limit,
                                                       size_t* total) override;
    void setUnread(const ConversationId& conv, int unread) override;
    int getUnread(const ConversationId& conv) const override;
    void clearConversation(const ConversationId& conv) override;
    void clearAll() override;
    bool updateMessageStatus(MessageId msg_id, MessageStatus status) override;

  private:
    fs::FS* fs_;

    struct FileHeader
    {
        uint32_t magic;
        uint16_t version;
        uint16_t head;
        uint16_t count;
        uint16_t reserved;
    } __attribute__((packed));

    struct Record
    {
        uint8_t protocol;
        uint8_t channel;
        uint8_t status;
        uint16_t text_len;
        uint32_t from;
        uint32_t peer;
        uint32_t msg_id;
        uint32_t timestamp;
        char text[kMaxTextLen];
    } __attribute__((packed));

    struct IndexHeader
    {
        uint32_t magic;
        uint16_t version;
        uint16_t count;
    } __attribute__((packed));

    struct IndexEntry
    {
        uint8_t protocol;
        uint8_t channel;
        uint8_t status;
        uint16_t unread;
        uint32_t peer;
        uint32_t last_msg_id;
        uint32_t last_timestamp;
        uint32_t last_from;
        uint16_t preview_len;
        char preview[kPreviewLen];
    } __attribute__((packed));

    static constexpr uint32_t kFileMagic = 0x474F4C43;  // "CLOG"
    static constexpr uint32_t kIndexMagic = 0x54414843; // "CHAT"
    static constexpr uint16_t kVersion = 2;

    bool ensureDir();
    bool ensureIndex(std::vector<IndexEntry>& entries);
    bool writeIndex(const std::vector<IndexEntry>& entries);
    bool readIndex(std::vector<IndexEntry>& entries);
    bool findIndexEntry(const ConversationId& conv,
                        std::vector<IndexEntry>& entries,
                        size_t* out_idx);
    void updateIndexForMessage(const ChatMessage& msg);
    void rebuildIndex();
    bool loadFileHeader(File& file, FileHeader& header);
    void initFileHeader(File& file);
    bool readRecord(File& file, uint16_t slot, Record& rec);
    bool writeRecord(File& file, uint16_t slot, const Record& rec);
    void buildConversationPath(const ConversationId& conv,
                               char* out,
                               size_t out_len) const;
    const char* channelName(ChannelId channel) const;
};

} // namespace chat
