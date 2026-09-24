#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "geocaching/protocol/sign_record.h"
#include <fstream>
#include <iterator>
#include <vector>
struct TestIdentity
{
    std::array<uint8_t, 64> public_key{}, private_key{};
    unsigned calls = 0;
    bool ready = true;
    bool isReady() const { return ready; }
    void combinedPublicKey(uint8_t* out) const { std::memcpy(out, public_key.data(), public_key.size()); }
    bool sign(const uint8_t* data, size_t size, uint8_t* signature)
    {
        ++calls;
        ed25519_sign(signature, data, size, public_key.data() + 32, private_key.data());
        return true;
    }
};
int main(int argc, char** argv)
{
    using namespace geocaching;
    if (argc != 2) return 1;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> expected((std::istreambuf_iterator<char>(file)), {});
    protocol::CmpReader reader({expected.data(), expected.size()});
    size_t count = 0;
    ByteView encoded;
    if (!reader.array(count, 2) || !reader.binary(encoded, 4096)) return 2;
    RecordView record;
    if (!protocol::decodeGeocacheRecord(encoded, record)) return 3;
    TestIdentity identity;
    std::memcpy(identity.public_key.data(), record.author_public_key.data, 64);
    // Public seed from the checked-in interoperability fixture, never device keys.
    uint8_t seed[32];
    for (unsigned i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i);
    ed25519_create_keypair(identity.public_key.data() + 32, identity.private_key.data(), seed);
    std::array<uint8_t, 4166> workspace{}, output{};
    size_t size = 0;
    if (!protocol::signGeocacheRecord(encoded, identity, workspace.data(), workspace.size(), output.data(), output.size(), size) ||
        size != expected.size() || std::memcmp(output.data(), expected.data(), size) || identity.calls != 1) return 4;
    identity.public_key[0] ^= 1;
    if (protocol::signGeocacheRecord(encoded, identity, workspace.data(), workspace.size(), output.data(), output.size(), size) || size || identity.calls != 1) return 5;
    identity.public_key[0] ^= 1;
    identity.ready = false;
    if (protocol::signGeocacheRecord(encoded, identity, workspace.data(), workspace.size(), output.data(), output.size(), size) || identity.calls != 1) return 6;
    identity.ready = true;
    std::vector<uint8_t> shared(encoded.size + 70);
    if (!protocol::signGeocacheRecord(encoded, identity, shared.data(), shared.size(), shared.data(), shared.size(), size) ||
        size != expected.size() || std::memcmp(shared.data(), expected.data(), size) || identity.calls != 2) return 7;
    return 0;
}
