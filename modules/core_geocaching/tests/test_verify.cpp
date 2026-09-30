#include "geocaching/protocol/verify_record.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

struct UnavailableCrypto : geocaching::protocol::RecordCrypto
{
    bool sha256(geocaching::ByteView, std::uint8_t[32]) override { return false; }
    geocaching::protocol::VerificationResult verifyEd25519(
        geocaching::ByteView, geocaching::ByteView, geocaching::ByteView) override
    {
        return geocaching::protocol::VerificationResult::CryptoUnavailable;
    }
};

int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    assert(file.good());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    std::vector<std::uint8_t> workspace(4200);
    UnavailableCrypto crypto;
    geocaching::protocol::VerifiedRecordView out;
    using namespace geocaching::protocol;
    assert(verifyGeocache({bytes.data(), bytes.size()}, crypto, workspace.data(), 1, out) ==
           VerificationResult::WorkspaceTooSmall);
    assert(verifyGeocache({bytes.data(), bytes.size()}, crypto, workspace.data(), workspace.size(), out) ==
           VerificationResult::CryptoUnavailable);
    assert(out.record.encoded.data == nullptr);
    bytes.pop_back();
    assert(verifyGeocache({bytes.data(), bytes.size()}, crypto, workspace.data(), workspace.size(), out) ==
           VerificationResult::InvalidRecord);
}
