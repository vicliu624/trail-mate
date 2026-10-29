#include "chat/ports/device_author.h"
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/management_envelope.h"
#include "platform/linux/hostlink_management.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "platform/linux/runtime_paths.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <sqlite3.h>
#include <unistd.h>

namespace native = platform::linux_runtime;
namespace mgmt = hostlink::management;
using Bytes = std::vector<uint8_t>;
using Writer = geocaching::protocol::CmpWriter;
using Reader = geocaching::protocol::CmpReader;
template <class Encode>
static Bytes encode(Encode encode)
{
    Bytes bytes(256);
    Writer writer(bytes.data(), bytes.size());
    encode(writer);
    assert(writer.good());
    bytes.resize(writer.size());
    return bytes;
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-hostlink-management-" + std::to_string(getpid()));
    const auto settings = root / "settings";
    setenv("TRAIL_MATE_SETTINGS_ROOT", settings.c_str(), 1);
    const auto database = native::sqlite_database_path();
    uint8_t session[16]{1}, request_id[16]{2}, challenge[32]{3};
    Bytes response;
    const auto call = [&](mgmt::Operation operation, const Bytes& body, mgmt::Status expected)
    {
        uint8_t request[512]{};
        size_t size = 0;
        assert(mgmt::encodeEnvelope(session, request_id, uint16_t(operation), body.data(), body.size(), request, sizeof(request), size));
        assert(native::process_hostlink_management(request, size, response) == hostlink::ErrorCode::Ok);
        mgmt::EnvelopeView decoded;
        assert(mgmt::decodeEnvelope(response.data(), response.size(), decoded));
        assert(decoded.code == uint16_t(expected) && !std::memcmp(decoded.session, session, 16) && !std::memcmp(decoded.request, request_id, 16));
        return Bytes(decoded.body, decoded.body + decoded.body_size);
    };
    const Bytes empty{0x90};
    assert(native::process_hostlink_management(nullptr, 0, response) == hostlink::ErrorCode::InvalidParam && response.empty());
    auto body = call(mgmt::Operation::Capabilities, empty, mgmt::Status::Ok);
    {
        Reader reader({body.data(), body.size()});
        size_t count = 0;
        uint64_t value = 0;
        assert(reader.array(count, 6) && count == 6);
        for (const uint64_t expected : {1, 512, 320, 1}) assert(reader.unsignedInteger(value) && value == expected);
        assert(reader.array(count, 4) && count == 4);
        for (uint64_t operation = 0; operation < 4; ++operation) assert(reader.unsignedInteger(value) && value == operation);
        assert(reader.unsignedInteger(value) && value == 0 && reader.finished());
    }
    assert(call(mgmt::Operation::TrackPackageInstall, empty, mgmt::Status::Unsupported) == empty);
    assert(call(mgmt::Operation::SessionClose, empty, mgmt::Status::Ok) == empty);
    assert(!std::filesystem::exists(database));
    body = call(mgmt::Operation::AuthorInfo, empty, mgmt::Status::Ok);
    std::array<uint8_t, 64> public_key{};
    uint64_t generation = 0;
    {
        Reader reader({body.data(), body.size()});
        size_t count = 0;
        geocaching::ByteView key, identity, destination;
        assert(reader.array(count, 4) && count == 4 && reader.binary(key, 64) && key.size == 64 &&
               reader.binary(identity, 16) && identity.size == 16 && reader.binary(destination, 16) && destination.size == 16 &&
               reader.unsignedInteger(generation) && generation && reader.finished());
        std::copy_n(key.data, 64, public_key.begin());
        native::LinuxReticulumIdentity persisted(database);
        assert(persisted.begin() && persisted.publicKey() == public_key && persisted.generation() == generation);
        assert(!std::memcmp(identity.data, persisted.identityHash().data(), 16) && !std::memcmp(destination.data, persisted.destinationHash().data(), 16));
    }
    assert(call(mgmt::Operation::AuthorInfo, empty, mgmt::Status::Ok) == body);
    const auto proof_request = [&](uint64_t expected, size_t challenge_size = 32)
    {
        return encode([&](Writer& writer)
                      { writer.array(2); writer.binary({challenge, challenge_size}); writer.unsignedInteger(expected); });
    };
    body = call(mgmt::Operation::AuthorProve, proof_request(generation), mgmt::Status::Ok);
    std::array<uint8_t, 64> signature{};
    {
        Reader reader({body.data(), body.size()});
        size_t count = 0;
        uint64_t proven = 0;
        geocaching::ByteView key, identity, destination, proof;
        assert(reader.array(count, 5) && count == 5 && reader.binary(key, 64) && key.size == 64 &&
               reader.binary(identity, 16) && identity.size == 16 && reader.binary(destination, 16) && destination.size == 16 &&
               reader.unsignedInteger(proven) && proven == generation && reader.binary(proof, 64) && proof.size == 64 && reader.finished());
        assert(!std::memcmp(key.data, public_key.data(), 64));
        std::copy_n(proof.data, 64, signature.begin());
    }
    std::array<uint8_t, 160> message{};
    size_t message_size = 0;
    assert(chat::deviceAuthorProofMessage(session, challenge, public_key.data(), generation, message.data(), message.size(), message_size));
    assert(native::LinuxReticulumIdentity::verify(public_key.data() + 32, message.data(), message_size, signature.data()));
    session[0] ^= 1;
    assert(chat::deviceAuthorProofMessage(session, challenge, public_key.data(), generation, message.data(), message.size(), message_size));
    assert(!native::LinuxReticulumIdentity::verify(public_key.data() + 32, message.data(), message_size, signature.data()));
    session[0] ^= 1;
    challenge[0] ^= 1;
    assert(chat::deviceAuthorProofMessage(session, challenge, public_key.data(), generation, message.data(), message.size(), message_size));
    assert(!native::LinuxReticulumIdentity::verify(public_key.data() + 32, message.data(), message_size, signature.data()));
    assert(call(mgmt::Operation::AuthorProve, proof_request(generation + 1), mgmt::Status::StaleIdentity) == empty);
    assert(call(mgmt::Operation::AuthorProve, proof_request(0), mgmt::Status::InvalidField) == empty);
    assert(call(mgmt::Operation::AuthorProve, proof_request(generation, 31), mgmt::Status::InvalidField) == empty);
    assert(call(mgmt::Operation::AuthorInfo, Bytes{0x91, 0}, mgmt::Status::InvalidField) == empty);
    sqlite3* db = nullptr;
    assert(sqlite3_open(database.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, "UPDATE linux_reticulum_identity SET public_key=zeroblob(64)", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
    assert(call(mgmt::Operation::AuthorInfo, empty, mgmt::Status::StorageUnavailable) == empty);
    native::LinuxReticulumIdentity corrupt(database);
    assert(!corrupt.begin()); // No silent replacement after corrupted keys.
    std::filesystem::remove_all(root);
}
