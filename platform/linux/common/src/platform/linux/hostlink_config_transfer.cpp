#include "platform/linux/hostlink_config_transfer.h"
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/hostlink_codec.h"
#include "platform/linux/application_lifecycle.h"
#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/hostlink_config_snapshot.h"
#include "platform/linux/tms_import_bridge.h"
#include "platform/linux/tms_snapshot_bridge.h"
#include <algorithm>
#include <cstring>

namespace platform::linux_runtime
{
namespace
{
namespace appstore = trailmate::linux_app;
namespace store = platform::ui::settings_store;
using Status = hostlink::management::Status;
using Operation = hostlink::management::Operation;
constexpr size_t kHeader = 111, kLimit = app::tms::kMaxDocumentBytes;
struct Record
{
    std::array<uint8_t, 16> owner{}, id{};
    std::array<uint8_t, 32> hash{}, base{};
    uint32_t total = 0;
    uint8_t phase = 0;
    std::string candidate;
};
uint32_t u32(const uint8_t* bytes)
{
    return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}
void put32(uint8_t* bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes[i] = uint8_t(value >> (8 * i));
}
std::string encode(const Record& record)
{
    std::string result(kHeader + record.candidate.size(), '\0');
    auto* bytes = reinterpret_cast<uint8_t*>(result.data());
    std::memcpy(bytes, "TMU1", 4);
    std::memcpy(bytes + 4, record.owner.data(), 16);
    std::memcpy(bytes + 20, record.id.data(), 16);
    std::memcpy(bytes + 36, record.hash.data(), 32);
    put32(bytes + 68, record.total);
    put32(bytes + 72, uint32_t(record.candidate.size()));
    bytes[76] = record.phase;
    std::memcpy(bytes + 77, record.base.data(), 32);
    const auto crc = hostlink::crc16_ccitt(bytes, 109);
    bytes[109] = uint8_t(crc);
    bytes[110] = uint8_t(crc >> 8);
    std::memcpy(bytes + kHeader, record.candidate.data(), record.candidate.size());
    return result;
}
bool hash(const std::string& bytes, std::array<uint8_t, 32>& output)
{
    LinuxGeocachingCrypto crypto;
    return crypto.sha256({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}, output.data());
}
bool load(Record& record, bool& exists)
{
    auto bytes = std::make_unique<std::array<uint8_t, kHeader + kLimit>>();
    size_t size = 0;
    const auto status = store::read_blob_into(appstore::kTmsTransferNamespace, appstore::kTmsTransferKey, bytes->data(), bytes->size(), &size);
    exists = status != store::BlobReadResult::Missing;
    if (!exists) return true;
    if (status != store::BlobReadResult::Ok || size < kHeader || std::memcmp(bytes->data(), "TMU1", 4) ||
        hostlink::crc16_ccitt(bytes->data(), 109) != (uint16_t((*bytes)[109]) | uint16_t((*bytes)[110]) << 8)) return false;
    record.total = u32(bytes->data() + 68);
    const auto offset = u32(bytes->data() + 72);
    record.phase = (*bytes)[76];
    if (!record.total || record.total > kLimit || offset > record.total || size != kHeader + offset || record.phase > 2 ||
        (record.phase && offset != record.total)) return false;
    std::copy_n(bytes->data() + 4, 16, record.owner.begin());
    std::copy_n(bytes->data() + 20, 16, record.id.begin());
    std::copy_n(bytes->data() + 36, 32, record.hash.begin());
    std::copy_n(bytes->data() + 77, 32, record.base.begin());
    record.candidate.assign(reinterpret_cast<const char*>(bytes->data() + kHeader), offset);
    std::array<uint8_t, 32> actual{};
    return !record.phase || (hash(record.candidate, actual) && actual == record.hash);
}
bool save(const Record& record)
{
    const auto bytes = encode(record);
    return store::put_blob(appstore::kTmsTransferNamespace, appstore::kTmsTransferKey, bytes.data(), bytes.size());
}
Status reply(const Record& record, uint8_t* output, size_t capacity, size_t& written, bool committed = false)
{
    geocaching::protocol::CmpWriter writer(output, capacity);
    const bool ok = committed ? writer.array(3) && writer.binary({record.id.data(), 16}) && writer.binary({record.hash.data(), 32}) && writer.unsignedInteger(application_restart_requested() ? 1 : 0) : writer.array(4) && writer.binary({record.id.data(), 16}) && writer.unsignedInteger(320) && writer.unsignedInteger(record.candidate.size()) && writer.unsignedInteger(record.phase ? 1 : 0);
    written = writer.size();
    return ok ? Status::Ok : Status::Internal;
}
} // namespace

bool HostlinkConfigTransfer::supported() { return HostlinkConfigSnapshot::supported() && tms_import_available(); }
bool HostlinkConfigTransfer::identityBusy() const
{
    Record record;
    bool exists = false;
    return !load(record, exists) || (exists && record.phase != 2);
}
bool HostlinkConfigTransfer::routes(const hostlink::management::EnvelopeView& request) const
{
    const auto operation = static_cast<Operation>(request.code);
    if (operation == Operation::ConfigValidate || operation == Operation::ConfigCommit) return true;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t count = 0;
    geocaching::ByteView id;
    if (!reader.array(count, 4) || !reader.binary(id, 16) || id.size != 16) return false;
    if (operation == Operation::TransferBegin)
    {
        uint64_t kind = 0;
        return reader.unsignedInteger(kind) && kind == 1;
    }
    if (request.code < 33 || request.code > 36) return false;
    Record record;
    bool exists = false;
    return load(record, exists) && exists && !std::memcmp(id.data, record.id.data(), 16);
}
Status HostlinkConfigTransfer::process(const hostlink::management::EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!supported()) return Status::Unsupported;
    if (!request.session || !output) return Status::InvalidField;
    const auto operation = static_cast<Operation>(request.code);
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    geocaching::ByteView id, data, expected;
    size_t fields = 0;
    const size_t wanted = operation == Operation::TransferBegin ? 4 : operation == Operation::TransferChunk ? 3
                                                                  : operation == Operation::ConfigCommit    ? 2
                                                                                                            : 1;
    if (!reader.array(fields, wanted) || fields != wanted || !reader.binary(id, 16) || id.size != 16) return Status::InvalidField;
    uint64_t offset = 0, kind = 0, total = 0;
    if (operation == Operation::TransferBegin && (!reader.unsignedInteger(kind) || kind != 1 || !reader.unsignedInteger(total) ||
                                                  !total || total > kLimit || !reader.binary(expected, 32) || expected.size != 32)) return Status::InvalidField;
    if (operation == Operation::TransferChunk && (!reader.unsignedInteger(offset) || offset > UINT32_MAX ||
                                                  !reader.binary(data, 320) || !data.size)) return Status::InvalidField;
    if (operation == Operation::ConfigCommit && (!reader.binary(expected, 32) || expected.size != 32)) return Status::InvalidField;
    if (!reader.finished()) return Status::InvalidField;
    Record record;
    bool exists = false;
    if (!load(record, exists)) return Status::StorageUnavailable;
    if (operation == Operation::TransferBegin)
    {
        if (exists && (record.phase < 2 || !std::memcmp(id.data, record.id.data(), 16)))
        {
            if (std::memcmp(record.owner.data(), request.session, 16)) return Status::Busy;
            if (std::memcmp(record.id.data(), id.data, 16) || total != record.total || std::memcmp(record.hash.data(), expected.data, 32)) return Status::Conflict;
        }
        else
        {
            if (settings_import_committed()) return Status::Busy;
            std::string base;
            const auto state = request_tms_snapshot(base);
            if (state == TmsSnapshotRead::Busy) return Status::Busy;
            if (state != TmsSnapshotRead::Ready || !hash(base, record.base)) return Status::StorageUnavailable;
            std::copy_n(request.session, 16, record.owner.begin());
            std::copy_n(id.data, 16, record.id.begin());
            std::copy_n(expected.data, 32, record.hash.begin());
            record.total = uint32_t(total);
            record.phase = 0;
            record.candidate.clear();
            if (!save(record)) return Status::StorageUnavailable;
        }
        return reply(record, output, capacity, written);
    }
    if (!exists) return Status::Expired;
    if (std::memcmp(record.owner.data(), request.session, 16)) return Status::Unauthorized;
    if (std::memcmp(record.id.data(), id.data, 16)) return Status::Conflict;
    if (operation == Operation::TransferAbort)
    {
        if (record.phase == 2) return Status::Conflict;
        if (!cancel_tms_import(record.id)) return Status::Busy;
        const SettingsMutation erase{appstore::kTmsTransferKey, 'x', "", true};
        if (!write_settings_batch(appstore::kTmsTransferNamespace, &erase, 1)) return Status::StorageUnavailable;
        if (!capacity) return Status::Internal;
        output[0] = 0x90;
        written = 1;
        return Status::Ok;
    }
    if (operation == Operation::TransferChunk)
    {
        if (offset > record.candidate.size() || offset > record.total || data.size > record.total - offset) return Status::Conflict;
        if (offset < record.candidate.size())
        {
            if (data.size > record.candidate.size() - offset || std::memcmp(record.candidate.data() + offset, data.data, data.size)) return Status::Conflict;
        }
        else
        {
            if (record.phase) return Status::Conflict;
            record.candidate.append(reinterpret_cast<const char*>(data.data), data.size);
            if (!save(record)) return Status::StorageUnavailable;
        }
    }
    else if (operation == Operation::TransferFinish || operation == Operation::ConfigValidate)
    {
        if (record.candidate.size() != record.total) return Status::Conflict;
        std::array<uint8_t, 32> actual{};
        if (!hash(record.candidate, actual)) return Status::StorageUnavailable;
        if (actual != record.hash) return Status::HashMismatch;
        if (!appstore::LinuxTmsImport::prepare(record.candidate.data(), record.candidate.size(), app::tms::DocumentKind::Working)) return Status::InvalidField;
        if (!record.phase)
        {
            record.phase = 1;
            if (!save(record)) return Status::StorageUnavailable;
        }
    }
    else if (operation == Operation::ConfigCommit)
    {
        if (!record.phase || std::memcmp(expected.data, record.base.data(), 32)) return Status::Conflict;
        if (record.phase != 2)
        {
            record.phase = 2;
            TmsImportCommand command;
            command.id = record.id;
            command.document = record.candidate;
            command.commit.expected_base_hash = record.base;
            command.commit.receipt = encode(record);
            const auto status = request_tms_import(command);
            if (status != Status::Ok) return status;
        }
        return reply(record, output, capacity, written, true);
    }
    else if (operation != Operation::TransferStatus) return Status::Unsupported;
    return reply(record, output, capacity, written);
}
} // namespace platform::linux_runtime
