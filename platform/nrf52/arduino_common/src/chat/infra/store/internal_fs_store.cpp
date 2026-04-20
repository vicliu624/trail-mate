#include "platform/nrf52/arduino_common/chat/infra/store/internal_fs_store.h"

#include <InternalFileSystem.h>

#include "sys/clock.h"
#include <algorithm>
#include <cstring>
#include <string>

namespace platform::nrf52::arduino_common::chat::infra::store
{
namespace
{
using Adafruit_LittleFS_Namespace::FILE_O_READ;
using Adafruit_LittleFS_Namespace::FILE_O_WRITE;

constexpr const char* kTempSuffix = ".tmp";
} // namespace

InternalFsStore::InternalFsStore(const char* path)
    : path_(path)
{
    (void)loadFromFs();
}

void InternalFsStore::append(const ::chat::ChatMessage& msg)
{
    if (total_message_count_ >= kMaxMessagesTotal)
    {
        evictOldestMessage();
    }

    ConversationStorage& storage = getConversationStorage(::chat::ConversationId(msg.channel, msg.peer, msg.protocol));
    StoredMessageEntry entry;
    entry.message = msg;
    entry.sequence = next_sequence_++;
    storage.messages.push_back(entry);
    total_message_count_++;
    if (msg.status == ::chat::MessageStatus::Incoming)
    {
        storage.unread_count++;
    }
    markDirty();
    maybeSave();
}

std::vector<::chat::ChatMessage> InternalFsStore::loadRecent(const ::chat::ConversationId& conv, size_t n)
{
    const ConversationStorage& storage = getConversationStorage(conv);
    std::vector<::chat::ChatMessage> result;

    size_t count = storage.messages.size();
    size_t start = (count > n) ? (count - n) : 0;
    for (size_t i = start; i < count; ++i)
    {
        result.push_back(storage.messages[i].message);
    }
    return result;
}

std::vector<::chat::ConversationMeta> InternalFsStore::loadConversationPage(size_t offset,
                                                                            size_t limit,
                                                                            size_t* total)
{
    std::vector<::chat::ConversationMeta> list;
    list.reserve(conversations_.size());

    for (const auto& pair : conversations_)
    {
        const auto& conv = pair.first;
        const auto& storage = pair.second;
        const size_t count = storage.messages.size();
        if (count == 0)
        {
            continue;
        }

        ::chat::ConversationMeta meta;
        meta.id = conv;
        meta.preview = storage.messages.back().message.text;
        meta.last_timestamp = storage.messages.back().message.timestamp;
        meta.unread = storage.unread_count;
        if (conv.peer == 0)
        {
            meta.name = "Broadcast";
        }
        else
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04lX", static_cast<unsigned long>(conv.peer & 0xFFFFUL));
            meta.name = buf;
        }
        list.push_back(meta);
    }

    std::sort(list.begin(), list.end(),
              [](const ::chat::ConversationMeta& a, const ::chat::ConversationMeta& b)
              {
                  return a.last_timestamp > b.last_timestamp;
              });

    if (total)
    {
        *total = list.size();
    }

    if (offset >= list.size())
    {
        return {};
    }
    if (limit == 0)
    {
        return std::vector<::chat::ConversationMeta>(list.begin() + static_cast<long>(offset), list.end());
    }

    const size_t end = std::min(list.size(), offset + limit);
    return std::vector<::chat::ConversationMeta>(list.begin() + static_cast<long>(offset),
                                                 list.begin() + static_cast<long>(end));
}

void InternalFsStore::setUnread(const ::chat::ConversationId& conv, int unread)
{
    getConversationStorage(conv).unread_count = unread;
    markDirty();
    maybeSave();
}

int InternalFsStore::getUnread(const ::chat::ConversationId& conv) const
{
    return getConversationStorage(conv).unread_count;
}

void InternalFsStore::clearConversation(const ::chat::ConversationId& conv)
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        return;
    }

    total_message_count_ -= std::min(total_message_count_, it->second.messages.size());
    it->second.messages.clear();
    it->second.unread_count = 0;
    conversations_.erase(it);
    markDirty();
    maybeSave(true);
}

