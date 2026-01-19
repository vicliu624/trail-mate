/**
 * @file log_store.cpp
 * @brief Append-only log + ring index chat storage (SD-based)
 */

#include "log_store.h"
#include <Arduino.h>
#include <cstring>

namespace chat
{

namespace
{
constexpr uint16_t kMagic = 0x434D; // "CM"
constexpr uint8_t kVersion = 1;
constexpr size_t kHeadCopies = 2; // A/B

const char* indexPath(ChannelId ch)
{
    switch (ch)
    {
    case ChannelId::PRIMARY:
        return "/chat/idx_ch0.bin";
    case ChannelId::SECONDARY:
        return "/chat/idx_ch1.bin";
    default:
        return "/chat/idx_other.bin";
    }
}
} // namespace

bool LogStore::begin(fs::FS& fs)
{
    fs_ = &fs;
    if (!ensureDir()) return false;

    loadIndex(ChannelId::PRIMARY);
    loadIndex(ChannelId::SECONDARY);
    return true;
}

void LogStore::append(const ChatMessage& msg)
{
    if (!fs_) return;
    File log = fs_->open(kLogFile, FILE_APPEND);
    if (!log) return;

    RecordHeader hdr{};
    hdr.magic = kMagic;
    hdr.ver = kVersion;
    hdr.flags = 0;
    hdr.channel = static_cast<uint16_t>(msg.channel);
    hdr.peer = msg.peer;
    hdr.timestamp = msg.timestamp;
    hdr.payload_len = static_cast<uint16_t>(msg.text.size());

    uint32_t offset = log.size();

    log.write(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr));
    if (hdr.payload_len > 0)
    {
        log.write(reinterpret_cast<const uint8_t*>(msg.text.data()), hdr.payload_len);
    }
    uint32_t crc = crc32(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    if (hdr.payload_len > 0)
    {
        crc = crc32(reinterpret_cast<const uint8_t*>(msg.text.data()), hdr.payload_len) ^ crc;
    }
    log.write(reinterpret_cast<uint8_t*>(&crc), sizeof(crc));
    log.flush();
    log.close();

    // Update index
    ChannelIndex& cx = idx(msg.channel);
    uint16_t slot_idx = cx.head.next;
    IndexSlot& slot = cx.slots[slot_idx];
    slot.offset = offset;
    slot.len = hdr.payload_len;
    slot.ts = hdr.timestamp;
    slot.flags = hdr.flags;
    slot.peer = msg.peer;
    slot.crc16 = crc16(reinterpret_cast<const uint8_t*>(&crc), sizeof(crc)); // lightweight check
    cx.head.next = (cx.head.next + 1) % kSlotsPerChannel;
    cx.head.count = std::min<uint16_t>(cx.head.count + 1, kSlotsPerChannel);
    cx.head.last_offset = offset;
    cx.head.seq++;
    persistSlot(msg.channel, slot_idx);
    persistHead(msg.channel);
}

std::vector<ChatMessage> LogStore::loadRecent(ChannelId channel, size_t n)
{
    std::vector<ChatMessage> out;
    if (!fs_) return out;
    const ChannelIndex& cx = idx(channel);
    if (cx.head.count == 0) return out;

    size_t total = std::min<size_t>(n, cx.head.count);
    out.reserve(total);

    File log = fs_->open(kLogFile, FILE_READ);
    if (!log) return out;

    for (size_t i = 0; i < total; ++i)
    {
        int idx_pos = (cx.head.next + kSlotsPerChannel - 1 - i) % kSlotsPerChannel;
        const IndexSlot& slot = cx.slots[idx_pos];
        if (slot.len == 0) continue;
        if (!log.seek(slot.offset)) continue;
        RecordHeader hdr{};
        if (log.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) continue;
        if (hdr.magic != kMagic || hdr.ver != kVersion) continue;
        std::string payload;
        payload.resize(hdr.payload_len);
        if (hdr.payload_len > 0)
        {
            if (log.read(reinterpret_cast<uint8_t*>(&payload[0]), hdr.payload_len) != hdr.payload_len) continue;
        }
        uint32_t crc_disk = 0;
        log.read(reinterpret_cast<uint8_t*>(&crc_disk), sizeof(crc_disk));
        uint32_t crc_calc = crc32(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
        if (hdr.payload_len > 0)
        {
            crc_calc = crc32(reinterpret_cast<const uint8_t*>(payload.data()), hdr.payload_len) ^ crc_calc;
        }
        if (crc_calc != crc_disk) continue;

        ChatMessage msg;
        msg.channel = static_cast<ChannelId>(hdr.channel);
        msg.from = hdr.peer;
        msg.peer = hdr.peer;
        msg.timestamp = hdr.timestamp;
        msg.text = payload;
        msg.status = MessageStatus::Incoming;
        out.push_back(msg);
    }
    return out;
}

void LogStore::setUnread(ChannelId, int)
{
    // Unused in this store; ChatModel tracks unread.
}

int LogStore::getUnread(ChannelId) const
{
    return 0;
}

void LogStore::clearChannel(ChannelId channel)
{
    // Clear index slots for channel; keep log intact to avoid erase.
    ChannelIndex& cx = idx(channel);
    cx.head = {};
    persistHead(channel);
    // Zero slots file content
    File idx_file = fs_->open(indexPath(channel), FILE_WRITE);
    if (idx_file)
    {
        IndexSlot zero{};
        idx_file.seek(sizeof(IndexHead) * kHeadCopies, SeekSet);
        for (size_t i = 0; i < kSlotsPerChannel; ++i)
        {
            idx_file.write(reinterpret_cast<uint8_t*>(&zero), sizeof(IndexSlot));
        }
        idx_file.close();
    }
}

bool LogStore::loadIndex(ChannelId ch)
{
    if (!fs_) return false;
    const char* path = indexPath(ch);
    File f = fs_->open(path, FILE_READ);
    if (!f)
    {
        // Create empty index file
        File nf = fs_->open(path, FILE_WRITE);
        if (nf)
        {
            ChannelIndex empty{};
            nf.write(reinterpret_cast<uint8_t*>(&empty), sizeof(empty));
            nf.close();
        }
        return true;
    }

    ChannelIndex tmp{};
    f.read(reinterpret_cast<uint8_t*>(&tmp), sizeof(tmp));
    f.close();

    // Validate A/B head
    auto head_crc_ok = [](const IndexHead& h)
    {
        uint32_t crc_calc = h.seq ^ h.next ^ h.count ^ h.last_offset;
        return crc_calc == h.crc;
    };
    const IndexHead* heads = reinterpret_cast<IndexHead*>(&tmp);
    IndexHead chosen = heads[0];
    if (head_crc_ok(heads[1]) && heads[1].seq >= chosen.seq)
    {
        chosen = heads[1];
    }
    else if (!head_crc_ok(chosen) && head_crc_ok(heads[1]))
    {
        chosen = heads[1];
    }
    indexes_[static_cast<int>(ch)].head = chosen;
    // Slots follow after two heads (we stored struct continuously)
    indexes_[static_cast<int>(ch)].slots = tmp.slots;
    return true;
}

void LogStore::persistHead(ChannelId ch)
{
    if (!fs_) return;
    const char* path = indexPath(ch);
    File f = fs_->open(path, FILE_WRITE);
    if (!f) return;
    IndexHead& h = indexes_[static_cast<int>(ch)].head;
    h.crc = h.seq ^ h.next ^ h.count ^ h.last_offset;
    // Write two copies
    f.write(reinterpret_cast<uint8_t*>(&h), sizeof(h));
    f.write(reinterpret_cast<uint8_t*>(&h), sizeof(h));
    // Leave slots untouched (they are written individually)
    f.close();
}

void LogStore::persistSlot(ChannelId ch, uint16_t idx_slot)
{
    if (!fs_) return;
    const char* path = indexPath(ch);
    File f = fs_->open(path, FILE_READ);
    if (!f) return;
    File f2 = fs_->open(path, FILE_WRITE);
    if (!f2)
    {
        f.close();
        return;
    }
    if (!f) return;
    size_t offset = sizeof(IndexHead) * kHeadCopies + idx_slot * sizeof(IndexSlot);
    f2.seek(offset);
    IndexSlot& slot = indexes_[static_cast<int>(ch)].slots[idx_slot];
    f2.write(reinterpret_cast<uint8_t*>(&slot), sizeof(slot));
    f.close();
    f2.close();
}

bool LogStore::ensureDir()
{
    if (!fs_->exists(kDir))
    {
        return fs_->mkdir(kDir);
    }
    return true;
}

uint32_t LogStore::crc32(const uint8_t* data, size_t len) const
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

uint16_t LogStore::crc16(const uint8_t* data, size_t len) const
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

LogStore::ChannelIndex& LogStore::idx(ChannelId ch)
{
    return indexes_[static_cast<int>(ch)];
}

const LogStore::ChannelIndex& LogStore::idx(ChannelId ch) const
{
    return indexes_[static_cast<int>(ch)];
}

} // namespace chat
