#pragma once
#include <SHA256.h>
#include <cstddef>
#include <cstdint>

namespace chat::reticulum
{
// Shared IFAC algorithm. One serialized interface owner; no device allocation
// policy belongs here. Platform wrappers enforce their own memory placement.
class InterfaceAccessCodec
{
  public:
    static constexpr size_t kMaxTagSize = 64, kMaxPacketSize = 500, kMaxCredentialBytes = 128;
    InterfaceAccessCodec() = default;
    ~InterfaceAccessCodec() { clear(); }
    InterfaceAccessCodec(const InterfaceAccessCodec&) = delete;
    InterfaceAccessCodec& operator=(const InterfaceAccessCodec&) = delete;
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
    void extract(const uint8_t*, size_t, const uint8_t*, size_t);
    void expand(uint8_t counter);
    void mask(uint8_t*, size_t);
    void clearScratch();
    SHA256 hash_;
    uint8_t key_[64]{}, private_key_[64]{}, public_key_[32]{};
    uint8_t origin_[64]{}, tag_[64]{}, signature_[64]{}, prk_[32]{}, block_[32]{};
    uint8_t tag_size_ = 0;
    State state_ = State::Invalid;
};
static_assert(sizeof(InterfaceAccessCodec) <= 640);
} // namespace chat::reticulum
