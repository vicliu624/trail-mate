#include "chat/linux_meshtastic_repeater.h"

#include <algorithm>
#include <cstring>

namespace trailmate::linux_app::meshtastic_wire
{
namespace mt = chat::meshtastic;
namespace
{
constexpr uint32_t kSeenLifetime = 300000;
constexpr uint32_t kQueueLifetime = 15000;
} // namespace

bool Repeater::Key::operator==(const Key& other) const
{
    return from == other.from && id == other.id && channel == other.channel;
}

bool Repeater::receive(uint32_t self, const platform::linux_runtime::RadioPacket& packet,
                       uint8_t primary_hash, uint8_t secondary_hash, uint32_t now, uint32_t random)
{
    if (!self || packet.size <= sizeof(mt::PacketHeaderWire) || packet.size > 255) return false;
    mt::PacketHeaderWire header;
    std::memcpy(&header, packet.data.data(), sizeof(header));
    const uint8_t hops = header.flags & mt::PACKET_FLAGS_HOP_LIMIT_MASK;
    if (!header.from || !header.id || !header.to || header.from == self || header.to == self || !hops ||
        (header.channel != primary_hash && header.channel != secondary_hash) ||
        (header.next_hop && header.next_hop != static_cast<uint8_t>(self))) return false;
    const Key key{header.from, header.id, header.channel};
    for (auto& pending : pending_)
    {
        if (pending.valid && now - pending.started >= kQueueLifetime) pending.valid = false;
        if (pending.valid && pending.key == key) return false;
    }
    Seen* history = nullptr;
    for (auto& seen : seen_)
    {
        if (seen.valid && now - seen.started >= kSeenLifetime) seen.valid = false;
        if (seen.valid && seen.key == key) history = &seen;
    }
    if (history)
    {
        const uint8_t start = (header.flags & mt::PACKET_FLAGS_HOP_START_MASK) >> mt::PACKET_FLAGS_HOP_START_SHIFT;
        // A repeated original transmission may need another relay when its ACK
        // was lost. Overheard relayed copies never start a forwarding loop.
        if (start != hops || history->transmissions >= 3 || now - history->forwarded < 3000) return false;
    }
    auto free = std::find_if(pending_.begin(), pending_.end(), [](const auto& value)
                             { return !value.valid; });
    if (free == pending_.end()) return false;
    free->frame.size = packet.size;
    std::copy_n(packet.data.begin(), packet.size, free->frame.bytes.begin());
    header.flags = (header.flags & ~mt::PACKET_FLAGS_HOP_LIMIT_MASK) | (hops - 1);
    header.relay_node = static_cast<uint8_t>(self);
    header.next_hop = 0;
    std::memcpy(free->frame.bytes.data(), &header, sizeof(header));
    free->key = key;
    free->started = now;
    free->due = now + 400 + random % 1400;
    free->failures = 0;
    free->valid = true;
    return true;
}

bool Repeater::take(uint32_t now, Frame& frame)
{
    for (auto& pending : pending_)
    {
        if (pending.valid && now - pending.started >= kQueueLifetime) pending.valid = false;
        if (pending.valid && static_cast<int32_t>(now - pending.due) >= 0)
        {
            frame = pending.frame;
            return true;
        }
    }
    return false;
}

void Repeater::complete(const Frame& frame, uint32_t now, bool transmitted)
{
    if (frame.size <= sizeof(mt::PacketHeaderWire) || frame.size > frame.bytes.size()) return;
    mt::PacketHeaderWire header;
    std::memcpy(&header, frame.bytes.data(), sizeof(header));
    const Key key{header.from, header.id, header.channel};
    for (auto& pending : pending_)
    {
        if (!pending.valid || !(pending.key == key)) continue;
        if (!transmitted)
        {
            if (++pending.failures >= 3) pending.valid = false;
            else pending.due = now + 1000;
            return;
        }
        pending.valid = false;
        for (auto& seen : seen_)
        {
            if (seen.valid && seen.key == key && now - seen.started < kSeenLifetime)
            {
                seen.forwarded = now;
                ++seen.transmissions;
                return;
            }
        }
        seen_[cursor_] = {key, now, now, 1, true};
        cursor_ = (cursor_ + 1) % seen_.size();
        return;
    }
}

void Repeater::reset()
{
    pending_ = {};
    seen_ = {};
    cursor_ = 0;
}

size_t Repeater::queued() const
{
    return std::count_if(pending_.begin(), pending_.end(), [](const auto& value)
                         { return value.valid; });
}
} // namespace trailmate::linux_app::meshtastic_wire