void InternalFsStore::clearAll()
{
    conversations_.clear();
    total_message_count_ = 0;
    next_sequence_ = 1;
    dirty_ = false;
    pending_write_count_ = 0;
    dirty_since_ms_ = 0;
    if (ensureFs() && path_ && InternalFS.exists(path_))
    {
        InternalFS.remove(path_);
    }
}

bool InternalFsStore::updateMessageStatus(::chat::MessageId msg_id, ::chat::MessageStatus status)
{
    if (msg_id == 0)
    {
        return false;
    }

    for (auto& pair : conversations_)
    {
        auto& storage = pair.second;
        const size_t count = storage.messages.size();
        for (size_t i = 0; i < count; ++i)
        {
            ::chat::ChatMessage* msg = &storage.messages[i].message;
            if (!msg || msg->msg_id != msg_id || msg->from != 0)
            {
                continue;
            }
            msg->status = status;
            markDirty();
            maybeSave();
            return true;
        }
    }

    return false;
}

bool InternalFsStore::getMessage(::chat::MessageId msg_id, ::chat::ChatMessage* out) const
{
    if (msg_id == 0)
    {
        return false;
    }

    for (const auto& pair : conversations_)
    {
        const auto& storage = pair.second;
        for (const auto& entry : storage.messages)
        {
            if (entry.message.msg_id != msg_id)
            {
                continue;
            }
            if (out)
            {
                *out = entry.message;
            }
            return true;
        }
    }

    return false;
}

bool InternalFsStore::ensureFs() const
{
    return path_ && InternalFS.begin();
}

