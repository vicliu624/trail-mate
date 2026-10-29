#include "chat/infra/reticulum/interface_access_codec.h"
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include <Crypto.h>
#include <cstring>

namespace chat::reticulum
{
namespace
{
constexpr uint8_t kSalt[32] = {
    0xad, 0xf5, 0x4d, 0x88, 0x2c, 0x9a, 0x9b, 0x80, 0x77, 0x1e, 0xb4, 0x99, 0x5d, 0x70, 0x2d, 0x4a,
    0x3e, 0x73, 0x33, 0x91, 0xb2, 0xa0, 0xf5, 0x3f, 0x41, 0x6d, 0x9f, 0x90, 0x7e, 0x55, 0xcf, 0xf8};
size_t credentialLength(const char* text)
{
    size_t length = 0;
    if (text)
        while (length <= InterfaceAccessCodec::kMaxCredentialBytes && text[length]) ++length;
    return length;
}
} // namespace
bool InterfaceAccessCodec::configure(const char* name, const char* passphrase, size_t tag_bytes)
{
    clear();
    const auto name_size = credentialLength(name), pass_size = credentialLength(passphrase);
    if (name_size > kMaxCredentialBytes || pass_size > kMaxCredentialBytes) return false;
    if (!name_size && !pass_size)
    {
        state_ = State::Public;
        return true;
    }
    if (!tag_bytes || tag_bytes > kMaxTagSize) return false;
    size_t origin_size = 0;
    if (name_size)
    {
        hash_.reset();
        hash_.update(name, name_size);
        hash_.finalize(origin_, 32);
        origin_size = 32;
    }
    if (pass_size)
    {
        hash_.reset();
        hash_.update(passphrase, pass_size);
        hash_.finalize(origin_ + origin_size, 32);
        origin_size += 32;
    }
    hash_.reset();
    hash_.update(origin_, origin_size);
    hash_.finalize(origin_, 32);
    extract(origin_, 32, kSalt, sizeof(kSalt));
    expand(1);
    std::memcpy(key_, block_, 32);
    expand(2);
    std::memcpy(key_ + 32, block_, 32);
    ed25519_create_keypair(public_key_, private_key_, key_ + 32);
    tag_size_ = static_cast<uint8_t>(tag_bytes);
    state_ = State::Protected;
    clearScratch();
    return true;
}
void InterfaceAccessCodec::extract(const uint8_t* input, size_t size, const uint8_t* salt, size_t salt_size)
{
    hash_.resetHMAC(salt, salt_size);
    hash_.update(input, size);
    hash_.finalizeHMAC(salt, salt_size, prk_, sizeof(prk_));
}
void InterfaceAccessCodec::expand(uint8_t counter)
{
    hash_.resetHMAC(prk_, sizeof(prk_));
    if (counter > 1) hash_.update(block_, sizeof(block_));
    hash_.update(&counter, 1);
    hash_.finalizeHMAC(prk_, sizeof(prk_), block_, sizeof(block_));
}
void InterfaceAccessCodec::mask(uint8_t* frame, size_t size)
{
    extract(tag_, tag_size_, key_, sizeof(key_));
    for (size_t offset = 0; offset < size; ++offset)
    {
        if (offset % sizeof(block_) == 0) expand(static_cast<uint8_t>(offset / sizeof(block_) + 1));
        if (offset < 2 || offset >= 2U + tag_size_) frame[offset] ^= block_[offset % sizeof(block_)];
    }
}
bool InterfaceAccessCodec::encode(uint8_t* frame, size_t& size, size_t capacity)
{
    const size_t length = size;
    size = 0;
    if (state_ == State::Invalid || !frame || length < 3 || length > kMaxPacketSize || length > capacity || (frame[0] & 0x80)) return false;
    if (state_ == State::Public)
    {
        size = length;
        return true;
    }
    if (capacity - length < tag_size_) return false;
    ed25519_sign(signature_, frame, length, public_key_, private_key_);
    std::memcpy(tag_, signature_ + sizeof(signature_) - tag_size_, tag_size_);
    std::memmove(frame + 2 + tag_size_, frame + 2, length - 2);
    std::memcpy(frame + 2, tag_, tag_size_);
    mask(frame, length + tag_size_);
    frame[0] |= 0x80;
    size = length + tag_size_;
    clearScratch();
    return true;
}
bool InterfaceAccessCodec::decode(uint8_t* frame, size_t& size, size_t capacity)
{
    const size_t length = size;
    size = 0;
    if (state_ == State::Invalid || !frame || length < 3 || length > capacity) return false;
    if (state_ == State::Public)
    {
        if ((frame[0] & 0x80) || length > kMaxPacketSize) return false;
        size = length;
        return true;
    }
    if (!(frame[0] & 0x80) || length < 3U + tag_size_ || length > kMaxPacketSize + tag_size_) return false;
    std::memcpy(tag_, frame + 2, tag_size_);
    mask(frame, length);
    frame[0] &= 0x7f;
    std::memmove(frame + 2, frame + 2 + tag_size_, length - 2 - tag_size_);
    ed25519_sign(signature_, frame, length - tag_size_, public_key_, private_key_);
    uint8_t difference = 0;
    for (size_t index = 0; index < tag_size_; ++index) difference |= tag_[index] ^ signature_[sizeof(signature_) - tag_size_ + index];
    if (difference) clean(frame, length);
    else
    {
        size = length - tag_size_;
        clean(frame + size, tag_size_);
    }
    clearScratch();
    return difference == 0;
}
void InterfaceAccessCodec::clearScratch()
{
    hash_.clear();
    clean(origin_, sizeof(origin_));
    clean(tag_, sizeof(tag_));
    clean(signature_, sizeof(signature_));
    clean(prk_, sizeof(prk_));
    clean(block_, sizeof(block_));
}
void InterfaceAccessCodec::clear()
{
    clearScratch();
    clean(key_, sizeof(key_));
    clean(private_key_, sizeof(private_key_));
    clean(public_key_, sizeof(public_key_));
    tag_size_ = 0;
    state_ = State::Invalid;
}
} // namespace chat::reticulum
