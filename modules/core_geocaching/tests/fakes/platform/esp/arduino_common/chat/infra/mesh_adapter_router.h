#pragma once
// Host-only transport seam for compiling the real dispatcher state machine.
// Firmware always includes the production router; this header is test-local.
#include "chat/domain/chat_types.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include <array>
#include <cstring>
#include <vector>

namespace chat
{
class MeshAdapterRouter
{
  public:
    bool ready = true, send_ok = true;
    unsigned sends = 0;
    std::array<uint8_t, 16> local{};
    std::vector<uint8_t> sent_bytes;
    bool getGeocachingDispatchDestination(uint8_t out[16])
    {
        if (!ready) return false;
        std::memcpy(out, local.data(), 16);
        return true;
    }
    MeshSendResult sendGeocachingData(const uint8_t[16], lxmf::ByteSpan bytes, bool,
                                      std::array<uint8_t, 32>* hash, const uint8_t expected[16])
    {
        ++sends;
        if (!ready || std::memcmp(expected, local.data(), 16)) return MeshSendResult::fail(MeshOperationFailure::NotReady);
        if (!send_ok) return MeshSendResult::fail(MeshOperationFailure::RadioTxFailed);
        sent_bytes.assign(bytes.data, bytes.data + bytes.size);
        hash->fill(0x42);
        return MeshSendResult::success(1);
    }
};
} // namespace chat
