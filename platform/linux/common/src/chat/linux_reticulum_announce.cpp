#include "chat/linux_reticulum_announce.h"
#include <algorithm>
#include <cstring>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/rand.h>
#endif
namespace trailmate::linux_app
{
namespace r = ::chat::reticulum;
void LinuxReticulumAnnounce::destination(Aspect aspect, uint8_t output[16]) const
{
    uint8_t name[10];
    r::computeNameHash(aspect == Aspect::Delivery ? "lxmf" : "lxst", aspect == Aspect::Delivery ? "delivery" : "telephony", name);
    r::computeDestinationHash(name, identity_.identityHash().data(), output);
}
bool LinuxReticulumAnnounce::build(Aspect aspect, const char* name, uint32_t utc, bool response)
{
    size_ = 0;
    if (!identity_.ready() || !utc) return false;
    uint8_t target[16];
    destination(aspect, target);
    std::memcpy(payload_.data(), identity_.publicKey().data(), 64);
    r::computeNameHash(aspect == Aspect::Delivery ? "lxmf" : "lxst", aspect == Aspect::Delivery ? "delivery" : "telephony", payload_.data() + 64);
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (RAND_bytes(payload_.data() + 74, 5) != 1) return false;
#else
    return false;
#endif
    for (unsigned index = 0; index < 5; ++index) payload_[79 + index] = uint8_t(uint64_t(utc) >> (8 * (4 - index)));
    size_t label_size = name ? std::min<size_t>(strnlen(name, 128), 31) : 0;
    if (name)
        while (label_size && (uint8_t(name[label_size]) & 0xc0) == 0x80) --label_size;
    std::memcpy(signed_.data(), target, 16);
    std::memcpy(signed_.data() + 16, payload_.data(), 84);
    if (label_size) std::memcpy(signed_.data() + 100, name, label_size);
    if (!identity_.sign(signed_.data(), 100 + label_size, payload_.data() + 84)) return false;
    if (label_size) std::memcpy(payload_.data() + 148, name, label_size);
    size_ = packet_.size();
    return r::buildHeader1Packet(r::PacketType::Announce, r::DestinationType::Single,
                                 response ? r::PacketContext::PathResponse : r::PacketContext::None,
                                 false, target, payload_.data(), 148 + label_size, packet_.data(), &size_);
}
} // namespace trailmate::linux_app