bool InternalFsStore::loadFromFs()
{
    conversations_.clear();
    total_message_count_ = 0;
    next_sequence_ = 1;
    if (!ensureFs() || !InternalFS.exists(path_))
    {
        return false;
    }

    auto file = InternalFS.open(path_, FILE_O_READ);
    if (!file)
    {
        return false;
    }

    LegacyFileHeader legacy_header{};
    if (file.read(&legacy_header, sizeof(legacy_header)) != sizeof(legacy_header) || legacy_header.magic != kMagic)
    {
        file.close();
        return false;
    }

    const bool legacy_v1 = legacy_header.version == 1;
    const bool current_v2 = legacy_header.version == kVersion;
    if (!legacy_v1 && !current_v2)
    {
        file.close();
        return false;
    }

    uint16_t conversation_count = legacy_header.conversation_count;
    if (current_v2)
    {
        uint32_t persisted_next_sequence = 1U;
        if (file.read(&persisted_next_sequence, sizeof(persisted_next_sequence)) != sizeof(persisted_next_sequence))
        {
            file.close();
            return false;
        }
        next_sequence_ = std::max<uint32_t>(1U, persisted_next_sequence);
    }
    uint32_t recovered_sequence = 1U;

    for (uint16_t index = 0; index < conversation_count; ++index)
    {
        ConversationRecord conv_record{};
        if (file.read(&conv_record, sizeof(conv_record)) != sizeof(conv_record))
        {
            file.close();
            conversations_.clear();
            return false;
        }

        ::chat::ConversationId conv(static_cast<::chat::ChannelId>(conv_record.channel),
                                    conv_record.peer,
                                    static_cast<::chat::MeshProtocol>(conv_record.protocol));
        ConversationStorage& storage = getConversationStorage(conv);
        storage.unread_count = conv_record.unread_count;

        for (uint16_t message_index = 0; message_index < conv_record.message_count; ++message_index)
        {
            MessageRecord rec{};
            if (current_v2)
            {
                if (file.read(&rec, sizeof(rec)) != sizeof(rec))
                {
                    file.close();
                    conversations_.clear();
                    return false;
                }
            }
            else
            {
                LegacyMessageRecord legacy_rec{};
                if (file.read(&legacy_rec, sizeof(legacy_rec)) != sizeof(legacy_rec))
                {
                    file.close();
                    conversations_.clear();
                    return false;
                }
                rec.protocol = legacy_rec.protocol;
                rec.channel = legacy_rec.channel;
                rec.status = legacy_rec.status;
                rec.flags = legacy_rec.flags;
                rec.from = legacy_rec.from;
                rec.peer = legacy_rec.peer;
                rec.msg_id = legacy_rec.msg_id;
                rec.timestamp = legacy_rec.timestamp;
                rec.team_location_icon = legacy_rec.team_location_icon;
                rec.geo_lat_e7 = legacy_rec.geo_lat_e7;
                rec.geo_lon_e7 = legacy_rec.geo_lon_e7;
                rec.text_len = legacy_rec.text_len;
                std::memcpy(rec.text, legacy_rec.text, sizeof(rec.text));
            }

            ::chat::ChatMessage msg;
            msg.protocol = static_cast<::chat::MeshProtocol>(rec.protocol);
            msg.channel = static_cast<::chat::ChannelId>(rec.channel);
            msg.from = rec.from;
            msg.peer = rec.peer;
            msg.msg_id = rec.msg_id;
            msg.timestamp = rec.timestamp;
            msg.team_location_icon = rec.team_location_icon;
            msg.has_geo = (rec.flags & 0x01U) != 0;
            msg.geo_lat_e7 = rec.geo_lat_e7;
            msg.geo_lon_e7 = rec.geo_lon_e7;
            msg.status = static_cast<::chat::MessageStatus>(rec.status);
            msg.text.assign(rec.text, std::min<size_t>(rec.text_len, sizeof(rec.text)));
            const uint32_t sequence = current_v2 ? rec.sequence : recovered_sequence++;
            StoredMessageEntry entry;
            entry.message = msg;
            entry.sequence = sequence;
            storage.messages.push_back(entry);
            total_message_count_++;
            if (sequence >= next_sequence_)
            {
                next_sequence_ = sequence + 1U;
            }
        }
    }

    file.close();
    while (total_message_count_ > kMaxMessagesTotal)
    {
        evictOldestMessage();
    }
    dirty_ = false;
    pending_write_count_ = 0;
    dirty_since_ms_ = 0;
    last_save_ms_ = sys::millis_now();
    return true;
}

bool InternalFsStore::saveToFs() const
{
    if (!ensureFs())
    {
        return false;
    }

    std::string temp_path = std::string(path_) + kTempSuffix;
    if (InternalFS.exists(temp_path.c_str()))
    {
        InternalFS.remove(temp_path.c_str());
    }

    auto file = InternalFS.open(temp_path.c_str(), FILE_O_WRITE);
    if (!file)
    {
        return false;
    }

    FileHeader header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.conversation_count = static_cast<uint16_t>(std::min<size_t>(conversations_.size(), 0xFFFF));
    header.next_sequence = next_sequence_;
    if (file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) != sizeof(header))
    {
        file.close();
        InternalFS.remove(temp_path.c_str());
        return false;
    }

    for (const auto& pair : conversations_)
    {
        const auto& conv = pair.first;
        const auto& storage = pair.second;
        ConversationRecord conv_record{};
        conv_record.protocol = static_cast<uint8_t>(conv.protocol);
        conv_record.channel = static_cast<uint8_t>(conv.channel);
        conv_record.peer = conv.peer;
        conv_record.unread_count = storage.unread_count;
        conv_record.message_count = static_cast<uint16_t>(std::min<size_t>(storage.messages.size(), 0xFFFF));
        if (file.write(reinterpret_cast<const uint8_t*>(&conv_record), sizeof(conv_record)) != sizeof(conv_record))
        {
            file.close();
            InternalFS.remove(temp_path.c_str());
            return false;
        }

        for (size_t i = 0; i < storage.messages.size(); ++i)
        {
            const StoredMessageEntry& entry = storage.messages[i];
            const ::chat::ChatMessage* msg = &entry.message;

            MessageRecord rec{};
            rec.protocol = static_cast<uint8_t>(msg->protocol);
            rec.channel = static_cast<uint8_t>(msg->channel);
            rec.status = static_cast<uint8_t>(msg->status);
            rec.flags = msg->has_geo ? 0x01U : 0x00U;
            rec.from = msg->from;
            rec.peer = msg->peer;
            rec.msg_id = msg->msg_id;
            rec.timestamp = msg->timestamp;
            rec.sequence = entry.sequence;
            rec.team_location_icon = msg->team_location_icon;
            rec.geo_lat_e7 = msg->geo_lat_e7;
            rec.geo_lon_e7 = msg->geo_lon_e7;
            rec.text_len = static_cast<uint16_t>(std::min<size_t>(msg->text.size(), sizeof(rec.text)));
            if (rec.text_len > 0)
            {
                std::memcpy(rec.text, msg->text.data(), rec.text_len);
            }

            if (file.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec)) != sizeof(rec))
            {
                file.close();
                InternalFS.remove(temp_path.c_str());
                return false;
            }
        }
    }

    file.flush();
    file.close();

    if (InternalFS.exists(path_))
    {
        InternalFS.remove(path_);
    }
    const bool renamed = InternalFS.rename(temp_path.c_str(), path_);
    if (renamed)
    {
        dirty_ = false;
        pending_write_count_ = 0;
        dirty_since_ms_ = 0;
        last_save_ms_ = sys::millis_now();
    }
    return renamed;
}

