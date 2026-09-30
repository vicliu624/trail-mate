#pragma once

#include <SHA256.h>
#include <cstddef>
#include <cstdint>

namespace chat::reticulum
{
// Interface-local access state. Own in PSRAM; packet storage belongs to caller.
// Calls are serialized by the interface owner. Never copy between interfaces.
class IfacCodec
{
  public:
    static constexpr size_t kMaxTagSize = 64;
    static constexpr size_t kMaxPacketSize = 500;
    static constexpr size_t kMaxCredentialBytes = 128;
    IfacCodec() = default;
    ~IfacCodec() { clear(); }
    IfacCodec(const IfacCodec&) = delete;
    IfacCodec& operator=(const IfacCodec&) = delete;

    // tag_bytes is bytes, unlike the RNS configuration's ifac_size in bits.
    // Empty credentials explicitly select public mode; any error fails closed.
    bool configure(const char* network_name, const char* passphrase, size_t tag_bytes);
    bool encode(uint8_t* frame, size_t& size, size_t capacity);
    bool decode(uint8_t* frame, size_t& size, size_t capacity);
    void clear();

  private:
    enum class State : uint8_t
    {
        Invalid,
        Public,
        Protected
    };
    void extract(const uint8_t* input, size_t size, const uint8_t* salt, size_t salt_size);
    void expand(uint8_t counter);
    void mask(uint8_t* frame, size_t size);
    void clearScratch();
    SHA256 hash_;
    uint8_t key_[64]{};
    uint8_t private_key_[64]{};
    uint8_t public_key_[32]{};
    uint8_t origin_[64]{};
    uint8_t tag_[64]{};
    uint8_t signature_[64]{};
    uint8_t prk_[32]{};
    uint8_t block_[32]{};
    uint8_t tag_size_ = 0;
    State state_ = State::Invalid;
};
static_assert(sizeof(IfacCodec) <= 640);
} // namespace chat::reticulum
