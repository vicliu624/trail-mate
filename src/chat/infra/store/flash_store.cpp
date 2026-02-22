/**
 * @file flash_store.cpp
 * @brief Flash-backed chat storage (Preferences)
 */

#include "flash_store.h"
#include <algorithm>
#include <cstring>
#include <map>

namespace chat
{

#ifndef FLASH_STORE_LOG_ENABLE
#define FLASH_STORE_LOG_ENABLE 0
#endif

#if FLASH_STORE_LOG_ENABLE
#define FLASH_STORE_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define FLASH_STORE_LOG(...)
#endif

FlashStore::FlashStore()
{
    ready_ = prefs_.begin(kPrefsNs, false);
    records_.resize(kMaxMessages);
    if (!ready_)
    {
        FLASH_STORE_LOG("[FlashStore] open failed ns=%s\n", kPrefsNs);
        return;
    }
    loadFromPrefs();
    FLASH_STORE_LOG("[FlashStore] ready=%d count=%u head=%u\n",
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
    rec.protocol = static_cast<uint8_t>(msg.protocol);
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

    FLASH_STORE_LOG("[FlashStore] append proto=%u ch=%u status=%u from=%08lX peer=%08lX ts=%lu len=%u\n",
                    static_cast<unsigned>(rec.protocol),
                    static_cast<unsigned>(rec.channel),
                    static_cast<unsigned>(rec.status),
                    static_cast<unsigned long>(rec.from),
                    static_cast<unsigned long>(rec.peer),
                    static_cast<unsigned long>(rec.timestamp),
                    static_cast<unsigned>(rec.text_len));
    records_[head_] = rec;
    persistRecord(head_);
    head_ = static_cast<uint16_t>((head_ + 1) % kMaxMessages);
    count_ = std::min<uint16_t>(static_cast<uint16_t>(count_ + 1), static_cast<uint16_t>(kMaxMessages));

    persistMeta();
    if (msg.status == MessageStatus::Incoming)
    {
        ConversationId conv(msg.channel, msg.peer, msg.protocol);
        unread_[conv] = unread_[conv] + 1;
    }
}

std::vector<ChatMessage> FlashStore::loadRecent(const ConversationId& conv, size_t n)
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
        if (static_cast<ChannelId>(rec.channel) != conv.channel)
        {
            continue;
        }
        if (static_cast<MeshProtocol>(rec.protocol) != conv.protocol)
        {
            continue;
        }
        if (rec.peer != conv.peer)
        {
            continue;
        }
        ChatMessage msg;
        msg.protocol = static_cast<MeshProtocol>(rec.protocol);
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

std::vector<ConversationMeta> FlashStore::loadConversationPage(size_t offset,
                                                               size_t limit,
                                                               size_t* total)
{
    std::vector<ConversationMeta> list;
    if (!ready_ || count_ == 0)
    {
        if (total)
        {
            *total = 0;
        }
        return list;
    }

    std::map<ConversationId, ChatMessage> last;
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
        msg.protocol = static_cast<MeshProtocol>(rec.protocol);
        msg.channel = static_cast<ChannelId>(rec.channel);
        msg.from = rec.from;
        msg.peer = rec.peer;
        msg.msg_id = rec.msg_id;
        msg.timestamp = rec.timestamp;
        msg.text.assign(rec.text, rec.text_len);
        msg.status = static_cast<MessageStatus>(rec.status);
        ConversationId conv(msg.channel, msg.peer, msg.protocol);
        auto it = last.find(conv);
        if (it == last.end() || it->second.timestamp <= msg.timestamp)
        {
            last[conv] = msg;
        }
    }

    list.reserve(last.size());
    for (const auto& pair : last)
    {
        const ConversationId& conv = pair.first;
        const ChatMessage& msg = pair.second;
        ConversationMeta meta;
        meta.id = conv;
        meta.preview = msg.text;
        meta.last_timestamp = msg.timestamp;
        auto unread_it = unread_.find(conv);
        meta.unread = (unread_it != unread_.end()) ? unread_it->second : 0;
        if (conv.peer == 0)
        {
            meta.name = "Broadcast";
        }
        else
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%04lX",
                     static_cast<unsigned long>(conv.peer & 0xFFFF));
            meta.name = buf;
        }
        list.push_back(meta);
    }
    std::sort(list.begin(), list.end(),
              [](const ConversationMeta& a, const ConversationMeta& b)
              {
                  return a.last_timestamp > b.last_timestamp;
              });
    if (total)
    {
        *total = list.size();
    }
    if (limit != 0 && offset < list.size())
    {
        size_t end = offset + limit;
        if (end > list.size())
        {
            end = list.size();
        }
        return std::vector<ConversationMeta>(list.begin() + static_cast<long>(offset),
                                             list.begin() + static_cast<long>(end));
    }
    if (offset >= list.size())
    {
        return {};
    }
    if (offset > 0)
    {
        return std::vector<ConversationMeta>(list.begin() + static_cast<long>(offset), list.end());
    }
    return list;
}

void FlashStore::setUnread(const ConversationId& conv, int unread)
{
    (void)ready_;
    unread_[conv] = unread;
}

int FlashStore::getUnread(const ConversationId& conv) const
{
    auto it = unread_.find(conv);
    if (it == unread_.end())
    {
        return 0;
    }
    return it->second;
}

void FlashStore::clearConversation(const ConversationId& conv)
{
    if (!ready_) return;
    for (size_t i = 0; i < kMaxMessages; ++i)
    {
        Record& rec = records_[i];
        if (static_cast<ChannelId>(rec.channel) == conv.channel &&
            static_cast<MeshProtocol>(rec.protocol) == conv.protocol &&
            rec.peer == conv.peer)
        {
            rec = {};
            persistRecord(static_cast<uint16_t>(i));
        }
    }
    unread_.erase(conv);
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
        if (rec.status == new_status)
        {
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
    size_t ver_written = prefs_.putUChar(kKeyVer, kVersion);
    size_t head_written = prefs_.putUShort(kKeyHead, head_);
    size_t count_written = prefs_.putUShort(kKeyCount, count_);
    if (ver_written == 0 || head_written == 0 || count_written == 0)
    {
        FLASH_STORE_LOG("[FlashStore] persistMeta failed ver=%u head=%u count=%u\n",
                        static_cast<unsigned>(ver_written),
                        static_cast<unsigned>(head_written),
                        static_cast<unsigned>(count_written));
    }
}

void FlashStore::persistRecord(uint16_t idx)
{
    if (idx >= kMaxMessages) return;
    char key[8];
    snprintf(key, sizeof(key), "m%03u", static_cast<unsigned int>(idx));
    size_t expected = sizeof(Record);
    size_t written = prefs_.putBytes(key, &records_[idx], expected);
    if (written != expected)
    {
        FLASH_STORE_LOG("[FlashStore] persistRecord failed idx=%u wrote=%u expected=%u\n",
                        static_cast<unsigned>(idx),
                        static_cast<unsigned>(written),
                        static_cast<unsigned>(expected));
    }
    else
    {
        size_t actual = prefs_.getBytesLength(key);
        if (actual != expected)
        {
            FLASH_STORE_LOG("[FlashStore] persistRecord size mismatch idx=%u len=%u expected=%u\n",
                            static_cast<unsigned>(idx),
                            static_cast<unsigned>(actual),
                            static_cast<unsigned>(expected));
        }
    }
}

void FlashStore::clearAll()
{
    if (!ready_) return;
    head_ = 0;
    count_ = 0;
    std::fill(records_.begin(), records_.end(), Record{});
    unread_.clear();
    persistMeta();
}

} // namespace chat
