#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "geocaching/protocol/capabilities.h"
#include "geocaching/protocol/get_response.h"
#include "geocaching/protocol/publish_response.h"
#include "geocaching/protocol/query_response.h"
#include "geocaching/protocol/verify_record.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

struct RecordCrypto final : geocaching::protocol::RecordCrypto
{
    bool sha256(geocaching::ByteView bytes, uint8_t out[32]) override
    {
        chat::reticulum::fullHash(bytes.data, bytes.size, out);
        return true;
    }
    geocaching::protocol::VerificationResult verifyEd25519(geocaching::ByteView key, geocaching::ByteView signature, geocaching::ByteView message) override
    {
        return ed25519_verify(signature.data, message.data, message.size, key.data) ? geocaching::protocol::VerificationResult::Valid : geocaching::protocol::VerificationResult::InvalidSignature;
    }
};
int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4) return 1;
    std::ifstream captured(argv[1], std::ios::binary), identity(argv[2], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(captured)), {});
    std::vector<uint8_t> key((std::istreambuf_iterator<char>(identity)), {});
    chat::lxmf::DecodedEnvelope envelope;
    chat::lxmf::DecodedTextPayload text;
    if (key.size() != 64 || !chat::lxmf::unpackMessageEnvelope(bytes.data(), bytes.size(), &envelope) ||
        !chat::lxmf::unpackTextPayload(envelope.packed_payload.data(), envelope.packed_payload.size(), &text)) return 2;
    uint8_t identity_hash[16], name_hash[10], source_hash[16];
    chat::reticulum::computeIdentityHash(key.data(), identity_hash);
    chat::reticulum::computeNameHash("lxmf", "delivery", name_hash);
    chat::reticulum::computeDestinationHash(name_hash, identity_hash, source_hash);
    if (std::memcmp(source_hash, envelope.source_hash, sizeof(source_hash))) return 10;
    std::vector<uint8_t> signed_part(bytes.size() + 64);
    size_t size = signed_part.size();
    uint8_t hash[32];
    if (!chat::lxmf::buildSignedPart(envelope.destination_hash, envelope.source_hash,
                                     envelope.packed_payload.data(), envelope.packed_payload.size(), signed_part.data(), &size, hash) ||
        !ed25519_verify(envelope.signature, signed_part.data(), size, key.data() + 32)) return 3;
    chat::lxmf::ByteSpan type, payload;
    constexpr char application_type[] = "trailmate.geocache";
    if (chat::lxmf::extractCustomData(text, &type, &payload) != chat::lxmf::CustomDataResult::Valid ||
        type.size != sizeof(application_type) - 1 || std::memcmp(type.data, application_type, sizeof(application_type) - 1)) return 4;
    geocaching::protocol::CmpReader reader({payload.data, payload.size});
    size_t count = 0;
    uint64_t value = 0, operation = 0;
    geocaching::ByteView id;
    if (!reader.array(count, 6) || !reader.unsignedInteger(value) || !reader.unsignedInteger(value) ||
        !reader.unsignedInteger(operation) || !reader.binary(id, 16) || id.size != 16) return 5;
    geocaching::RequestId request;
    std::memcpy(request.bytes.data(), id.data, 16);
    std::vector<uint8_t> reference;
    geocaching::protocol::VerifiedRecordView expected;
    const bool has_expected = argc == 4;
    if (has_expected)
    {
        std::ifstream file(argv[3], std::ios::binary);
        reference.assign(std::istreambuf_iterator<char>(file), {});
        std::vector<uint8_t> scratch(reference.size() + 64);
        RecordCrypto crypto;
        if (geocaching::protocol::verifyGeocache({reference.data(), reference.size()}, crypto, scratch.data(), scratch.size(), expected) !=
            geocaching::protocol::VerificationResult::Valid) return 13;
    }
    if (operation == 0)
    {
        geocaching::protocol::DirectoryCapabilities capabilities;
        if (!geocaching::protocol::decodeDirectoryCapabilities({payload.data, payload.size}, request, capabilities)) return 6;
    }
    else if (operation == 1)
    {
        geocaching::protocol::PublishDisposition disposition;
        if (!has_expected || !geocaching::protocol::decodePublishResponse({payload.data, payload.size}, request, expected.id, expected.hash,
                                                                          expected.record.revision, expected.record.state, disposition)) return 14;
    }
    else if (operation == 2)
    {
        geocaching::protocol::QueryPageView page;
        geocaching::protocol::SummaryView summary;
        if (!geocaching::protocol::decodeQueryPage({payload.data, payload.size}, request, 2048, 20, page)) return 7;
        geocaching::protocol::CmpReader rows(page.encoded_items);
        bool matched = false;
        for (size_t index = 0; index < page.count; ++index)
        {
            if (!geocaching::protocol::decodeSummary(rows, summary)) return 8;
            if (!has_expected) matched = matched || summary.name == "Test";
            else if (summary.id.bytes == expected.id.bytes && summary.hash.bytes == expected.hash.bytes)
                matched = summary.name == expected.record.name && summary.revision == expected.record.revision &&
                          summary.state == expected.record.state && summary.latitude_e7 == expected.record.latitude_e7 &&
                          summary.longitude_e7 == expected.record.longitude_e7 && summary.difficulty_x2 == expected.record.difficulty_x2 &&
                          summary.terrain_x2 == expected.record.terrain_x2 && summary.container_size == expected.record.container_size &&
                          summary.signed_bytes == reference.size();
        }
        if (!matched) return 8;
    }
    else if (operation == 3)
    {
        geocaching::protocol::GetResponseView response;
        geocaching::protocol::VerifiedRecordView record;
        RecordCrypto crypto;
        if (!geocaching::protocol::decodeGetResponse({payload.data, payload.size}, request, 8192, response)) return 11;
        std::vector<uint8_t> scratch(response.signed_cache.size + 64);
        if (geocaching::protocol::verifyGeocache(response.signed_cache, crypto, scratch.data(), scratch.size(), record,
                                                 has_expected ? &expected.id : nullptr, has_expected ? &expected.hash : nullptr) !=
                geocaching::protocol::VerificationResult::Valid ||
            record.record.name != (has_expected ? expected.record.name : std::string_view("Test"))) return 12;
        if (has_expected && (response.signed_cache.size != reference.size() || std::memcmp(response.signed_cache.data, reference.data(), reference.size()))) return 15;
    }
    else return 9;
    std::printf("Firmware codecs verified real LXMF signature and operation %u\n", unsigned(operation));
    return 0;
}
