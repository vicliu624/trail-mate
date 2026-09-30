#pragma once

#include <SHA256.h>
#include <cstddef>
#include <cstdint>

namespace chat::reticulum
{
// Own this object in PSRAM on ESP. Each poll expands just one official discovery
// round; hashing is streamed, so there is no 5 KB/256 KB workblock allocation.
// A valid stamp does not replace verification of the outer announce signature.
class DiscoveryStampVerifier
{
  public:
    enum class State : uint8_t
    {
        Idle,
        Pending,
        Valid,
        Invalid
    };
    DiscoveryStampVerifier() = default;
    DiscoveryStampVerifier(const DiscoveryStampVerifier&) = delete;
    DiscoveryStampVerifier& operator=(const DiscoveryStampVerifier&) = delete;

    bool begin(const uint8_t* packed, size_t size, const uint8_t* stamp);
    State poll();
    void reset();
    State state() const { return state_; }
    uint8_t rounds() const { return round_; }

  private:
    SHA256 hash_;
    SHA256 accumulated_;
    uint8_t material_[32] = {};
    uint8_t stamp_[32] = {};
    uint8_t salt_[32] = {};
    uint8_t prk_[32] = {};
    uint8_t previous_[32] = {};
    State state_ = State::Idle;
    uint8_t round_ = 0;
};
} // namespace chat::reticulum