void InternalFsStore::flush()
{
    maybeSave(true);
}

void InternalFsStore::markDirty()
{
    if (!dirty_)
    {
        dirty_ = true;
        dirty_since_ms_ = sys::millis_now();
    }
    ++pending_write_count_;
}

void InternalFsStore::maybeSave(bool force)
{
    if (!dirty_)
    {
        return;
    }

    const uint32_t now_ms = sys::millis_now();
    const bool interval_elapsed =
        (dirty_since_ms_ != 0) && ((now_ms - dirty_since_ms_) >= kSaveIntervalMs);
    const bool too_many_pending = pending_write_count_ >= kMaxPendingWrites;
    if (!force && !interval_elapsed && !too_many_pending)
    {
        return;
    }

    (void)saveToFs();
}

void InternalFsStore::evictOldestMessage()
{
    auto oldest_it = conversations_.end();
    size_t oldest_index = 0;
    uint32_t oldest_sequence = 0;
    bool found = false;

    for (auto it = conversations_.begin(); it != conversations_.end(); ++it)
    {
        auto& messages = it->second.messages;
        for (size_t index = 0; index < messages.size(); ++index)
        {
            if (!found || messages[index].sequence < oldest_sequence)
            {
                oldest_it = it;
                oldest_index = index;
                oldest_sequence = messages[index].sequence;
                found = true;
            }
        }
    }

    if (!found)
    {
        total_message_count_ = 0;
        return;
    }

    auto& storage = oldest_it->second;
    const ::chat::ChatMessage removed = storage.messages[oldest_index].message;
    storage.messages.erase(storage.messages.begin() + static_cast<long>(oldest_index));
    if (removed.status == ::chat::MessageStatus::Incoming && storage.unread_count > 0)
    {
        storage.unread_count--;
    }
    if (storage.messages.empty())
    {
        conversations_.erase(oldest_it);
    }
    if (total_message_count_ > 0)
    {
        total_message_count_--;
    }
}

InternalFsStore::ConversationStorage& InternalFsStore::getConversationStorage(const ::chat::ConversationId& conv)
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        auto result = conversations_.emplace(conv, ConversationStorage{});
        return result.first->second;
    }
    return it->second;
}

const InternalFsStore::ConversationStorage& InternalFsStore::getConversationStorage(const ::chat::ConversationId& conv) const
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        static ConversationStorage empty;
        return empty;
    }
    return it->second;
}

} // namespace platform::nrf52::arduino_common::chat::infra::store
