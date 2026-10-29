#pragma once
#include "geocaching/protocol/cmp_writer.h"
#include "track_sharing/manifest_hash.h"
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <openssl/evp.h>
#include <vector>

namespace package_fixture
{
using Bytes = std::vector<uint8_t>;
using Writer = geocaching::protocol::CmpWriter;
inline std::array<uint8_t, 32> hash(const Bytes& bytes)
{
    std::array<uint8_t, 32> result{};
    unsigned count = 0;
    assert(EVP_Digest(bytes.data(), bytes.size(), result.data(), &count, EVP_sha256(), nullptr) == 1 && count == 32);
    return result;
}
template <class Encode>
Bytes encode(Encode encode)
{
    Bytes bytes(8192);
    Writer writer(bytes.data(), bytes.size());
    encode(writer);
    assert(writer.good());
    bytes.resize(writer.size());
    return bytes;
}
struct StreamHash
{
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    ~StreamHash() { EVP_MD_CTX_free(context); }
    bool start() { return EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1; }
    bool update(const uint8_t* data, size_t bytes) { return EVP_DigestUpdate(context, data, bytes) == 1; }
    bool finish(uint8_t* output)
    {
        unsigned bytes = 0;
        return EVP_DigestFinal_ex(context, output, &bytes) == 1 && bytes == 32;
    }
    void reset() { EVP_MD_CTX_reset(context); }
};
struct SignedPackage
{
    Bytes kml, image, index, signed_manifest;
    std::array<uint8_t, 32> object{}, revision{}, kml_hash{}, image_hash{}, index_hash{};
    std::array<uint8_t, 64> author{};
};
inline SignedPackage make(const std::filesystem::path& image_path, bool attachments = true, bool correct_geometry = true)
{
    SignedPackage result;
    std::ifstream stream(image_path, std::ios::binary);
    assert(stream);
    result.image = Bytes(std::istreambuf_iterator<char>(stream), {});
    const std::string text = "<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Document><Placemark><description><![CDATA[<img src=\"photos/example.jpg\">]]></description><Point><coordinates>1,2</coordinates></Point></Placemark></Document></kml>";
    result.kml.assign(text.begin(), text.end());
    result.kml_hash = hash(result.kml);
    result.image_hash = hash(result.image);
    if (attachments)
    {
        result.index = encode([&](Writer& w)
                              {
            w.array(4); w.unsignedInteger(1); w.binary({result.kml_hash.data(), 32}); w.unsignedInteger(result.kml.size()); w.array(1);
            w.array(7); w.text("photo"); w.binary({result.image_hash.data(), 32}); w.unsignedInteger(result.image.size());
            w.text("image/jpeg"); w.text("photos/example.jpg"); w.unsignedInteger(0); w.unsignedInteger(1); });
        result.index_hash = hash(result.index);
    }
    std::array<uint8_t, 32> seed{};
    seed[0] = 41;
    EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
    assert(key);
    auto encryption_seed = seed;
    encryption_seed[0] = 40;
    EVP_PKEY* encryption = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, encryption_seed.data(), encryption_seed.size());
    size_t encryption_size = 32;
    assert(encryption && EVP_PKEY_get_raw_public_key(encryption, result.author.data(), &encryption_size) == 1 && encryption_size == 32);
    EVP_PKEY_free(encryption);
    size_t size = 32;
    assert(EVP_PKEY_get_raw_public_key(key, result.author.data() + 32, &size) == 1 && size == 32);
    uint8_t nonce[16]{2};
    const auto manifest = encode([&](Writer& w)
                                 {
        w.array(attachments ? 16 : 14); w.unsignedInteger(attachments ? 2 : 1); w.binary({result.author.data(), 64}); w.binary({nonce, 16}); w.unsignedInteger(0);
        w.unsignedInteger(1); w.nil(); w.unsignedInteger(0); w.unsignedInteger(1); w.unsignedInteger(1); w.text("Remote package"); w.text("");
        w.array(4); w.signedInteger(correct_geometry ? 10000000 : 20000000); w.signedInteger(20000000); w.signedInteger(correct_geometry ? 10000000 : 20000000); w.signedInteger(20000000);
        w.array(4); w.unsignedInteger(0); w.nil(); w.nil(); w.unsignedInteger(1);
        w.array(4); w.binary({result.kml_hash.data(), 32}); w.unsignedInteger(result.kml.size()); w.text("application/vnd.google-earth.kml+xml"); w.unsignedInteger(1);
        if (attachments)
        {
            w.array(8); w.unsignedInteger(0); w.array(0); w.unsignedInteger(0); w.unsignedInteger(1); w.unsignedInteger(0); w.unsignedInteger(0); w.unsignedInteger(0); w.nil();
            w.array(4); w.binary({result.index_hash.data(), 32}); w.unsignedInteger(result.index.size()); w.unsignedInteger(1); w.unsignedInteger(1);
        } });
    std::string domain = attachments ? "TrailMate.Track.Manifest.v2" : "TrailMate.Track.Manifest.v1";
    domain.push_back('\0');
    Bytes message(domain.begin(), domain.end());
    message.insert(message.end(), manifest.begin(), manifest.end());
    uint8_t signature[64]{};
    size = 64;
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    assert(context && EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key) == 1 && EVP_DigestSign(context, signature, &size, message.data(), message.size()) == 1 && size == 64);
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    result.signed_manifest = encode([&](Writer& w)
                                    { w.array(2); w.binary({manifest.data(), manifest.size()}); w.binary({signature, 64}); });
    StreamHash hasher;
    track_sharing::ManifestView view;
    assert(track_sharing::deriveManifestHashes({manifest.data(), manifest.size()}, view, hasher, result.object.data(), result.revision.data()));
    return result;
}
} // namespace package_fixture
