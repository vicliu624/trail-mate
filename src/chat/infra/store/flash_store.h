/**
 * @file flash_store.h
 * @brief Flash-backed chat storage (Preferences)
 */

#pragma once

#include "../../ports/i_chat_store.h"
#include "../../domain/chat_types.h"
#include <Preferences.h>
#include <vector>

namespace chat {

class FlashStore : public IChatStore {
public:
    static constexpr size_t kMaxMessages = 300;
    static constexpr size_t kMaxTextLen = 220;

    FlashStore();
    ~FlashStore() override;

    bool isReady() const { return ready_; }

    void append(const ChatMessage& msg) override;
    std::vector<ChatMessage> loadRecent(ChannelId channel, size_t n) override;
    void setUnread(ChannelId channel, int unread) override;
    int getUnread(ChannelId channel) const override;
    void clearChannel(ChannelId channel) override;

    std::vector<ChatMessage> loadAll() const;

private:
    struct Record {
        uint8_t channel;
        uint8_t status;
        uint16_t text_len;
        uint32_t from;
        uint32_t peer;
        uint32_t msg_id;
        uint32_t timestamp;
        char text[kMaxTextLen];
    } __attribute__((packed));

    static constexpr const char* kPrefsNs = "chat_store";
    static constexpr const char* kKeyVer = "ver";
    static constexpr const char* kKeyHead = "head";
    static constexpr const char* kKeyCount = "count";
    static constexpr const char* kKeyRecords = "records";
    static constexpr uint8_t kVersion = 1;

    mutable Preferences prefs_;
    bool ready_ = false;
    uint16_t head_ = 0;
    uint16_t count_ = 0;
    std::vector<Record> records_;

    void loadFromPrefs();
    void persistMeta();
    void persistRecord(uint16_t idx);
    void clearAll();
};

} // namespace chat
