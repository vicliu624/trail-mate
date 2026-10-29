#include "chat/infra/lxmf/lxmf_incoming_envelope.h"
#include "chat/infra/lxmf/lxmf_memory.h"
#include <cstring>
namespace chat::lxmf
{
bool unpackMessageEnvelopeForDestination(const uint8_t expected[16], const uint8_t* payload, size_t size,
                                         DecodedEnvelope* out, bool* embedded)
{
    if (!expected || !payload || !size || !out) return false;
    if (size >= 96 && !std::memcmp(payload, expected, 16) && unpackMessageEnvelope(payload, size, out))
    {
        if (embedded) *embedded = true;
        return true;
    }
    if (size < 80 || size > SIZE_MAX - 16) return false;
    runtime::RuntimeByteBuffer full(16 + size);
    std::memcpy(full.data(), expected, 16);
    std::memcpy(full.data() + 16, payload, size);
    if (!unpackMessageEnvelope(full.data(), full.size(), out)) return false;
    if (embedded) *embedded = false;
    return true;
}
} // namespace chat::lxmf
