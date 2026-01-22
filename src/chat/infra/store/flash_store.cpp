/**
 * @file flash_store.cpp
 * @brief Flash-backed chat storage (Preferences)
 */

#include "flash_store.h"
#include <algorithm>
#include <cstring>

namespace chat
{

FlashStore::FlashStore()
{
    ready_ = prefs_.begin(kPrefsNs, false);
    records_.resize(kMaxMessages);
    if (!ready_)
    {
        Serial.printf("[FlashStore] open failed ns=%s\n", kPrefsNs);
        return;
    }
    loadFromPrefs();
    Serial.printf("[FlashStore] ready=%d count=%u head=%u\n",
                  ready_ ? 1 : 0,
                  static_cast<unsigned>(count_),
                  static_cast<unsigned>(head_));
}

FlashStore::~FlashStore()
{
    if (ready_)
    {
        prefs_.end();
    }
}

void FlashStore::append(const ChatMessage& msg)
{
    if (!ready_) return;

    Record rec{};
    rec.channel = static_cast<uint8_t>(msg.channel);
    rec.status = static_cast<uint8_t>(msg.status);
    rec.text_len = static_cast<uint16_t>(std::min<size_t>(msg.text.size(), kMaxTextLen));
    rec.from = msg.from;
    rec.peer = msg.peer;
    rec.msg_id = msg.msg_id;
    rec.timestamp = msg.timestamp;
    if (rec.text_len > 0)
    {
        memcpy(rec.text, msg.text.data(), rec.text_len);
    }

    records_[head_] = rec;
    persistRecord(head_);
    head_ = static_cast<uint16_t>((head_ + 1) % kMaxMessages);
    count_ = std::min<uint16_t>(static_cast<uint16_t>(count_ + 1), static_cast<uint16_t>(kMaxMessages));

    persistMeta();
}

std::vector<ChatMessage> FlashStore::loadRecent(ChannelId channel, size_t n)
{
    std::vector<ChatMessage> out;
    if (!ready_ || count_ == 0 || n == 0) return out;

    out.reserve(std::min<size_t>(n, count_));
    size_t collected = 0;

    for (size_t i = 0; i < count_ && collected < n; ++i)
    {
        size_t idx = (head_ + kMaxMessages - 1 - i) % kMaxMessages;
        const Record& rec = records_[idx];
        if (rec.text_len == 0)
        {
            continue;
        }
        if (static_cast<ChannelId>(rec.channel) != channel)
        {
            continue;
        }
        ChatMessage msg;
        msg.channel = static_cast<ChannelId>(rec.channel);
        msg.from = rec.from;
        msg.peer = rec.peer;
        msg.msg_id = rec.msg_id;
        msg.timestamp = rec.timestamp;
        msg.text.assign(rec.text, rec.text_len);
        msg.status = static_cast<MessageStatus>(rec.status);
        out.push_back(msg);
        collected++;
    }

    std::reverse(out.begin(), out.end());
    return out;
}

std::vector<ChatMessage> FlashStore::loadAll() const
{
    std::vector<ChatMessage> out;
    if (!ready_ || count_ == 0) return out;

    out.reserve(count_);
    size_t start = (head_ + kMaxMessages - count_) % kMaxMessages;
    for (size_t i = 0; i < count_; ++i)
    {
        size_t idx = (start + i) % kMaxMessages;
        const Record& rec = records_[idx];
        if (rec.text_len == 0)
        {
            continue;
        }
        ChatMessage msg;
        msg.channel = static_cast<ChannelId>(rec.channel);
        msg.from = rec.from;
        msg.peer = rec.peer;
        msg.msg_id = rec.msg_id;
        msg.timestamp = rec.timestamp;
        msg.text.assign(rec.text, rec.text_len);
        msg.status = static_cast<MessageStatus>(rec.status);
        out.push_back(msg);
    }
    return out;
}

void FlashStore::setUnread(ChannelId channel, int unread)
{
    if (!ready_) return;
    char key[16];
    snprintf(key, sizeof(key), "unread_%u", static_cast<uint8_t>(channel));
    prefs_.putInt(key, unread);
}

int FlashStore::getUnread(ChannelId channel) const
{
    if (!ready_) return 0;
    char key[16];
    snprintf(key, sizeof(key), "unread_%u", static_cast<uint8_t>(channel));
    return prefs_.getInt(key, 0);
}

void FlashStore::clearChannel(ChannelId channel)
{
    if (!ready_) return;
    for (size_t i = 0; i < kMaxMessages; ++i)
    {
        Record& rec = records_[i];
        if (static_cast<ChannelId>(rec.channel) == channel)
        {
            rec = {};
            persistRecord(static_cast<uint16_t>(i));
        }
    }
}

bool FlashStore::updateMessageStatus(MessageId msg_id, MessageStatus status)
{
    if (!ready_ || msg_id == 0) return false;

    for (size_t i = 0; i < count_; ++i)
    {
        size_t idx = (head_ + kMaxMessages - 1 - i) % kMaxMessages;
        Record& rec = records_[idx];
        if (rec.text_len == 0) continue;
        if (rec.msg_id != msg_id) continue;
        if (rec.from != 0) continue; // only update outgoing messages
        uint8_t new_status = static_cast<uint8_t>(status);
        if (rec.status == new_status) {
            return true;
        }
        rec.status = new_status;
        persistRecord(static_cast<uint16_t>(idx));
        return true;
    }
    return false;
}

void FlashStore::loadFromPrefs()
{
    uint8_t ver = prefs_.getUChar(kKeyVer, 0);
    if (ver != kVersion)
    {
        clearAll();
        return;
    }

    head_ = static_cast<uint16_t>(prefs_.getUShort(kKeyHead, 0));
    count_ = static_cast<uint16_t>(prefs_.getUShort(kKeyCount, 0));
    if (head_ >= kMaxMessages) head_ = 0;
    if (count_ > kMaxMessages) count_ = kMaxMessages;

    for (uint16_t i = 0; i < kMaxMessages; ++i)
    {
        char key[8];
        snprintf(key, sizeof(key), "m%03u", static_cast<unsigned int>(i));
        size_t actual = prefs_.getBytesLength(key);
        if (actual == sizeof(Record))
        {
            prefs_.getBytes(key, &records_[i], sizeof(Record));
        }
        else
        {
            records_[i] = {};
        }
    }
}

void FlashStore::persistMeta()
{
    prefs_.putUChar(kKeyVer, kVersion);
    prefs_.putUShort(kKeyHead, head_);
    prefs_.putUShort(kKeyCount, count_);
}

void FlashStore::persistRecord(uint16_t idx)
{
    if (idx >= kMaxMessages) return;
    char key[8];
    snprintf(key, sizeof(key), "m%03u", static_cast<unsigned int>(idx));
    prefs_.putBytes(key, &records_[idx], sizeof(Record));
}

void FlashStore::clearAll()
{
    if (!ready_) return;
    head_ = 0;
    count_ = 0;
    std::fill(records_.begin(), records_.end(), Record{});
    persistMeta();
}

} // namespace chat
