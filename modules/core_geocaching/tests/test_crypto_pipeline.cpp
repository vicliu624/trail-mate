#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "geocaching/protocol/verify_record.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

struct NativeCrypto : geocaching::protocol::RecordCrypto
{
    bool sha256(geocaching::ByteView in, std::uint8_t out[32]) override
    {
        chat::reticulum::fullHash(in.data, in.size, out);
        return true;
    }
    geocaching::protocol::VerificationResult verifyEd25519(geocaching::ByteView key, geocaching::ByteView sig, geocaching::ByteView msg) override
    {
        using R = geocaching::protocol::VerificationResult;
        return ed25519_verify(sig.data, msg.data, msg.size, key.data) ? R::Valid : R::InvalidSignature;
    }
};
int main(int argc, char** argv)
{
    assert(argc == 3);
    std::ifstream f(argv[1], std::ios::binary), metadata(argv[2], std::ios::binary);
    assert(f.good() && metadata.good());
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(f)), {}), workspace(4200);
    geocaching::GeocacheId id;
    geocaching::RevisionHash hash;
    metadata.read(reinterpret_cast<char*>(id.bytes.data()), 32);
    metadata.read(reinterpret_cast<char*>(hash.bytes.data()), 32);
    assert(metadata.good());
    NativeCrypto crypto;
    geocaching::protocol::VerifiedRecordView out;
    using namespace geocaching::protocol;
    assert(verifyGeocache({data.data(), data.size()}, crypto, workspace.data(), workspace.size(), out, &id, &hash) == VerificationResult::Valid);
    hash.bytes[0] ^= 1;
    assert(verifyGeocache({data.data(), data.size()}, crypto, workspace.data(), workspace.size(), out, &id, &hash) == VerificationResult::IdentityMismatch);
    data.back() ^= 1;
    assert(verifyGeocache({data.data(), data.size()}, crypto, workspace.data(), workspace.size(), out) == VerificationResult::InvalidSignature);
}
