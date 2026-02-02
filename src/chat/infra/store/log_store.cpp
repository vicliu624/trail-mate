/**
 * @file log_store.cpp
 * @brief Per-conversation ring log store (SD-based)
 */

#include "log_store.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace chat
{

bool LogStore::begin(fs::FS& fs)
{
    fs_ = &fs;
    if (!ensureDir())
    {
        fs_ = nullptr;
        return false;
    }
    std::vector<IndexEntry> entries;
    if (!readIndex(entries))
    {
        rebuildIndex();
    }
    return true;
}

void LogStore::append(const ChatMessage& msg)
{
    if (!fs_) return;
    if (!ensureDir()) return;

    ConversationId conv(msg.channel, msg.peer);
    char path[64];
    buildConversationPath(conv, path, sizeof(path));

    FileHeader header{};
    bool have_header = false;
    if (fs_->exists(path))
    {
        File rf = fs_->open(path, FILE_READ);
        if (rf)
        {
            have_header = loadFileHeader(rf, header);
            rf.close();
        }
    }

    if (!have_header)
    {
        File wf = fs_->open(path, FILE_WRITE);
        if (!wf)
        {
            return;
        }
        initFileHeader(wf);
        header.magic = kFileMagic;
        header.version = kVersion;
        header.head = 0;
        header.count = 0;
        wf.close();
    }

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

    File wf = fs_->open(path, FILE_WRITE);
    if (!wf)
    {
        return;
    }

    if (!writeRecord(wf, header.head, rec))
    {
        wf.close();
        return;
    }

    header.head = static_cast<uint16_t>((header.head + 1) % kMaxMessagesPerConv);
    if (header.count < kMaxMessagesPerConv)
    {
        header.count = static_cast<uint16_t>(header.count + 1);
    }

    wf.seek(0);
    wf.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
    wf.flush();
    wf.close();

    updateIndexForMessage(msg);
}

std::vector<ChatMessage> LogStore::loadRecent(const ConversationId& conv, size_t n)
{
    std::vector<ChatMessage> out;
    if (!fs_ || n == 0) return out;

    char path[64];
    buildConversationPath(conv, path, sizeof(path));
    if (!fs_->exists(path))
    {
        return out;
    }

    File rf = fs_->open(path, FILE_READ);
    if (!rf)
    {
        return out;
    }

    FileHeader header{};
    if (!loadFileHeader(rf, header))
    {
        rf.close();
        return out;
    }

    uint16_t count = header.count;
    if (count == 0)
    {
        rf.close();
        return out;
    }

    size_t to_read = std::min<size_t>(n, count);
    uint16_t start = static_cast<uint16_t>(
        (header.head + kMaxMessagesPerConv - to_read) % kMaxMessagesPerConv);

    out.reserve(to_read);
    for (size_t i = 0; i < to_read; ++i)
    {
        uint16_t slot = static_cast<uint16_t>((start + i) % kMaxMessagesPerConv);
        Record rec{};
        if (!readRecord(rf, slot, rec))
        {
            continue;
        }
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
    rf.close();

    return out;
}

std::vector<ConversationMeta> LogStore::loadConversationPage(size_t offset,
                                                             size_t limit,
                                                             size_t* total)
{
    std::vector<IndexEntry> entries;
    if (!ensureIndex(entries))
    {
        if (total)
        {
            *total = 0;
        }
        return {};
    }

    std::sort(entries.begin(), entries.end(),
              [](const IndexEntry& a, const IndexEntry& b)
              {
                  return a.last_timestamp > b.last_timestamp;
              });

    if (total)
    {
        *total = entries.size();
    }

    size_t start = offset;
    if (start >= entries.size())
    {
        return {};
    }
    size_t end = entries.size();
    if (limit != 0 && start + limit < end)
    {
        end = start + limit;
    }

    std::vector<ConversationMeta> list;
    list.reserve(end - start);
    for (size_t i = start; i < end; ++i)
    {
        const IndexEntry& entry = entries[i];
        ConversationMeta meta;
        meta.id.channel = static_cast<ChannelId>(entry.channel);
        meta.id.peer = entry.peer;
        meta.preview.assign(entry.preview, entry.preview_len);
        meta.last_timestamp = entry.last_timestamp;
        meta.unread = static_cast<int>(entry.unread);
        if (entry.peer == 0)
        {
            meta.name = "Broadcast";
        }
        else
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%04lX",
                     static_cast<unsigned long>(entry.peer & 0xFFFF));
            meta.name = buf;
        }
        list.push_back(meta);
    }

    return list;
}

