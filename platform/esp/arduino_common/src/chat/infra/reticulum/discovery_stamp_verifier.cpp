#include "platform/esp/arduino_common/chat/infra/reticulum/discovery_stamp_verifier.h"

#include <cstring>
#if defined(ESP_PLATFORM)
#if __has_include(<esp_memory_utils.h>)
#include <esp_memory_utils.h>
#else
#include <soc/soc_memory_types.h>
#endif
#endif

namespace chat::reticulum
{
bool DiscoveryStampVerifier::begin(const uint8_t* packed, size_t size, const uint8_t* stamp)
{
    // Do not overwrite a pending job with another incoming announce.
    if (state_ == State::Pending) return false;
    reset();
#if defined(ESP_PLATFORM)
    if (!esp_ptr_external_ram(this))
    {
        state_ = State::Invalid;
        return false;
    }
#endif
    if (!packed || size == 0 || size > 467 || !stamp)
    {
        state_ = State::Invalid;
        return false;
    }
    hash_.reset();
    hash_.update(packed, size);
    hash_.finalize(material_, sizeof(material_));
    std::memcpy(stamp_, stamp, sizeof(stamp_));
    accumulated_.reset();
    state_ = State::Pending;
    return true;
}

DiscoveryStampVerifier::State DiscoveryStampVerifier::poll()
{
    if (state_ != State::Pending) return state_;
    // Native LXStamper: HKDF-SHA256(material, SHA256(material || msgpack(n)),
    // empty context, 256 bytes), for n in [0, 20). These n encode as one byte.
    hash_.reset();
    hash_.update(material_, sizeof(material_));
    hash_.update(&round_, 1);
    hash_.finalize(salt_, sizeof(salt_));
    hash_.resetHMAC(salt_, sizeof(salt_));
    hash_.update(material_, sizeof(material_));
    hash_.finalizeHMAC(salt_, sizeof(salt_), prk_, sizeof(prk_));
    for (uint8_t block = 1; block <= 8; ++block)
    {
        hash_.resetHMAC(prk_, sizeof(prk_));
        if (block != 1) hash_.update(previous_, sizeof(previous_));
        hash_.update(&block, 1);
        hash_.finalizeHMAC(prk_, sizeof(prk_), previous_, sizeof(previous_));
        accumulated_.update(previous_, sizeof(previous_));
    }
    if (++round_ == 20)
    {
        accumulated_.update(stamp_, sizeof(stamp_));
        accumulated_.finalize(previous_, sizeof(previous_));
        // Official discovery requires stamp_value >= 16 (leading zero bits).
        state_ = previous_[0] == 0 && previous_[1] == 0 ? State::Valid : State::Invalid;
    }
    return state_;
}

void DiscoveryStampVerifier::reset()
{
    hash_.clear();
    accumulated_.clear();
    std::memset(material_, 0, sizeof(material_));
    std::memset(stamp_, 0, sizeof(stamp_));
    std::memset(salt_, 0, sizeof(salt_));
    std::memset(prk_, 0, sizeof(prk_));
    std::memset(previous_, 0, sizeof(previous_));
    state_ = State::Idle;
    round_ = 0;
}
} // namespace chat::reticulum
