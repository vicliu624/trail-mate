#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/identity_restore_request.h"
#include "platform/linux/hostlink_identity_backup.h"
#include "platform/linux/identity_restore_bridge.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <unistd.h>

namespace native = platform::linux_runtime;
namespace mgmt = hostlink::management;
void check(const std::filesystem::path& database, uint32_t generation, uint8_t format)
{
    native::HostlinkIdentityBackup service(database);
    uint8_t session[16]{1}, id[16]{2};
    std::array<uint8_t, 16> body{};
    geocaching::protocol::CmpWriter writer(body.data(), body.size());
    assert(writer.array(2) && writer.unsignedInteger(generation));
    body[writer.size()] = 0xc3;
    mgmt::EnvelopeView request;
    request.session = session;
    request.request = id;
    request.code = uint16_t(mgmt::Operation::IdentityBackupExport);
    request.body = body.data();
    request.body_size = writer.size() + 1;
    std::array<uint8_t, 448> response{};
    size_t written = 0;
    assert(service.process(request, response.data(), response.size(), written) == mgmt::Status::Ok);
    geocaching::protocol::CmpReader reader({response.data(), written});
    size_t fields = 0;
    geocaching::ByteView payload, signature;
    assert(reader.array(fields, 2) && fields == 2 && reader.binary(payload, 320) && reader.binary(signature, 64) && reader.finished());
    native::LinuxReticulumIdentity identity(database);
    assert(identity.begin());
    std::array<uint8_t, 448> restore_body{};
    geocaching::protocol::CmpWriter restore(restore_body.data(), restore_body.size());
    assert(restore.array(5) && restore.unsignedInteger(generation) && restore.binary({identity.publicKey().data(), 64}) &&
           restore.binary(payload) && restore.binary(signature));
    restore_body[restore.size()] = 0xc3;
    mgmt::IdentityRestoreView decoded;
    assert(mgmt::parseIdentityRestoreRequest({restore_body.data(), restore.size() + 1}, decoded) == mgmt::Status::Ok);
    std::array<uint8_t, 352> scratch{};
    assert(decoded.format == format && decoded.backup_generation == generation && mgmt::verifyIdentityRestoreSignature(decoded, scratch.data(), scratch.size()));
    assert(native::LinuxReticulumIdentity::validateKeyPair(format, decoded.private_key.data, decoded.private_key.size, decoded.public_key.data));
    assert(native::validate_identity_restore(restore_body.data(), restore.size() + 1) == mgmt::Status::Ok);
    const auto reject_signed_field = [&](geocaching::ByteView field)
    {
        auto* byte = const_cast<uint8_t*>(field.data) + field.size / 2;
        *byte ^= 0x20;
        static constexpr char domain[] = "TrailMate.Identity.Backup.v1";
        std::array<uint8_t, 352> message{};
        std::copy_n(reinterpret_cast<const uint8_t*>(domain), sizeof(domain), message.data());
        std::copy_n(decoded.payload.data, decoded.payload.size, message.data() + sizeof(domain));
        std::array<uint8_t, 64> signature{};
        assert(identity.sign(message.data(), sizeof(domain) + decoded.payload.size, signature.data()));
        std::copy(signature.begin(), signature.end(), const_cast<uint8_t*>(decoded.signature.data));
        assert(native::validate_identity_restore(restore_body.data(), restore.size() + 1) == mgmt::Status::InvalidField);
        *byte ^= 0x20;
        message.fill(0);
    };
    reject_signed_field(decoded.identity_hash);
    reject_signed_field(decoded.delivery);
    reject_signed_field(decoded.private_key);
    response.fill(0);
    restore_body.fill(0);
    scratch.fill(0);
}
int main()
{
    char directory[] = "/tmp/trailmate-identity-backup-XXXXXX";
    assert(mkdtemp(directory));
    const auto path = std::filesystem::path(directory) / "identity.db";
    {
        native::LinuxReticulumIdentity identity(path);
        assert(identity.begin());
        check(path, 1, 1);
        std::array<uint8_t, 96> seed{}, expanded{};
        size_t size = 0;
        assert(identity.copyPrivateKey(1, seed.data(), seed.size(), size));
        std::copy_n(seed.data(), 32, expanded.data());
        expanded[0] &= 0xf8;
        expanded[31] = (expanded[31] & 0x7f) | 0x40;
        uint8_t public_key[32];
        ed25519_create_keypair(public_key, expanded.data() + 32, seed.data() + 32);
        assert(identity.replaceKeyPair(1, identity.publicKey().data(), 0, expanded.data(), 96, identity.publicKey().data()) == chat::DeviceIdentityRestoreResult::CommittedRebootRequired);
        seed.fill(0);
        expanded.fill(0);
    }
    check(path, 2, 0);
    std::filesystem::remove_all(directory);
}
