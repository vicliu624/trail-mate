#include "chat/linux_meshcore_verification.h"
#include <algorithm>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#endif

namespace trailmate::linux_app::meshcore_wire
{
namespace
{
uint64_t read(const uint8_t* in, unsigned count)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value |= uint64_t(in[i]) << (i * 8);
    return value;
}
void write(uint8_t* out, uint64_t value, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) out[i] = uint8_t(value >> (i * 8));
}
bool valid(uint32_t peer, const std::array<uint8_t, 32>& key, uint64_t nonce, uint32_t number)
{
    return peer && peer != 0xffffffffu && nonce && number < 1000000 &&
           std::any_of(key.begin(), key.end(), [](uint8_t byte)
                       { return byte != 0; });
}
} // namespace
bool Verification::number(const uint8_t shared[32], uint32_t self, uint32_t peer, uint64_t nonce, uint32_t& out)
{
    out = 0xffffffffu;
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (!shared || !self || !peer || self == peer || self == 0xffffffffu || peer == 0xffffffffu || !nonce ||
        std::all_of(shared, shared + 32, [](uint8_t byte)
                    { return byte == 0; })) return false;
    struct Input
    {
        std::array<uint8_t, 48> bytes{};
        ~Input()
        {
            volatile uint8_t* cursor = bytes.data();
            for (size_t i = 0; i < bytes.size(); ++i) cursor[i] = 0;
        }
    } input;
    std::copy_n(shared, 32, input.bytes.data());
    write(input.bytes.data() + 32, std::min(self, peer), 4);
    write(input.bytes.data() + 36, std::max(self, peer), 4);
    write(input.bytes.data() + 40, nonce, 8);
    std::array<uint8_t, 32> digest{};
    unsigned size = 0;
    if (EVP_Digest(input.bytes.data(), input.bytes.size(), digest.data(), &size, EVP_sha256(), nullptr) != 1 || size != digest.size()) return false;
    out = uint32_t(read(digest.data(), 4)) % 1000000;
    return true;
#else
    (void)shared;
    (void)self;
    (void)peer;
    (void)nonce;
    return false;
#endif
}
void Verification::action(Action& out, Prompt prompt, uint8_t type) const
{
    out = {};
    out.peer = peer_;
    out.nonce = nonce_;
    out.number = number_;
    out.sender = stage_ == Stage::Ready || stage_ == Stage::Number;
    out.prompt = prompt;
    out.send = type != 0;
    if (type)
    {
        out.payload = {'T', 'M', 2, type};
        write(out.payload.data() + 4, nonce_, 8);
    }
}
void Verification::clear()
{
    stage_ = Stage::Idle;
    peer_ = number_ = started_ = 0;
    nonce_ = 0;
    key_.fill(0);
}
bool Verification::start(uint32_t peer, const std::array<uint8_t, 32>& key, uint64_t nonce, uint32_t number, uint32_t now, Action& out)
{
    out = {};
    if (stage_ != Stage::Idle || !valid(peer, key, nonce, number)) return false;
    peer_ = peer;
    key_ = key;
    nonce_ = nonce;
    number_ = number;
    started_ = now;
    stage_ = Stage::Ready;
    action(out, Prompt::None, 1);
    return true;
}
bool Verification::receive(uint32_t peer, const std::array<uint8_t, 32>& key, const uint8_t* bytes, size_t size,
                           uint32_t number, uint32_t now, Action& out)
{
    out = {};
    if (!bytes || size != 12 || bytes[0] != 'T' || bytes[1] != 'M' || bytes[2] != 2) return false;
    const auto nonce = read(bytes + 4, 8);
    if (!nonce) return false;
    if (bytes[3] == 1)
    {
        if (stage_ != Stage::Idle || !valid(peer, key, nonce, number)) return false;
        for (const auto& seen : seen_)
            if (seen.peer == peer && seen.nonce == nonce) return false;
        seen_[cursor_] = {peer, nonce};
        cursor_ = (cursor_ + 1) % seen_.size();
        peer_ = peer;
        key_ = key;
        nonce_ = nonce;
        number_ = number;
        started_ = now;
        stage_ = Stage::Final;
        action(out, Prompt::ShowNumber, 2);
        return true;
    }
    if (stage_ == Stage::Idle || peer != peer_ || nonce != nonce_ || key != key_ || uint32_t(now - started_) >= timeout_ms) return false;
    if (bytes[3] == 2 && stage_ == Stage::Ready)
    {
        stage_ = Stage::Number;
        started_ = now;
        action(out, Prompt::EnterNumber);
        return true;
    }
    if (bytes[3] == 3 && stage_ == Stage::Final)
    {
        action(out, Prompt::Compare);
        clear();
        return true;
    }
    return false;
}
bool Verification::submit(uint32_t peer, const std::array<uint8_t, 32>& key, uint64_t nonce, uint32_t number, uint32_t now, Action& out)
{
    out = {};
    if (stage_ != Stage::Number || peer != peer_ || key != key_ || nonce != nonce_ || number != number_ ||
        number >= 1000000 || uint32_t(now - started_) >= timeout_ms) return false;
    action(out, Prompt::Compare, 3);
    clear();
    return true;
}
bool Verification::expire(uint32_t now, Action& out)
{
    out = {};
    if (stage_ == Stage::Idle || uint32_t(now - started_) < timeout_ms) return false;
    return cancel(out);
}
bool Verification::cancel(Action& out)
{
    out = {};
    if (stage_ == Stage::Idle) return false;
    action(out, Prompt::Failed);
    clear();
    return true;
}
} // namespace trailmate::linux_app::meshcore_wire
