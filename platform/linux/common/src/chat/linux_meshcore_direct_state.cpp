#include "chat/linux_meshcore_direct_state.h"

namespace trailmate::linux_app::meshcore_wire
{
bool DirectState::canTrack(uint32_t peer, uint32_t signature, uint32_t message) const
{
    if (!peer || !signature) return false;
    bool space = false;
    for (const auto& item : pending_)
    {
        if (!item.valid)
        {
            space = true;
            continue;
        }
        if (item.peer == peer && item.signature == signature && (!message || item.message != message)) return false;
        if (message && item.peer == peer && item.message == message) space = true;
    }
    return space;
}
bool DirectState::track(uint32_t peer, uint32_t signature, uint32_t message, uint32_t now)
{
    if (!canTrack(peer, signature, message)) return false;
    for (auto& item : pending_)
        if (item.valid && message && item.peer == peer && item.message == message)
        {
            item = {peer, signature, message, now, true};
            return true;
        }
    for (auto& item : pending_)
        if (!item.valid)
        {
            item = {peer, signature, message, now, true};
            return true;
        }
    return false;
}
bool DirectState::acknowledge(uint32_t peer, uint32_t signature, uint32_t now, DirectOutcome& result)
{
    result = {};
    for (auto& item : pending_)
        if (item.valid && item.peer == peer && item.signature == signature && uint32_t(now - item.started) < timeout_ms)
        {
            result = {item.peer, item.message};
            item.valid = false;
            return true;
        }
    return false;
}
bool DirectState::expire(uint32_t now, DirectOutcome& result)
{
    result = {};
    for (auto& item : pending_)
        if (item.valid && uint32_t(now - item.started) >= timeout_ms)
        {
            result = {item.peer, item.message};
            item.valid = false;
            return true;
        }
    return false;
}

bool DirectState::acknowledgeLegacy(uint32_t signature, uint32_t now, DirectOutcome& result)
{
    result = {};
    uint32_t peer = 0;
    for (const auto& item : pending_)
        if (item.valid && item.signature == signature && uint32_t(now - item.started) < timeout_ms)
        {
            if (peer) return false; // A short unsigned ACK cannot disambiguate peers.
            peer = item.peer;
        }
    return peer && acknowledge(peer, signature, now, result);
}
bool DirectState::cancel(DirectOutcome& result)
{
    result = {};
    for (auto& item : pending_)
        if (item.valid)
        {
            result = {item.peer, item.message};
            item.valid = false;
            return true;
        }
    return false;
}
bool DirectState::enqueue(uint32_t peer, uint32_t signature, const Frame& frame, uint32_t now)
{
    if (!peer || !signature || !frame.size || frame.size > frame.bytes.size()) return false;
    for (const auto& item : replies_)
        if (item.valid && item.peer == peer && item.signature == signature && uint32_t(now - item.started) < timeout_ms) return true;
    for (auto& item : replies_)
        if (!item.valid || uint32_t(now - item.started) >= timeout_ms)
        {
            item = {frame, peer, signature, now, true};
            return true;
        }
    return false;
}
bool DirectState::takeReply(uint32_t now, bool allowed, Frame& frame)
{
    frame = {};
    for (auto& item : replies_)
    {
        if (!item.valid) continue;
        const auto age = uint32_t(now - item.started);
        if (age >= timeout_ms)
        {
            item.valid = false;
            continue;
        }
        if (!allowed || age < 120) continue;
        frame = item.frame;
        item.valid = false;
        return true;
    }
    return false;
}
void DirectState::clearReplies() { replies_ = {}; }
} // namespace trailmate::linux_app::meshcore_wire
