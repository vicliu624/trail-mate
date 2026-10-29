#include "platform/linux/hostlink_identity_backup.h"
#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/identity_backup_request.h"
#include "hostlink/identity_restore_service.h"
#include "platform/linux/identity_restore_bridge.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace platform::linux_runtime
{
bool HostlinkIdentityBackup::supported()
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    return true;
#else
    return false;
#endif
}
chat::DeviceAuthorResult HostlinkIdentityBackup::exportDeviceIdentityBackup(uint32_t generation, uint8_t* output, size_t capacity, size_t& written) const
{
    using Result = chat::DeviceAuthorResult;
    written = 0;
    if (!output) return Result::InvalidInput;
    const auto out = reinterpret_cast<uintptr_t>(output), self = reinterpret_cast<uintptr_t>(this);
    if (out <= self ? self - out < capacity : out - self < sizeof(*this)) return Result::InvalidInput;
    const auto fail = [&](Result result)
    { std::fill_n(output, std::min(capacity, size_t(448)), 0); return result; };
    if (!supported()) return fail(Result::Unsupported);
    if (!generation || capacity < 448) return fail(Result::InvalidInput);
    LinuxReticulumIdentity identity(database_);
    if (!identity.begin()) return fail(Result::StorageError);
    if (identity.generation() != generation) return fail(Result::StaleIdentity);
    struct Secrets
    {
        std::array<uint8_t, 96> private_key{};
        std::array<uint8_t, 448> message{};
        ~Secrets()
        {
            auto* bytes = reinterpret_cast<volatile uint8_t*>(this);
            for (size_t i = 0; i < sizeof(*this); ++i) bytes[i] = 0;
        }
    } secrets;
    size_t private_size = 0;
    if (!identity.copyPrivateKey(generation, secrets.private_key.data(), secrets.private_key.size(), private_size)) return fail(Result::StaleIdentity);
    static constexpr char domain[] = "TrailMate.Identity.Backup.v1";
    std::memcpy(secrets.message.data(), domain, sizeof(domain));
    auto* payload = secrets.message.data() + sizeof(domain);
    geocaching::protocol::CmpWriter writer(payload, secrets.message.size() - sizeof(domain));
    if (!writer.array(8) || !writer.unsignedInteger(1) || !writer.text("trailmate.identity.backup.v1") || !writer.unsignedInteger(identity.privateFormat()) ||
        !writer.binary({identity.publicKey().data(), 64}) || !writer.binary({secrets.private_key.data(), private_size}) ||
        !writer.binary({identity.identityHash().data(), 16}) || !writer.binary({identity.destinationHash().data(), 16}) || !writer.unsignedInteger(generation)) return fail(Result::InvalidInput);
    std::array<uint8_t, 64> signature{};
    if (!identity.sign(secrets.message.data(), sizeof(domain) + writer.size(), signature.data())) return fail(Result::Unavailable);
    geocaching::protocol::CmpWriter response(output, capacity);
    if (!response.array(2) || !response.binary({payload, writer.size()}) || !response.binary({signature.data(), signature.size()})) return fail(Result::InvalidInput);
    written = response.size();
    return Result::Ok;
}
bool HostlinkIdentityBackup::restoreSupported() const
{
    return supported() && identity_restore_available(database_);
}
hostlink::management::Status HostlinkIdentityBackup::process(const hostlink::management::EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    namespace mgmt = hostlink::management;
    if (request.code == uint16_t(mgmt::Operation::IdentityBackupRestore))
    {
        if (!restoreSupported()) return mgmt::Status::Unsupported;
        const auto valid = validate_identity_restore(request.body, request.body_size);
        if (valid != mgmt::Status::Ok) return valid;
        struct Frontend
        {
            const mgmt::EnvelopeView& input;
            const std::filesystem::path& database;
            chat::DeviceIdentityRestoreResult restoreDeviceIdentity(const mgmt::IdentityRestoreView&, uint8_t*, size_t)
            {
                IdentityRestoreCommand command;
                command.size = input.body_size;
                std::copy_n(input.body, input.body_size, command.body.data());
                return request_identity_restore(command, database);
            }
        } adapter{request, database_};
        mgmt::IdentityRestoreWork work;
        return mgmt::processIdentityRestore(adapter, request, {}, work, output, capacity, written);
    }
    return hostlink::management::processIdentityBackupExport(*this, request, output, capacity, written);
}
} // namespace platform::linux_runtime
