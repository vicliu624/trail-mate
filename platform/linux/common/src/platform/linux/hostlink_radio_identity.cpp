#include "platform/linux/hostlink_radio_identity.h"
#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/radio_identity_backup.h"
#include "platform/linux/identity_restore_bridge.h"
#include "platform/linux/mesh/linux_radio_identity_store.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace platform::linux_runtime
{
namespace
{
bool invalidOutput(const void* object, size_t size, uint8_t* output, size_t capacity)
{
    if (!output || capacity < 448) return true;
    const auto out = reinterpret_cast<uintptr_t>(output), self = reinterpret_cast<uintptr_t>(object);
    return out <= self ? self - out < capacity : out - self < size;
}
} // namespace
bool HostlinkRadioIdentity::supported()
{
#if defined(__linux__) && defined(TRAIL_MATE_HAS_OPENSSL)
    return true;
#else
    return false;
#endif
}
bool HostlinkRadioIdentity::restoreSupported() const { return supported() && identity_restore_available(database_); }
chat::DeviceAuthorResult HostlinkRadioIdentity::readRadioIdentityInfo(uint8_t kind, uint32_t generation, uint8_t* output, size_t capacity, size_t& written) const
{
    using Result = chat::DeviceAuthorResult;
    written = 0;
    if (invalidOutput(this, sizeof(*this), output, capacity) || !generation || kind < 1 || kind > 2) return Result::InvalidInput;
    if (!supported()) return Result::Unsupported;
    LinuxReticulumIdentity author(database_);
    if (!author.begin()) return Result::StorageError;
    if (author.generation() != generation) return Result::StaleIdentity;
    RadioIdentitySnapshot snapshot;
    if (!LinuxRadioIdentityStore(database_).read(kind, snapshot).ok) return Result::StorageError;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(4) || !writer.unsignedInteger(kind) || !writer.unsignedInteger(snapshot.present ? 1 : 0) || !writer.unsignedInteger(snapshot.generation) ||
        !(snapshot.present ? writer.binary({snapshot.public_key.data(), 32}) : writer.nil())) return Result::InvalidInput;
    written = writer.size();
    return Result::Ok;
}
chat::DeviceAuthorResult HostlinkRadioIdentity::exportRadioIdentityBackup(uint8_t kind, uint32_t generation, uint8_t* output, size_t capacity, size_t& written,
                                                                          const uint8_t* session, const uint8_t* request) const
{
    using Result = chat::DeviceAuthorResult;
    written = 0;
    if (invalidOutput(this, sizeof(*this), output, capacity) || !session || !request || !generation || kind < 1 || kind > 2) return Result::InvalidInput;
    // The shared parser may put the borrowed IDs in the output arena's tail.
    std::array<uint8_t, 16> session_id{}, request_id{};
    std::copy_n(session, 16, session_id.data());
    std::copy_n(request, 16, request_id.data());
    const auto fail = [&](Result result)
    { std::fill_n(output, std::min(capacity, size_t(448)), 0); return result; };
    if (!supported()) return fail(Result::Unsupported);
    LinuxReticulumIdentity author(database_);
    if (!author.begin()) return fail(Result::StorageError);
    if (author.generation() != generation) return fail(Result::StaleIdentity);
    RadioIdentitySnapshot snapshot;
    if (!LinuxRadioIdentityStore(database_).read(kind, snapshot).ok) return fail(Result::StorageError);
    if (!snapshot.present) return fail(Result::Unavailable);
    struct Message
    {
        std::array<uint8_t, 448> bytes{};
        ~Message()
        {
            volatile uint8_t* p = bytes.data();
            for (size_t i = 0; i < bytes.size(); ++i) p[i] = 0;
        }
    } message;
    namespace mgmt = hostlink::management;
    std::copy_n(reinterpret_cast<const uint8_t*>(mgmt::kRadioIdentityBackupDomain), sizeof(mgmt::kRadioIdentityBackupDomain), message.bytes.data());
    auto* payload = message.bytes.data() + sizeof(mgmt::kRadioIdentityBackupDomain);
    geocaching::protocol::CmpWriter writer(payload, message.bytes.size() - sizeof(mgmt::kRadioIdentityBackupDomain));
    if (!writer.array(10) || !writer.unsignedInteger(1) || !writer.text(mgmt::kRadioIdentityBackupType) || !writer.unsignedInteger(kind) ||
        !writer.binary({snapshot.public_key.data(), 32}) || !writer.binary({snapshot.private_key.data(), snapshot.private_size}) ||
        !writer.binary({author.publicKey().data(), 64}) || !writer.unsignedInteger(generation) || !writer.unsignedInteger(snapshot.generation) ||
        !writer.binary({session_id.data(), 16}) || !writer.binary({request_id.data(), 16})) return fail(Result::InvalidInput);
    std::array<uint8_t, 64> signature{};
    if (!author.sign(message.bytes.data(), sizeof(mgmt::kRadioIdentityBackupDomain) + writer.size(), signature.data())) return fail(Result::Unavailable);
    geocaching::protocol::CmpWriter response(output, capacity);
    if (!response.array(2) || !response.binary({payload, writer.size()}) || !response.binary({signature.data(), signature.size()})) return fail(Result::InvalidInput);
    written = response.size();
    return Result::Ok;
}
hostlink::management::Status HostlinkRadioIdentity::process(const hostlink::management::EnvelopeView& input, uint8_t* output, size_t capacity, size_t& written)
{
    namespace mgmt = hostlink::management;
    written = 0;
    if (!supported()) return mgmt::Status::Unsupported;
    if (input.code != uint16_t(mgmt::Operation::RadioIdentityBackupRestore)) return mgmt::radioIdentityReadOperation(*this, input, output, capacity, written);
    if (!restoreSupported()) return mgmt::Status::Unsupported;
    const auto valid = validate_radio_identity_restore(input.body, input.body_size);
    if (valid != mgmt::Status::Ok) return valid;
    struct Frontend
    {
        const mgmt::EnvelopeView& input;
        const std::filesystem::path& database;
        chat::DeviceIdentityRestoreResult restoreRadioIdentity(const mgmt::RadioIdentityRestoreView&, uint8_t*, size_t)
        {
            IdentityRestoreCommand command;
            command.target = IdentityRestoreTarget::Radio;
            command.size = input.body_size;
            std::copy_n(input.body, input.body_size, command.body.data());
            return request_identity_restore(command, database);
        }
    } adapter{input, database_};
    mgmt::RadioIdentityRestoreWork work;
    return mgmt::processRadioIdentityRestore(adapter, input, {}, work, output, capacity, written);
}
} // namespace platform::linux_runtime
