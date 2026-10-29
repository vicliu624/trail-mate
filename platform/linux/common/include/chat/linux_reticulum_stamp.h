#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace trailmate::linux_app
{
// One serialized worker owns this incremental computation and its heap scratch.
class LinuxReticulumStamp
{
  public:
    enum class State
    {
        Idle,
        Expanding,
        Searching,
        Complete,
        Failed
    };
    LinuxReticulumStamp();
    ~LinuxReticulumStamp();
    LinuxReticulumStamp(const LinuxReticulumStamp&) = delete;
    LinuxReticulumStamp& operator=(const LinuxReticulumStamp&) = delete;
    bool begin(const uint8_t transient[32], uint8_t cost);
    State poll(uint16_t expand_budget = 4, uint16_t search_budget = 256);
    bool take(uint8_t stamp[32]);
    void reset();
    State state() const;
    uint16_t expandedRounds() const;
    uint32_t searchRounds() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trailmate::linux_app
