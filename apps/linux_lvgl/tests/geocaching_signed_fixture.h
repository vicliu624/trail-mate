#pragma once
#include "geocaching/protocol/record_encoder.h"
#include "geocaching/protocol/sign_record.h"
#include "platform/linux/geocaching_crypto.h"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>

namespace linux_geocaching_test
{
struct SignedVersion
{
    geocaching::GeocacheId id;
    geocaching::RevisionHash hash;
    std::vector<uint8_t> bytes;
    geocaching::ByteView view() const { return {bytes.data(), bytes.size()}; }
};
class FixtureSigner
{
  public:
    explicit FixtureSigner(const char* filename)
    {
        std::ifstream file(filename, std::ios::binary);
        assert(file.good());
        original_.assign(std::istreambuf_iterator<char>(file), {});
        std::array<uint8_t, 4200> scratch{};
        geocaching::protocol::VerifiedRecordView verified;
        assert(geocaching::protocol::verifyGeocache({original_.data(), original_.size()}, crypto, scratch.data(), scratch.size(), verified) == geocaching::protocol::VerificationResult::Valid);
        base_ = verified.record;
        std::copy_n(base_.author_public_key.data, 64, public_.data());
        // Public checked-in interoperability seed, never a device key.
        std::array<uint8_t, 32> seed{};
        for (size_t i = 0; i < seed.size(); ++i) seed[i] = static_cast<uint8_t>(i);
        key_ = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
        assert(key_);
        std::array<uint8_t, 32> derived{};
        size_t size = derived.size();
        assert(EVP_PKEY_get_raw_public_key(key_, derived.data(), &size) == 1 && size == 32);
        assert(std::equal(derived.begin(), derived.end(), public_.begin() + 32));
    }
    ~FixtureSigner() { EVP_PKEY_free(key_); }
    bool isReady() const { return key_ != nullptr; }
    void combinedPublicKey(uint8_t* output) const { std::copy(public_.begin(), public_.end(), output); }
    bool sign(const uint8_t* bytes, size_t size, uint8_t* signature)
    {
        EVP_MD_CTX* context = EVP_MD_CTX_new();
        if (!context) return false;
        size_t written = 64;
        const bool ok = EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key_) == 1 &&
                        EVP_DigestSign(context, signature, &written, bytes, size) == 1 && written == 64;
        EVP_MD_CTX_free(context);
        return ok;
    }
    SignedVersion make(uint32_t revision, const geocaching::RevisionHash* previous = nullptr)
    {
        auto record = base_;
        record.revision = revision;
        record.previous_hash = previous ? geocaching::ByteView{previous->bytes.data(), 32} : geocaching::ByteView{};
        if (revision > 1) record.updated_at += revision;
        std::vector<uint8_t> encoded(4096), scratch(4200);
        size_t size = 0, written = 0;
        assert(geocaching::protocol::encodeGeocacheRecord(record, encoded.data(), encoded.size(), size));
        SignedVersion out;
        out.bytes.resize(4166);
        assert(geocaching::protocol::signGeocacheRecord({encoded.data(), size}, *this, scratch.data(), scratch.size(), out.bytes.data(), out.bytes.size(), written));
        out.bytes.resize(written);
        geocaching::protocol::VerifiedRecordView verified;
        assert(geocaching::protocol::verifyGeocache(out.view(), crypto, scratch.data(), scratch.size(), verified) == geocaching::protocol::VerificationResult::Valid);
        out.id = verified.id;
        out.hash = verified.hash;
        if (revision == 1) assert(out.bytes == original_);
        return out;
    }
    platform::linux_runtime::LinuxGeocachingCrypto crypto;

  private:
    std::vector<uint8_t> original_;
    geocaching::RecordView base_;
    std::array<uint8_t, 64> public_{};
    EVP_PKEY* key_ = nullptr;
};
} // namespace linux_geocaching_test
#endif
