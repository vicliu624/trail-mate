#pragma once
#include "native_track_fixture.h"
#include "platform/linux/route_image_source_io.h"
#include "platform/linux/track_manifest_crypto.h"
#include "track_sharing/publication_directory.h"
#include <map>

namespace native_track_fixture
{
struct Directory
{
    using Hash = std::array<uint8_t, 32>;
    struct Blob
    {
        Bytes bytes;
        bool sealed = false;
    };
    Directory(Fixture& fixture, native::LinuxReticulumIdentity& identity) : fixture(fixture), identity(identity)
    {
        std::copy_n(identity.publicKey().data(), 64, binding.public_key);
        std::copy_n(identity.destinationHash().data(), 16, binding.delivery);
        binding.epoch[0] = 7;
        binding.file_limit = 8388608;
        binding.chunk_limit = 64;
        binding.query_limit = 32;
    }
    const Bytes& expected(const Hash& hash) const
    {
        if (hash == fixture.package.kml_hash) return fixture.package.kml;
        if (hash == fixture.package.index_hash) return fixture.package.index;
        assert(hash == fixture.package.image_hash);
        return fixture.package.image;
    }
    Bytes handle(const Bytes& packet, bool forged = false)
    {
        geocaching::protocol::CmpReader reader({packet.data(), packet.size()});
        size_t fields = 0;
        uint64_t value = 0;
        geocaching::ByteView nonce;
        assert(reader.array(fields, 6) && fields == 6 && reader.unsignedInteger(value) && value == 1 && reader.unsignedInteger(value) && value == 0 &&
               reader.unsignedInteger(last_operation) && reader.binary(nonce, 16) && nonce.size == 16 && reader.unsignedInteger(value) && value == 8192);
        const auto response = [&](unsigned count, auto body)
        { return encode([&](Writer& w)
                        {w.array(6);w.unsignedInteger(1);w.unsignedInteger(1);w.unsignedInteger(last_operation);
            w.binary(nonce);w.unsignedInteger(200);w.array(count);body(w); }); };
        if (last_operation == 4)
        {
            geocaching::ByteView hash;
            assert(reader.array(fields, 2) && fields == 2 && reader.binary(hash, 32) && hash.size == 32 && reader.unsignedInteger(value) && reader.finished());
            std::copy_n(hash.data, 32, last_hash.begin());
            assert(expected(last_hash).size() == value);
            auto& blob = blobs[last_hash];
            Id token{};
            std::copy_n(hash.data, 16, token.begin());
            tokens[token] = last_hash;
            return response(4, [&](Writer& w)
                            {if(blob.sealed)w.nil();else w.binary({token.data(),16});w.unsignedInteger(blob.bytes.size());w.unsignedInteger(blob.sealed);w.unsignedInteger(64); });
        }
        if (last_operation == 5 || last_operation == 6)
        {
            geocaching::ByteView received;
            assert(reader.array(fields, 3) && fields == (last_operation == 5 ? 3 : 1) && reader.binary(received, 16) && received.size == 16);
            Id token{};
            std::copy_n(received.data, 16, token.begin());
            assert(tokens.count(token));
            last_hash = tokens[token];
            auto& blob = blobs[last_hash];
            if (last_operation == 5)
            {
                geocaching::ByteView chunk;
                uint64_t offset = 0;
                assert(reader.unsignedInteger(offset) && reader.binary(chunk, 1024) && chunk.size <= 64 && reader.finished());
                if (offset == blob.bytes.size()) blob.bytes.insert(blob.bytes.end(), chunk.data, chunk.data + chunk.size);
                else assert(offset + chunk.size <= blob.bytes.size() && !std::memcmp(blob.bytes.data() + offset, chunk.data, chunk.size));
                return response(2, [&](Writer& w)
                                {w.binary({token.data(),16});w.unsignedInteger(offset+chunk.size); });
            }
            assert(reader.finished() && blob.bytes == expected(last_hash));
            blob.sealed = true;
            return response(4, [&](Writer& w)
                            {w.binary({last_hash.data(),32});w.unsignedInteger(blob.bytes.size());w.unsignedInteger(1);w.unsignedInteger(0); });
        }
        assert(last_operation == 7 && blobs[fixture.package.kml_hash].sealed);
        geocaching::ByteView manifest, signature;
        assert(reader.array(fields, 2) && fields == 2 && reader.array(fields, 2) && fields == 2 && reader.binary(manifest, 4096) &&
               reader.binary(signature, 64) && signature.size == 64 && reader.nil() && reader.finished());
        const auto signed_manifest = encode([&](Writer& w)
                                            {w.array(2);w.binary(manifest);w.binary(signature); });
        native::LinuxTrackManifestCrypto crypto;
        native::RouteSourceHash hash;
        track_sharing::VerifiedManifestView proof;
        std::array<uint8_t, 4200> workspace{};
        assert(track_sharing::verifyManifest({signed_manifest.data(), signed_manifest.size()}, crypto, hash, workspace.data(), workspace.size(), proof, {fixture.author.publicKey().data(), 64}) == track_sharing::ManifestVerification::Valid);
        assert(!std::memcmp(proof.object_id, fixture.object.data(), 32) && !std::memcmp(proof.revision_hash, fixture.revision.data(), 32));
        track_sharing::ManifestExtensionsView extensions;
        if (proof.manifest.version == 2 && track_sharing::getManifestExtensions(proof.manifest, extensions) && extensions.assets.count)
        {
            assert(blobs[fixture.package.index_hash].sealed && blobs[fixture.package.image_hash].sealed);
        }
        if (receipt.empty())
        {
            const auto raw = encode([&](Writer& w)
                                    {w.array(13);w.unsignedInteger(1);w.binary(nonce);w.binary({fixture.object.data(),32});w.binary({fixture.revision.data(),32});
                w.unsignedInteger(1);w.unsignedInteger(0);w.binary({fixture.package.kml_hash.data(),32});w.unsignedInteger(fixture.package.kml.size());
                w.binary({binding.public_key,64});w.binary({binding.delivery,16});w.binary({binding.epoch,16});w.unsignedInteger(1);w.unsignedInteger(100); });
            constexpr char domain[] = "TrailMate.Track.Receipt.v1";
            Bytes message(reinterpret_cast<const uint8_t*>(domain), reinterpret_cast<const uint8_t*>(domain) + sizeof(domain));
            message.insert(message.end(), raw.begin(), raw.end());
            std::array<uint8_t, 64> signed_bytes{};
            assert(identity.sign(message.data(), message.size(), signed_bytes.data()));
            receipt = encode([&](Writer& w)
                             {w.array(2);w.binary({raw.data(),raw.size()});w.binary({signed_bytes.data(),signed_bytes.size()}); });
        }
        auto body = receipt;
        if (forged) body.back() ^= 1;
        auto output = response(2, [](Writer&) {});
        output.insert(output.end(), body.begin(), body.end());
        output.push_back(1);
        return output;
    }
    Fixture& fixture;
    native::LinuxReticulumIdentity& identity;
    track_sharing::PublicationDirectory binding;
    std::map<Hash, Blob> blobs;
    std::map<Id, Hash> tokens;
    Bytes receipt;
    uint64_t last_operation = 0;
    Hash last_hash{};
};
} // namespace native_track_fixture