void LogStore::setUnread(const ConversationId& conv, int unread)
{
    std::vector<IndexEntry> entries;
    if (!ensureIndex(entries))
    {
        return;
    }
    size_t idx = 0;
    if (!findIndexEntry(conv, entries, &idx))
    {
        return;
    }
    entries[idx].unread = static_cast<uint16_t>(std::max(0, unread));
    writeIndex(entries);
}

int LogStore::getUnread(const ConversationId& conv) const
{
    std::vector<IndexEntry> entries;
    if (!const_cast<LogStore*>(this)->readIndex(entries))
    {
        return 0;
    }
    for (const auto& entry : entries)
    {
        if (entry.peer == conv.peer &&
            entry.channel == static_cast<uint8_t>(conv.channel))
        {
            return entry.unread;
        }
    }
    return 0;
}

void LogStore::clearConversation(const ConversationId& conv)
{
    if (!fs_) return;
    char path[64];
    buildConversationPath(conv, path, sizeof(path));
    if (fs_->exists(path))
    {
        fs_->remove(path);
    }

    std::vector<IndexEntry> entries;
    if (!readIndex(entries))
    {
        return;
    }

    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const IndexEntry& entry)
                                 {
                                     return entry.peer == conv.peer &&
                                            entry.channel == static_cast<uint8_t>(conv.channel);
                                 }),
                  entries.end());
    writeIndex(entries);
}

