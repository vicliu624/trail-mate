#include "platform/linux/geocaching_crypto.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

using Result = geocaching::protocol::VerificationResult;
int main(int argc, char** argv)
{
    assert(argc == 3);
    std::ifstream record(argv[1], std::ios::binary), metadata(argv[2], std::ios::binary);
    assert(record.good() && metadata.good());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(record)), {});
    std::vector<uint8_t> scratch(4200);
    geocaching::GeocacheId id;
    geocaching::RevisionHash hash;
    metadata.read(reinterpret_cast<char*>(id.bytes.data()), 32);
    metadata.read(reinterpret_cast<char*>(hash.bytes.data()), 32);
    assert(metadata.good());
    platform::linux_runtime::LinuxGeocachingCrypto crypto;
    std::array<uint8_t, 32> digest{};
    assert(!crypto.sha256({nullptr, 1}, digest.data()));
    assert(!crypto.sha256({}, nullptr));
    geocaching::protocol::VerifiedRecordView out;
    const auto verify = [&](const geocaching::GeocacheId* expected_id = nullptr, const geocaching::RevisionHash* expected_hash = nullptr)
    {
        return geocaching::protocol::verifyGeocache({bytes.data(), bytes.size()}, crypto,
                                                    scratch.data(), scratch.size(), out, expected_id, expected_hash);
    };
#if defined(TRAIL_MATE_HAS_OPENSSL)
    const std::array<uint8_t, 32> empty_hash{{0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
                                              0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55}};
    assert(crypto.sha256({}, digest.data()) && digest == empty_hash);
    std::array<uint8_t, 32> overlapping{};
    overlapping[0] = 'a';
    overlapping[1] = 'b';
    overlapping[2] = 'c';
    assert(crypto.sha256({overlapping.data(), 3}, digest.data()));
    assert(crypto.sha256({overlapping.data(), 3}, overlapping.data()) && overlapping == digest);
    assert(verify(&id, &hash) == Result::Valid);
    assert(out.id.bytes == id.bytes && out.hash.bytes == hash.bytes);
    const auto public_key = geocaching::ByteView{out.record.author_public_key.data + 32, 32};
    std::array<uint8_t, 64> invalid_signature{};
    assert(crypto.verifyEd25519(public_key, {invalid_signature.data(), 64}, {}) != Result::Valid);
    hash.bytes[0] ^= 1;
    assert(verify(&id, &hash) == Result::IdentityMismatch && !out.record.encoded.data);
    hash.bytes[0] ^= 1;
    id.bytes[0] ^= 1;
    assert(verify(&id, &hash) == Result::IdentityMismatch);
    id.bytes[0] ^= 1;
    bytes.back() ^= 1;
    assert(verify() == Result::InvalidSignature && !out.record.encoded.data);
    bytes.back() ^= 1;
    // Trailing/malformed envelopes must not produce a verified borrowed view.
    bytes.push_back(0);
    assert(verify() == Result::InvalidRecord && !out.record.encoded.data);
    bytes.pop_back();
    assert(geocaching::protocol::verifyGeocache({bytes.data(), bytes.size()}, crypto, scratch.data(), 1, out) == Result::WorkspaceTooSmall);
#else
    digest.fill(0xff);
    assert(!crypto.sha256({}, digest.data()));
    assert(std::all_of(digest.begin(), digest.end(), [](uint8_t byte)
                       { return byte == 0; }));
    assert(verify(&id, &hash) == Result::CryptoUnavailable && !out.record.encoded.data);
#endif
    std::array<uint8_t, 32> key{};
    std::array<uint8_t, 64> signature{};
    assert(crypto.verifyEd25519({}, {signature.data(), signature.size()}, {}) == Result::InvalidSignature);
    assert(crypto.verifyEd25519({key.data(), 31}, {signature.data(), signature.size()}, {}) == Result::InvalidSignature);
    assert(crypto.verifyEd25519({key.data(), key.size()}, {signature.data(), 63}, {}) == Result::InvalidSignature);
    assert(crypto.verifyEd25519({key.data(), key.size()}, {signature.data(), signature.size()}, {nullptr, 1}) == Result::InvalidSignature);
}
