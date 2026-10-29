#include "chat/linux_track_read.h"
#include "platform/linux/hostlink_track_read_owner.h"
#include "platform/linux/track_read_runtime.h"

namespace trailmate::linux_app
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
namespace native = platform::linux_runtime;
namespace ts = track_sharing;
namespace mgmt = ::hostlink::management;
using Status = mgmt::Status;
struct LinuxTrackRead::Impl
{
    Impl(std::filesystem::path storage, std::filesystem::path database, const native::LinuxReticulumIdentity& identity, Directory directory, Transport transport, MessageSource messages)
        : storage(std::move(storage)), database(std::move(database)), identity(identity), directory(std::move(directory)), transport(std::move(transport)), messages(std::move(messages)) {}
    void retire()
    {
        read.reset();
        if (publisher) publisher->active(nullptr);
    }
    Status process(const native::TrackReadCommand& command, uint64_t now, size_t& written)
    {
        written = 0;
        geocaching::protocol::CmpWriter writer(output.data(), output.size());
        if (command.code == uint16_t(mgmt::Operation::TrackReadBegin))
        {
            ts::ReadUsbRequest decoded;
            if (!ts::decodeReadUsbRequest({command.body.data(), command.bytes}, decoded)) return Status::InvalidField;
            if (decoded.generation != generation) return Status::StaleIdentity;
            if (!enabled || !directory) return Status::Busy;
            if (read && (!read->session(command.session.data()) || !read->same({command.body.data(), command.bytes}))) return Status::Busy;
            if (!read)
            {
                ts::PublicationDirectory pinned;
                const auto selected = directory(command.session.data(), decoded.directory, decoded.epoch, pinned);
                if (selected != Status::Ok) return selected;
                read = std::make_unique<native::HostlinkTrackReadOwner>(storage, identity, command.session.data(), geocaching::ByteView{command.body.data(), command.bytes}, pinned, transport, messages, now);
                const auto admitted = read->admission();
                if (admitted != Status::Ok)
                {
                    retire();
                    return admitted;
                }
                publisher->active(command.session.data());
            }
            if (!read->current())
            {
                retire();
                return Status::StorageUnavailable;
            }
            read->touched = now;
            if (!writer.array(2) || !writer.binary(decoded.request) || !writer.unsignedInteger(decoded.operation)) return Status::Internal;
            written = writer.size();
            return Status::Ok;
        }
        const bool chunk = command.code == uint16_t(mgmt::Operation::TrackReadChunk);
        const bool release = command.code == uint16_t(mgmt::Operation::TrackReadRelease);
        if (!chunk && !release && command.code != uint16_t(mgmt::Operation::TrackReadStatus)) return Status::Unsupported;
        geocaching::protocol::CmpReader reader({command.body.data(), command.bytes});
        geocaching::ByteView request;
        size_t fields = 0;
        uint64_t offset = 0, count = 0;
        if (!reader.array(fields, 3) || fields != (chunk ? 3u : 1u) || !reader.binary(request, 16) || !ts::networkRequestId(request) ||
            (chunk && (!reader.unsignedInteger(offset) || offset > 8191 || !reader.unsignedInteger(count) || !count || count > 320)) || !reader.finished()) return Status::InvalidField;
        if (!read) return Status::Expired;
        if (!read->session(command.session.data())) return Status::Unauthorized;
        if (!read->request(request)) return Status::Conflict;
        if (release)
        {
            retire();
            output[0] = 0x90;
            written = 1;
            return Status::Ok;
        }
        ts::StoredReadResponse metadata;
        size_t copied = 0;
        const auto status = read->read(0, 0, output.data(), output.size(), copied, metadata);
        if (status != Status::Ok) return status;
        if (!chunk)
        {
            if (!writer.array(5) || !writer.binary(request) || !writer.unsignedInteger(metadata.operation) || !writer.unsignedInteger(metadata.status) ||
                !writer.unsignedInteger(metadata.bytes) || !writer.binary({metadata.digest, 32})) return Status::Internal;
            written = writer.size();
        }
        else
        {
            if (offset >= metadata.bytes || count > metadata.bytes - offset) return Status::InvalidField;
            if (!writer.array(4) || !writer.binary({metadata.digest, 32}) || !writer.unsignedInteger(metadata.bytes) || !writer.unsignedInteger(offset) || !writer.binaryHeader(count)) return Status::Internal;
            const auto header = writer.size();
            const auto loaded = read->read(uint32_t(offset), uint16_t(count), output.data() + header, output.size() - header, copied, metadata);
            if (loaded != Status::Ok || copied != count) return loaded == Status::Ok ? Status::StorageUnavailable : loaded;
            written = header + copied;
        }
        read->touched = now;
        return Status::Ok;
    }
    std::filesystem::path storage, database;
    const native::LinuxReticulumIdentity& identity;
    Directory directory;
    Transport transport;
    MessageSource messages;
    std::unique_ptr<native::TrackReadPublisher> publisher;
    std::unique_ptr<native::HostlinkTrackReadOwner> read;
    uint32_t generation = 0;
    uint64_t context = 0, register_after = 0;
    bool enabled = false;
    std::array<uint8_t, 448> output{};
};
LinuxTrackRead::LinuxTrackRead(std::filesystem::path storage, std::filesystem::path database, const native::LinuxReticulumIdentity& identity, Directory directory, Transport transport, MessageSource messages)
    : impl_(std::make_unique<Impl>(std::move(storage), std::move(database), identity, std::move(directory), std::move(transport), std::move(messages))) {}
