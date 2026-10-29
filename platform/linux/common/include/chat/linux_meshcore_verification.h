#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace trailmate::linux_app::meshcore_wire
{
// Production Trail Mate MC control payload, carried only in authenticated
// direct appdata. It has no radio, directory mutation, or UI ownership.
class Verification
{
  public:
    static constexpr uint32_t port = 12, timeout_ms = 60000;
    enum class Prompt
    {
        None,
        ShowNumber,
        EnterNumber,
        Compare,
        Failed
    };
    struct Action
    {
        uint32_t peer = 0, number = 0;
        uint64_t nonce = 0;
        bool sender = false, send = false;
        Prompt prompt = Prompt::None;
        std::array<uint8_t, 12> payload{};
    };
    static bool number(const uint8_t shared[32], uint32_t self, uint32_t peer, uint64_t nonce, uint32_t& out);
    bool start(uint32_t peer, const std::array<uint8_t, 32>& key, uint64_t nonce, uint32_t number, uint32_t now, Action&);
    bool receive(uint32_t peer, const std::array<uint8_t, 32>& key, const uint8_t*, size_t,
                 uint32_t number, uint32_t now, Action&);
    bool submit(uint32_t peer, const std::array<uint8_t, 32>& key, uint64_t nonce, uint32_t number, uint32_t now, Action&);
    bool expire(uint32_t now, Action&);
    bool cancel(Action&);
    uint32_t peer() const { return peer_; }
    bool matches(const std::array<uint8_t, 32>& key) const { return key_ == key; }

  private:
    enum class Stage
    {
        Idle,
        Ready,
        Number,
        Final
    };
    void action(Action&, Prompt, uint8_t type = 0) const;
    void clear();
    Stage stage_ = Stage::Idle;
    uint32_t peer_ = 0, number_ = 0, started_ = 0;
    uint64_t nonce_ = 0;
    std::array<uint8_t, 32> key_{};
    // Prevent completed/cancelled received INITs from reopening UI prompts.
    struct Seen
    {
        uint32_t peer = 0;
        uint64_t nonce = 0;
    };
    std::array<Seen, 8> seen_{};
    size_t cursor_ = 0;
};
} // namespace trailmate::linux_app::meshcore_wire
