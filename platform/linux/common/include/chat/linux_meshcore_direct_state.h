#pragma once
#include "chat/linux_meshcore_group_wire.h"

namespace trailmate::linux_app::meshcore_wire
{
struct DirectOutcome
{
    uint32_t peer = 0, message = 0;
};
class DirectState
{
  public:
    static constexpr uint32_t timeout_ms = 15000;
    bool canTrack(uint32_t peer, uint32_t signature, uint32_t message) const;
    bool track(uint32_t peer, uint32_t signature, uint32_t message, uint32_t now);
    bool acknowledge(uint32_t peer, uint32_t signature, uint32_t now, DirectOutcome& result);
    bool acknowledgeLegacy(uint32_t signature, uint32_t now, DirectOutcome& result);
    bool expire(uint32_t now, DirectOutcome& result);
    bool cancel(DirectOutcome& result);
    bool enqueue(uint32_t peer, uint32_t signature, const Frame& frame, uint32_t now);
    bool takeReply(uint32_t now, bool allowed, Frame& frame);
    void clearReplies();

  private:
    struct Ack
    {
        uint32_t peer = 0, signature = 0, message = 0, started = 0;
        bool valid = false;
    };
    struct Reply
    {
        Frame frame;
        uint32_t peer = 0, signature = 0, started = 0;
        bool valid = false;
    };
    std::array<Ack, 32> pending_{};
    std::array<Reply, 8> replies_{};
};
} // namespace trailmate::linux_app::meshcore_wire