void LogStore::clearAll()
{
    if (!fs_) return;
    if (fs_->exists(kIndexFile))
    {
        fs_->remove(kIndexFile);
    }

    File dir = fs_->open(kDir);
    if (!dir)
    {
        return;
    }
    File entry = dir.openNextFile();
    while (entry)
    {
        if (!entry.isDirectory())
        {
            const char* name = entry.name();
            if (name && strstr(name, ".log"))
            {
                fs_->remove(name);
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
}

bool LogStore::updateMessageStatus(MessageId msg_id, MessageStatus status)
{
    if (!fs_ || msg_id == 0)
    {
        return false;
    }

    std::vector<IndexEntry> entries;
    if (!readIndex(entries))
    {
        return false;
    }

    bool updated = false;
    for (auto& entry : entries)
    {
        ConversationId conv(static_cast<ChannelId>(entry.channel), entry.peer);
        char path[64];
        buildConversationPath(conv, path, sizeof(path));
        if (!fs_->exists(path))
        {
            continue;
        }
        File rf = fs_->open(path, FILE_READ);
        if (!rf)
        {
            continue;
        }
        FileHeader header{};
        if (!loadFileHeader(rf, header))
        {
            rf.close();
            continue;
        }
        bool updated_in_file = false;
        for (uint16_t i = 0; i < header.count; ++i)
        {
            uint16_t slot =
                static_cast<uint16_t>((header.head + kMaxMessagesPerConv - header.count + i) %
                                      kMaxMessagesPerConv);
            Record rec{};
            if (!readRecord(rf, slot, rec))
            {
                continue;
            }
            if (rec.msg_id != msg_id)
            {
                continue;
            }
            if (rec.from != 0)
            {
                continue;
            }
            rec.status = static_cast<uint8_t>(status);
            rf.close();
            File wf = fs_->open(path, FILE_WRITE);
            if (!wf)
            {
                return updated;
            }
            writeRecord(wf, slot, rec);
            wf.flush();
            wf.close();
            updated_in_file = true;
            break;
        }
        if (updated_in_file)
        {
            updated = true;
            if (entry.last_msg_id == msg_id)
            {
                entry.status = static_cast<uint8_t>(status);
            }
            break;
        }
        rf.close();
    }

    if (updated)
    {
        writeIndex(entries);
    }
    return updated;
}

bool LogStore::ensureDir()
{
    if (!fs_) return false;
    if (fs_->exists(kDir))
    {
        return true;
    }
    return fs_->mkdir(kDir);
}

bool LogStore::ensureIndex(std::vector<IndexEntry>& entries)
{
    if (readIndex(entries))
    {
        return true;
    }
    rebuildIndex();
    return readIndex(entries);
}

bool LogStore::writeIndex(const std::vector<IndexEntry>& entries)
{
    if (!fs_) return false;
    if (fs_->exists(kIndexFile))
    {
        fs_->remove(kIndexFile);
    }
    File wf = fs_->open(kIndexFile, FILE_WRITE);
    if (!wf)
    {
        return false;
    }
    IndexHeader header{};
    header.magic = kIndexMagic;
    header.version = kVersion;
    header.count = static_cast<uint16_t>(entries.size());
    wf.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
    for (const auto& entry : entries)
    {
        wf.write(reinterpret_cast<const uint8_t*>(&entry), sizeof(entry));
    }
    wf.flush();
    wf.close();
    return true;
}

bool LogStore::readIndex(std::vector<IndexEntry>& entries)
{
    entries.clear();
    if (!fs_ || !fs_->exists(kIndexFile))
    {
        return false;
    }
    File rf = fs_->open(kIndexFile, FILE_READ);
    if (!rf)
    {
        return false;
    }
    IndexHeader header{};
    size_t read = rf.read(reinterpret_cast<uint8_t*>(&header), sizeof(header));
    if (read != sizeof(header) || header.magic != kIndexMagic || header.version != kVersion)
    {
        rf.close();
        return false;
    }
    entries.resize(header.count);
    size_t expected = header.count * sizeof(IndexEntry);
    size_t got = rf.read(reinterpret_cast<uint8_t*>(entries.data()), expected);
    rf.close();
    if (got != expected)
    {
        entries.clear();
        return false;
    }
    return true;
}

bool LogStore::findIndexEntry(const ConversationId& conv,
                              std::vector<IndexEntry>& entries,
                              size_t* out_idx)
{
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (entries[i].peer == conv.peer &&
            entries[i].channel == static_cast<uint8_t>(conv.channel))
        {
            if (out_idx)
            {
                *out_idx = i;
            }
            return true;
        }
    }
    return false;
}

void LogStore::updateIndexForMessage(const ChatMessage& msg)
{
    std::vector<IndexEntry> entries;
    if (!ensureIndex(entries))
    {
        return;
    }

    ConversationId conv(msg.channel, msg.peer);
    size_t idx = 0;
    if (!findIndexEntry(conv, entries, &idx))
    {
        IndexEntry entry{};
        entry.channel = static_cast<uint8_t>(msg.channel);
        entry.peer = msg.peer;
        entries.push_back(entry);
        idx = entries.size() - 1;
    }

    IndexEntry& entry = entries[idx];
    entry.channel = static_cast<uint8_t>(msg.channel);
    entry.status = static_cast<uint8_t>(msg.status);
    entry.peer = msg.peer;
    entry.last_msg_id = msg.msg_id;
    entry.last_timestamp = msg.timestamp;
    entry.last_from = msg.from;
    entry.preview_len = static_cast<uint16_t>(std::min<size_t>(msg.text.size(), kPreviewLen));
    memset(entry.preview, 0, sizeof(entry.preview));
    if (entry.preview_len > 0)
    {
        memcpy(entry.preview, msg.text.data(), entry.preview_len);
    }
    if (msg.status == MessageStatus::Incoming)
    {
        entry.unread = static_cast<uint16_t>(entry.unread + 1);
    }
    writeIndex(entries);
}

void LogStore::rebuildIndex()
{
    if (!fs_) return;
    std::vector<IndexEntry> entries;

    File dir = fs_->open(kDir);
    if (!dir)
    {
        return;
    }

    File entry = dir.openNextFile();
    while (entry)
    {
        if (!entry.isDirectory())
        {
            const char* name = entry.name();
            if (name && strstr(name, ".log"))
            {
                FileHeader header{};
                if (!loadFileHeader(entry, header))
                {
                    entry = dir.openNextFile();
                    continue;
                }

                ChatMessage last_msg;
                bool have_last = false;
                for (uint16_t i = 0; i < header.count; ++i)
                {
                    uint16_t slot =
                        static_cast<uint16_t>((header.head + kMaxMessagesPerConv - header.count + i) %
                                              kMaxMessagesPerConv);
                    Record rec{};
                    if (!readRecord(entry, slot, rec))
                    {
                        continue;
                    }
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
                    if (!have_last || msg.timestamp >= last_msg.timestamp)
                    {
                        last_msg = msg;
                        have_last = true;
                    }
                }

                if (have_last)
                {
                    IndexEntry idx{};
                    idx.channel = static_cast<uint8_t>(last_msg.channel);
                    idx.status = static_cast<uint8_t>(last_msg.status);
                    idx.unread = 0;
                    idx.peer = last_msg.peer;
                    idx.last_msg_id = last_msg.msg_id;
                    idx.last_timestamp = last_msg.timestamp;
                    idx.last_from = last_msg.from;
                    idx.preview_len =
                        static_cast<uint16_t>(std::min<size_t>(last_msg.text.size(), kPreviewLen));
                    if (idx.preview_len > 0)
                    {
                        memcpy(idx.preview, last_msg.text.data(), idx.preview_len);
                    }
                    entries.push_back(idx);
                }
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
    writeIndex(entries);
}

bool LogStore::loadFileHeader(File& file, FileHeader& header)
{
    if (!file)
    {
        return false;
    }
    size_t size = file.size();
    if (size < sizeof(FileHeader))
    {
        return false;
    }
    file.seek(0);
    size_t read = file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header));
    if (read != sizeof(header))
    {
        return false;
    }
    return header.magic == kFileMagic && header.version == kVersion;
}

void LogStore::initFileHeader(File& file)
{
    FileHeader header{};
    header.magic = kFileMagic;
    header.version = kVersion;
    header.head = 0;
    header.count = 0;
    file.seek(0);
    file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
    file.flush();
}

bool LogStore::readRecord(File& file, uint16_t slot, Record& rec)
{
    size_t offset = sizeof(FileHeader) + static_cast<size_t>(slot) * sizeof(Record);
    if (file.size() < offset + sizeof(Record))
    {
        return false;
    }
    file.seek(offset);
    size_t read = file.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec));
    return read == sizeof(rec);
}

bool LogStore::writeRecord(File& file, uint16_t slot, const Record& rec)
{
    size_t offset = sizeof(FileHeader) + static_cast<size_t>(slot) * sizeof(Record);
    file.seek(offset);
    size_t written = file.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
    return written == sizeof(rec);
}

void LogStore::buildConversationPath(const ConversationId& conv,
                                     char* out,
                                     size_t out_len) const
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (conv.peer == 0)
    {
        const char* name = channelName(conv.channel);
        snprintf(out, out_len, "%s/broadcast_%s.log", kDir, name);
    }
    else
    {
        snprintf(out, out_len, "%s/n_%08lX.log", kDir,
                 static_cast<unsigned long>(conv.peer));
    }
}

const char* LogStore::channelName(ChannelId channel) const
{
    switch (channel)
    {
    case ChannelId::PRIMARY:
        return "LongFast";
    case ChannelId::SECONDARY:
        return "Squad";
    default:
        return "Unknown";
    }
}

} // namespace chat
