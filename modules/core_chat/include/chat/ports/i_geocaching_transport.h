#pragma once
#include "chat/domain/chat_types.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include <cstring>
#include <array>

namespace chat
{
namespace lxmf
{
// These views borrow transport-owned memory for the callback duration only.
struct CustomDeliveryView
{
    ByteSpan source;
    ByteSpan destination;
    ByteSpan message_hash;
    ByteSpan data;
};

// True means durable acceptance; volatile queueing alone must return false.
using CustomDeliveryHandler = bool (*)(const CustomDeliveryView&, void*);

struct GeocachingAnnouncementView
{
    ByteSpan discovery_destination;
    ByteSpan delivery_destination;
    ByteSpan public_key;
    ByteSpan app_data;
};
using GeocachingAnnouncementHandler = void (*)(const GeocachingAnnouncementView&, void*);
}
// Optional application transport capability; unrelated adapters need not
// implement it. The owning router controls lifetime and serializes access.
class IGeocachingTransport
{
  public:
    virtual ~IGeocachingTransport() = default;
    virtual bool getGeocachingAuthorKey(uint8_t out[64])
    { if (out) std::memset(out, 0, 64); return false; }
    virtual bool signGeocachingRecord(lxmf::ByteSpan, uint8_t*, size_t, uint8_t*, size_t, size_t& written)
    { written = 0; return false; }
    // Input bytes are borrowed for this call only; deferred transport work must
    // own its encoded payload and must not retain the caller's pointer.
    virtual MeshSendResult sendGeocachingData(const uint8_t destination_hash[16],
                                              lxmf::ByteSpan data, bool response = false,
                                              std::array<uint8_t, 32>* accepted_lxmf_hash = nullptr) = 0;
    virtual void setGeocachingAnnouncementHandler(
        void (*handler)(const lxmf::GeocachingAnnouncementView&, void*), void* context) = 0;
    virtual void setGeocachingDeliveryHandler(
        bool (*handler)(const lxmf::CustomDeliveryView&, void*), void* context) = 0;
};
} // namespace chat