LinuxTrackRead::~LinuxTrackRead() = default;
void LinuxTrackRead::tick(uint64_t now, bool context_current, uint64_t context_generation)
{
    auto& s = *impl_;
    if (!s.identity.ready() || (s.publisher && s.generation != s.identity.generation()))
    {
        s.retire();
        s.publisher.reset();
        if (!s.identity.ready()) return;
    }
    if (!s.publisher || !s.publisher->valid())
    {
        if (now < s.register_after) return;
        s.register_after = now + 1000;
        s.generation = s.identity.generation();
        s.publisher = std::make_unique<native::TrackReadPublisher>(s.database, s.generation);
        if (!s.publisher->valid()) return;
    }
    if (s.publisher->retiring() || !context_current || s.context != context_generation || (s.read && (now < s.read->touched || now - s.read->touched >= 30000))) s.retire();
    s.context = context_generation;
    s.enabled = context_current;
    native::TrackReadCommand command;
    if (s.publisher->take(command))
    {
        size_t bytes = 0;
        const auto result = s.process(command, now, bytes);
        if (!s.publisher->current(command.serial))
        {
            if (command.code == uint16_t(mgmt::Operation::TrackReadBegin)) s.retire();
        }
        else s.publisher->complete(command.serial, result, s.output.data(), bytes);
    }
    if (s.read) s.read->advance(now);
}
bool LinuxTrackRead::receive(chat::lxmf::ByteSpan type, const chat::lxmf::CustomDeliveryView& view, uint64_t)
{
    auto& s = *impl_;
    return s.enabled && chat::lxmf::isTrackCustomType(type) && s.read && s.read->receive(view);
}
#else
struct LinuxTrackRead::Impl
{
};
LinuxTrackRead::LinuxTrackRead(std::filesystem::path, std::filesystem::path, const platform::linux_runtime::LinuxReticulumIdentity&, Directory, Transport, MessageSource) : impl_(std::make_unique<Impl>()) {}
LinuxTrackRead::~LinuxTrackRead() = default;
void LinuxTrackRead::tick(uint64_t, bool, uint64_t) {}
bool LinuxTrackRead::receive(chat::lxmf::ByteSpan, const chat::lxmf::CustomDeliveryView&, uint64_t) { return false; }
#endif
} // namespace trailmate::linux_app
